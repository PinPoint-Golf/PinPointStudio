/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// Standalone test for axial body rotation (src/Analysis/body_rotation.{h,cpp}).
// Synthetic tracks and synthetic IMU streams only — no fixture, no video.
//
// THE TWO THINGS THIS TEST EXISTS TO PIN:
//
//   1. THE TIER CHOICE. A bound Pelvis / Thorax stream must win over the camera, per segment and
//      independently — a swing with a pelvis IMU and no thorax IMU has to come back with one
//      measured turn and one estimated one, not a refusal and not two estimates.
//   2. THE MAGNITUDE CONVENTION. The curve is |turn from address|, positive at the top AND positive
//      at impact, passing through zero as the body squares up. That is what the shipped corridors
//      ask for (m_pelvisRotP4 at +45°, m_pelvisRotP7 at +40°), and a signed curve cannot satisfy
//      both. §3 asserts it directly, because getting this wrong would grade every downswing
//      backwards while looking entirely plausible.

#include "../body_rotation.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;

#define CHECK(label, cond)                                        \
    do {                                                          \
        const bool ok = (cond);                                   \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);  \
        if (!ok) ++g_fail;                                        \
    } while (0)

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

constexpr int kLSh = 5, kRSh = 6, kLHip = 11, kRHip = 12;
constexpr int kW = 1000, kH = 1000;
constexpr double kDeg2Rad = 0.017453292519943295;

// A frame whose hip and shoulder spans are the address spans scaled by cos(turn) — exactly the
// foreshortening the camera tier inverts. Building the input from the model the producer assumes
// is deliberate: this test is about the inversion, the sign and the tier choice, not about whether
// a real pelvis obeys a cosine (it does not exactly, which is what the sigma is for).
static PoseFrame2D makeTurned(int64_t t, double hipTurnDeg, double shoulderTurnDeg,
                              double hipSpan0 = 0.10, double shSpan0 = 0.16, float conf = 0.9f)
{
    PoseFrame2D f;
    f.t_us = t;
    const double hs = 0.5 * hipSpan0 * std::cos(hipTurnDeg * kDeg2Rad);
    const double ss = 0.5 * shSpan0 * std::cos(shoulderTurnDeg * kDeg2Rad);
    f.kp[kLHip] = QPointF(0.5 - hs, 0.55);  f.conf[kLHip] = conf;
    f.kp[kRHip] = QPointF(0.5 + hs, 0.55);  f.conf[kRHip] = conf;
    f.kp[kLSh]  = QPointF(0.5 - ss, 0.30);  f.conf[kLSh]  = conf;
    f.kp[kRSh]  = QPointF(0.5 + ss, 0.30);  f.conf[kRSh]  = conf;
    return f;
}

// A track that holds address, turns to `topDeg` at the top, and comes back through square to
// `impactDeg` open at impact. The camera cannot tell "away" from "open" — which is precisely why
// the series is a magnitude.
// TIMESCALE MATTERS HERE, and the first version of this test got it wrong. The address reference is
// a median over frames within ±addrWindowUs (250 ms) of the Address event, so a synthetic track
// compressed into 160 ms puts the TOP inside the address window and contaminates the very span the
// turn is measured against. That is not a producer defect — a real swing runs well over a second
// and the window picks out the address hold — it is a fixture that did not look like a swing. So
// this one does: 240 fps, an address hold, a backswing, a square transition and an impact, ~1.6 s.
constexpr int64_t kFrameUs = 4167;                 // 240 fps
constexpr int64_t kAddressUs = 100000;             // 0.10 s — inside the hold
constexpr int64_t kTopUs     = 900000;             // 0.90 s
constexpr int64_t kImpactUs  = 1300000;            // 1.30 s

static PoseTrack2D turningTrack(double hipTop, double shTop, double hipImpact, double shImpact)
{
    PoseTrack2D t;
    const auto span = [&](int64_t fromUs, int64_t toUs, double hip, double sh) {
        for (int64_t us = fromUs; us < toUs; us += kFrameUs)
            t.frames.push_back(makeTurned(us, hip, sh));
    };
    span(0,        400000,  0.0,       0.0);            // address hold
    span(400000,   800000,  hipTop * 0.5, shTop * 0.5); // backswing, halfway
    span(800000,  1000000,  hipTop,    shTop);          // the top
    span(1000000, 1150000,  0.0,       0.0);            // square, on the way down
    span(1150000, 1500000,  hipImpact, shImpact);       // impact and through
    return t;
}

static std::vector<PhaseEvent> ladder()
{
    return { { Phase::Address, kAddressUs, 1.f, SegmentRole::Unknown },
             { Phase::Top,     kTopUs,     1.f, SegmentRole::Unknown },
             { Phase::Impact,  kImpactUs,  1.f, SegmentRole::Unknown } };
}

static const MetricSeries *find(const std::vector<MetricSeries> &all, const char *key)
{
    for (const MetricSeries &m : all)
        if (m.key == QLatin1String(key)) return &m;
    return nullptr;
}

static bool sampleAt(const MetricSeries *m, Phase p, double &out)
{
    if (!m) return false;
    for (const PhaseSample &s : m->phaseSamples)
        if (s.phase == p) { out = s.value; return true; }
    return false;
}

// A synthetic anatomical stream: the segment's +X axis yawed by `deg` about world Z (up).
static SegmentStream yawStream(SegmentRole role, const std::vector<int64_t> &grid,
                               const std::vector<double> &yawDeg)
{
    SegmentStream s;
    s.role = role;
    for (size_t i = 0; i < grid.size(); ++i)
        s.qAnat.push_back(QQuaternion::fromAxisAndAngle(0.f, 0.f, 1.f, float(yawDeg[i])));
    return s;
}

int main()
{
    std::printf("=== body rotation ===\n");

    // ── 1. A SINGLE CAMERA PRODUCES NOTHING, and that is the headline ──────────────────────────
    //
    // The camera tier estimated turn from the collapse of an image span. It was honest in intent,
    // badged Bridged rather than Measured, and it carried a propagated sigma — and it still could
    // not do the job, for two reasons that no corridor could fix:
    //
    //   A COSINE IS FLAT WHERE THE SWING LIVES. dtheta/dratio = 1/sin(theta), so on a real capture
    //   (2026-09-09) the hip span's 2.1% scatter over STILL address frames became +/-1.9 deg at 40
    //   deg of turn but +/-13.9 deg at 5 deg — and impact is where the pelvis passes through
    //   square. Over a 41 ms delivery window that is +/-123 deg/s on a rate whose graded corridor
    //   is 100 deg/s wide.
    //
    //   A COSINE CARRIES NO SIGN. It cannot say which way the body turned, so the curve folded at
    //   the square-up instead of crossing it, and every derivative across impact inverted.
    //
    // Rotation about the body's vertical axis needs a route that reads GEOMETRY — a bound segment
    // IMU, or the hip/shoulder line's bearing triangulated from a calibrated pair. One view is not
    // one of them.
    {
        const PoseTrack2D t = turningTrack(45.0, 90.0, 40.0, 25.0);
        const BodyRotationResult r = trackBodyRotation(t, FusedStreams{}, kW, kH, true, ladder());
        CHECK("a face-on track with no IMU resolves nothing", !r.valid);
        CHECK("pelvis has no tier", r.pelvis.tier == RotationTier::None);
        CHECK("thorax has no tier", r.thorax.tier == RotationTier::None);

        // ABSENT, NEVER A CURVE OF ZEROS. A zeroed series would read as a golfer who does not turn.
        const auto series = buildBodyRotationSeries(r, ladder());
        CHECK("pelvisRotation is absent", find(series, "pelvisRotation") == nullptr);
        CHECK("thoraxRotation is absent",  find(series, "thoraxRotation") == nullptr);
        CHECK("xFactor is absent",         find(series, "xFactor") == nullptr);
        CHECK("xFactorStretch is absent",  find(series, "xFactorStretch") == nullptr);
    }

    // ── 2. A bound IMU measures it ─────────────────────────────────────────────────────────────
    {
        const PoseTrack2D t = turningTrack(45.0, 88.0, 40.0, 25.0);
        std::vector<int64_t> grid;
        std::vector<double>  yaw;
        for (const PoseFrame2D &f : t.frames) {
            grid.push_back(f.t_us);
            yaw.push_back(f.t_us >= 800000 && f.t_us < 1000000 ? 30.0 : 0.0);
        }
        FusedStreams fs;
        fs.timeGrid = grid;
        fs.segments.push_back(yawStream(SegmentRole::Pelvis, grid, yaw));

        const BodyRotationResult r = trackBodyRotation(t, fs, kW, kH, true, ladder());
        CHECK("pelvis came from the IMU", r.pelvis.tier == RotationTier::Imu);
        CHECK("the thorax has no IMU and so has nothing", r.thorax.tier == RotationTier::None);

        const auto series = buildBodyRotationSeries(r, ladder());
        double v = 0.0;
        CHECK("pelvisRotation at the top", sampleAt(find(series, "pelvisRotation"), Phase::Top, v));
        CHECK("the IMU's 30 degrees", near(v, 30.0, 2.0));
        CHECK("the measured tier claims no error budget",
              !find(series, "pelvisRotation")->sigma.has_value());
        // The chest is ABSENT rather than estimated — there is no estimator left to fall back to.
        CHECK("thoraxRotation absent without its own IMU", find(series, "thoraxRotation") == nullptr);
        CHECK("and no X-factor from half a pair", find(series, "xFactor") == nullptr);
    }

    // ── 3. The IMU tier is a MAGNITUDE ─────────────────────────────────────────────────────────
    // Yawing the other way must read the same number: one curve, one meaning. This is the
    // convention the shipped corridors rest on — m_pelvisRotP4 at +45 (turned away at the top) and
    // m_pelvisRotP7 at +40 (turned open at impact) cannot both be positive on a signed curve.
    {
        const PoseTrack2D t = turningTrack(45.0, 88.0, 40.0, 25.0);
        std::vector<int64_t> grid;
        for (const PoseFrame2D &f : t.frames) grid.push_back(f.t_us);

        double pos = 0.0, neg = 0.0;
        for (int sign : { +1, -1 }) {
            std::vector<double> yaw;
            for (int64_t g : grid) yaw.push_back(g >= 800000 && g < 1000000 ? sign * 30.0 : 0.0);
            FusedStreams fs;
            fs.timeGrid = grid;
            fs.segments.push_back(yawStream(SegmentRole::Pelvis, grid, yaw));
            const auto series = buildBodyRotationSeries(
                trackBodyRotation(t, fs, kW, kH, true, ladder()), ladder());
            sampleAt(find(series, "pelvisRotation"), Phase::Top, sign > 0 ? pos : neg);
        }
        std::printf("    imu yaw +30 -> %.2f   yaw -30 -> %.2f\n", pos, neg);
        CHECK("turning either way reads the same magnitude", near(pos, neg, 0.5) && pos > 25.0);
    }

    // ── 4. X-factor needs BOTH segments instrumented ───────────────────────────────────────────
    {
        const PoseTrack2D t = turningTrack(45.0, 85.0, 40.0, 25.0);
        std::vector<int64_t> grid;
        std::vector<double>  hip, chest;
        for (const PoseFrame2D &f : t.frames) {
            grid.push_back(f.t_us);
            const bool atTop = f.t_us >= 800000 && f.t_us < 1000000;
            hip.push_back(atTop ? 40.0 : 0.0);
            chest.push_back(atTop ? 85.0 : 0.0);
        }
        FusedStreams fs;
        fs.timeGrid = grid;
        fs.segments.push_back(yawStream(SegmentRole::Pelvis, grid, hip));
        fs.segments.push_back(yawStream(SegmentRole::Thorax, grid, chest));

        const auto series = buildBodyRotationSeries(
            trackBodyRotation(t, fs, kW, kH, true, ladder()), ladder());
        double xf = 0.0;
        CHECK("xFactor at the top", sampleAt(find(series, "xFactor"), Phase::Top, xf));
        CHECK("chest minus pelvis at the top", near(xf, 45.0, 3.0));

        const MetricSeries *st = find(series, "xFactorStretch");
        CHECK("xFactorStretch emitted", st != nullptr);
        double sTop = 0.0;
        CHECK("stretch at the top", sampleAt(st, Phase::Top, sTop));
        CHECK("the stretch is zero AT the top by construction", near(sTop, 0.0, 0.5));
    }

    // ── 5. Refusals ────────────────────────────────────────────────────────────────────────────
    {
        const BodyRotationResult none = trackBodyRotation(PoseTrack2D{}, FusedStreams{}, kW, kH,
                                                          true, ladder());
        CHECK("no pose and no IMU refuses", !none.valid);

        // No Top means no anchor for the stretch — and the producer must not invent one.
        const PoseTrack2D t = turningTrack(45.0, 85.0, 40.0, 25.0);
        std::vector<int64_t> grid;
        std::vector<double>  hip, chest;
        for (const PoseFrame2D &f : t.frames) {
            grid.push_back(f.t_us);
            const bool atTop = f.t_us >= 800000 && f.t_us < 1000000;
            hip.push_back(atTop ? 40.0 : 0.0);
            chest.push_back(atTop ? 85.0 : 0.0);
        }
        FusedStreams fs;
        fs.timeGrid = grid;
        fs.segments.push_back(yawStream(SegmentRole::Pelvis, grid, hip));
        fs.segments.push_back(yawStream(SegmentRole::Thorax, grid, chest));

        const std::vector<PhaseEvent> noTop = {
            { Phase::Address, kAddressUs, 1.f, SegmentRole::Unknown },
            { Phase::Impact,  kImpactUs,  1.f, SegmentRole::Unknown } };
        const auto series = buildBodyRotationSeries(
            trackBodyRotation(t, fs, kW, kH, true, noTop), noTop);
        CHECK("no Top => no xFactorStretch", find(series, "xFactorStretch") == nullptr);
        CHECK("but the turns themselves still land", find(series, "pelvisRotation") != nullptr);
    }

    return g_fail == 0 ? 0 : 1;
}
