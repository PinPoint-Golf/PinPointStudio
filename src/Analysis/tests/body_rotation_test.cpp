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

#include "../../Body/body_rotation.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

    // ── 6. The IMU's SIGNED twins (trunk design defect 2) ──────────────────────────────────────
    // With a pelvis IMU bound the two-camera stage steps aside for the pelvis, so the signed series
    // hip_stall's rate reads has to come from here. Lead-relative: for a right-hander (lead = left)
    // opening is +Z, so a yaw of −30° at the top (turned away) reads −30 and +20° at impact (open)
    // reads +20; a left-hander's same numbers are the mirror. The magnitude beside it is unchanged.
    {
        const PoseTrack2D t = turningTrack(45.0, 88.0, 40.0, 25.0);
        std::vector<int64_t> grid;
        std::vector<double>  hip, chest;
        for (const PoseFrame2D &f : t.frames) {
            grid.push_back(f.t_us);
            const bool atTop = f.t_us >= 800000 && f.t_us < 1000000;
            const bool past  = f.t_us >= 1150000;
            hip.push_back(atTop ? -30.0 : past ? 20.0 : 0.0);
            chest.push_back(atTop ? -80.0 : past ? 10.0 : 0.0);
        }
        const auto run = [&](bool leadIsLeft, bool withThorax) {
            FusedStreams fs;
            fs.timeGrid = grid;
            fs.segments.push_back(yawStream(SegmentRole::Pelvis, grid, hip));
            if (withThorax) fs.segments.push_back(yawStream(SegmentRole::Thorax, grid, chest));
            return buildBodyRotationSeries(trackBodyRotation(t, fs, kW, kH, leadIsLeft, ladder()), ladder());
        };

        const auto rh = run(true, true);
        const MetricSeries *ps = find(rh, "pelvisRotationSigned");
        const MetricSeries *ts = find(rh, "thoraxRotationSigned");
        CHECK("pelvisRotationSigned from the pelvis IMU", ps != nullptr);
        CHECK("thoraxRotationSigned from the thorax IMU", ts != nullptr);
        double v = 0.0;
        CHECK("signed pelvis at the top ≈ −30 (closed, right-hander)",
              sampleAt(ps, Phase::Top, v) && near(v, -30.0, 0.5));
        CHECK("signed pelvis at impact ≈ +20 (open)", sampleAt(ps, Phase::Impact, v) && near(v, 20.0, 0.5));
        CHECK("signed thorax at the top ≈ −80", sampleAt(ts, Phase::Top, v) && near(v, -80.0, 0.5));
        CHECK("signed thorax at impact ≈ +10", sampleAt(ts, Phase::Impact, v) && near(v, 10.0, 0.5));
        CHECK("the magnitude beside it is still |turn| (20 at impact)",
              sampleAt(find(rh, "pelvisRotation"), Phase::Impact, v) && near(v, 20.0, 0.5));
        CHECK("the IMU signed series claims no error budget (design §8.2 not built)",
              ps && !ps->sigma.has_value() && ts && !ts->sigma.has_value());

        const auto lh = run(false, true);
        CHECK("left-hander: the same yaw reads the mirror (+30 at the top)",
              sampleAt(find(lh, "pelvisRotationSigned"), Phase::Top, v) && near(v, 30.0, 0.5));
        CHECK("left-hander: thorax mirrored too (−10 at impact)",
              sampleAt(find(lh, "thoraxRotationSigned"), Phase::Impact, v) && near(v, -10.0, 0.5));

        const auto pelvisOnly = run(true, false);
        CHECK("pelvis IMU alone: the signed pelvis, no signed thorax",
              find(pelvisOnly, "pelvisRotationSigned") && !find(pelvisOnly, "thoraxRotationSigned"));

        // With no DTL the IMU route's refusal is final, and the chest's covers its signed twin.
        FusedStreams fs;
        fs.timeGrid = grid;
        fs.segments.push_back(yawStream(SegmentRole::Pelvis, grid, hip));
        const BodyRotationResult r = trackBodyRotation(t, fs, kW, kH, true, ladder());
        bool covered = false, pelvisRefused = false;
        for (const GateRecord &g : bodyRotationGates(r, fs, /*dtlCamera*/ false, /*triangulatedOn*/ true)) {
            if (g.subject == QLatin1String("pelvisRotation")) pelvisRefused = true;
            if (g.subject == QLatin1String("thoraxRotation") && g.covers.contains(QStringLiteral("thoraxRotationSigned")))
                covered = true;
        }
        CHECK("no thorax IMU, no DTL: the chest refusal covers thoraxRotationSigned", covered);
        CHECK("…and the measured pelvis is not refused", !pelvisRefused);
    }

    // ── 7. AN IMU-ONLY JOB: no camera, so no pose track and a 0×0 frame (trunk design §8.3) ────
    // The worn session's capture is trunk sensors alone. BodyRotationStage::run hands this
    // producer exactly what such a job gives it — an EMPTY pose track and w = h = 0, the stage no
    // longer reading cameraSources.front() off an empty list — and all four turn series must land
    // from the IMU tier on the stream grid. This is the stage's own call, asserted here because
    // analysis_pipeline.cpp (where the stage lives, file-local) is not built into the test tree.
    {
        std::printf("\n--- IMU-only job: empty pose track, 0x0 frame ---\n");
        std::vector<int64_t> grid;
        std::vector<double>  hip, chest;
        for (int64_t t = 0; t <= 1600000; t += 5000) {
            grid.push_back(t);
            const bool atTop = t >= 800000 && t < 1000000;
            const bool past  = t >= 1150000;
            hip.push_back(atTop ? -30.0 : past ? 20.0 : 0.0);
            chest.push_back(atTop ? -80.0 : past ? 10.0 : 0.0);
        }
        FusedStreams fs;
        fs.timeGrid = grid;
        fs.segments.push_back(yawStream(SegmentRole::Pelvis, grid, hip));
        fs.segments.push_back(yawStream(SegmentRole::Thorax, grid, chest));

        const BodyRotationResult r = trackBodyRotation(PoseTrack2D{}, fs, /*w*/ 0, /*h*/ 0, true, ladder());
        CHECK("IMU-only: the result is valid", r.valid);
        CHECK("IMU-only: the grid is the stream grid", r.grid == grid);
        const auto series = buildBodyRotationSeries(r, ladder());
        const MetricSeries *pm = find(series, "pelvisRotation");
        const MetricSeries *ps = find(series, "pelvisRotationSigned");
        const MetricSeries *tm = find(series, "thoraxRotation");
        const MetricSeries *ts = find(series, "thoraxRotationSigned");
        CHECK("IMU-only: pelvisRotation", pm != nullptr);
        CHECK("IMU-only: pelvisRotationSigned", ps != nullptr);
        CHECK("IMU-only: thoraxRotation", tm != nullptr);
        CHECK("IMU-only: thoraxRotationSigned", ts != nullptr);
        double v = 0.0;
        CHECK("IMU-only: pelvis magnitude at the top ≈ 30", sampleAt(pm, Phase::Top, v) && near(v, 30.0, 0.5));
        CHECK("IMU-only: signed pelvis at the top ≈ −30", sampleAt(ps, Phase::Top, v) && near(v, -30.0, 0.5));
        CHECK("IMU-only: signed thorax at impact ≈ +10", sampleAt(ts, Phase::Impact, v) && near(v, 10.0, 0.5));
        CHECK("IMU-only: no gate refuses either measured segment",
              bodyRotationGates(r, fs, /*dtlCamera*/ false, /*triangulatedOn*/ true).empty());
    }

    // ── The two-camera route (trackBodyRotationTriangulated) ────────────────────────────────
    {
        std::printf("\n--- triangulated: skeleton3d bearings ---\n");
        const double kD = 3.14159265358979323846 / 180.0;
        TriangulatedTurnInput in;
        // 5 ms frames 0..1.6 s. Pelvis: still to 0.3 s, turns to 45° CLOSED (bearing −45°) at the
        // top, through square at impact, 30° open after. Thorax: 90° closed at the top.
        for (int64_t t = 0; t <= 1600000; t += 5000) {
            double p = 0.0, x = 0.0;
            if (t > 300000 && t <= kTopUs)      { const double u = double(t - 300000) / double(kTopUs - 300000); p = -45.0 * u; x = -90.0 * u; }
            else if (t > kTopUs && t <= kImpactUs) { const double u = double(t - kTopUs) / double(kImpactUs - kTopUs); p = -45.0 + 45.0 * u; x = -90.0 + 90.0 * u; }
            else if (t > kImpactUs)             { p = 30.0 * std::min(1.0, double(t - kImpactUs) / 200000.0); x = p; }
            in.t_us.push_back(t);
            in.pelvisBearing.push_back((180.0 + p) * kD); in.pelvisSigma.push_back(1.0 * kD);
            in.thoraxBearing.push_back((180.0 + x) * kD); in.thoraxSigma.push_back(1.0 * kD);
        }
        const auto ph = ladder();
        const BodyRotationResult r = trackBodyRotationTriangulated(in, /*leadIsLeft*/ true, ph, true, true);
        const auto series = buildBodyRotationSeries(r, ph);
        const MetricSeries *pm = find(series, "pelvisRotation");
        const MetricSeries *ps = find(series, "pelvisRotationSigned");
        const MetricSeries *xf = find(series, "xFactor");
        CHECK("every channel lands", pm && ps && xf && find(series, "thoraxRotation") && find(series, "xFactorStretch"));
        const auto at = [](const MetricSeries *m, Phase p) -> const PhaseSample * {
            for (const PhaseSample &s : m->phaseSamples) if (s.phase == p) return &s;
            return nullptr;
        };
        CHECK("magnitude at the top ≈ 45°, the corridors' convention", at(pm, Phase::Top) && std::fabs(at(pm, Phase::Top)->value - 45.0) < 0.5);
        CHECK("signed at the top ≈ −45° (closed, lead-relative)", at(ps, Phase::Top) && std::fabs(at(ps, Phase::Top)->value + 45.0) < 0.5);
        CHECK("X-factor at the top ≈ 45°", at(xf, Phase::Top) && std::fabs(at(xf, Phase::Top)->value - 45.0) < 0.5);
        // A left-handed golfer's same physical bearing change reads the other way round.
        const BodyRotationResult rl = trackBodyRotationTriangulated(in, /*leadIsLeft*/ false, ph, true, false);
        const auto lhSeries = buildBodyRotationSeries(rl, ph);   // held: find() points into it
        const MetricSeries *psl = find(lhSeries, "pelvisRotationSigned");
        CHECK("the sign is lead-relative: mirrored for a left-handed golfer", psl && at(psl, Phase::Top) && at(psl, Phase::Top)->value > 40.0);
        // σ: bigger at the top than at address (the camera-scale term grows with the turn), and
        // every phase reading carries one, tagged propagated.
        const PhaseSample *a0 = at(pm, Phase::Address), *a4 = at(pm, Phase::Top);
        CHECK("phase readings carry σ, propagated", a0 && a4 && a0->sigma && a4->sigma
              && a4->sigmaKind == uint8_t(SigmaKind::Propagated) && pm->sigmaKind == uint8_t(SigmaKind::Propagated));
        CHECK("σ grows with the turn (assumed camera placement)", a0 && a4 && *a4->sigma > *a0->sigma + 2.0);
        // Calibrated cameras shrink it.
        TriangulatedTurnInput cal = in; cal.camerasCalibrated = true;
        const auto calSeries = buildBodyRotationSeries(trackBodyRotationTriangulated(cal, true, ph, true, true), ph);
        const MetricSeries *pc = find(calSeries, "pelvisRotation");
        CHECK("a calibrated pair shrinks the σ at the top", pc && at(pc, Phase::Top) && *at(pc, Phase::Top)->sigma < *a4->sigma);
        // Only the asked-for segment, and no separation from half a pair.
        const auto onlyThorax = buildBodyRotationSeries(trackBodyRotationTriangulated(in, true, ph, false, true), ph);
        CHECK("an IMU-measured pelvis is not overwritten, and no separation from half a pair",
              !find(onlyThorax, "pelvisRotation") && find(onlyThorax, "thoraxRotation") && !find(onlyThorax, "xFactor"));

        // The chest's signed twin from the same route (2026-10-26): the shoulder line's signed
        // bearing, lead-relative, with its magnitude's per-sample σ.
        {
            const MetricSeries *tm = find(series, "thoraxRotation");
            const MetricSeries *ts = find(series, "thoraxRotationSigned");
            CHECK("thoraxRotationSigned lands from the two cameras", tm && ts);
            CHECK("signed thorax at the top ≈ −90° (closed, right-hander)",
                  ts && at(ts, Phase::Top) && std::fabs(at(ts, Phase::Top)->value + 90.0) < 0.5);
            CHECK("…and the magnitude beside it ≈ +90°", tm && at(tm, Phase::Top) && std::fabs(at(tm, Phase::Top)->value - 90.0) < 0.5);
            CHECK("signed thorax through the strike is open (> 0 after impact)",
                  r.thoraxSigned.turn.value.size() > 1 && r.thoraxSigned.turn.value.back() > 25.0);
            CHECK("the signed thorax carries σ, propagated, the magnitude's own",
                  ts && tm && ts->sigma && tm->sigma && *ts->sigma == *tm->sigma
                  && ts->sigmaKind == uint8_t(SigmaKind::Propagated) && at(ts, Phase::Top) && at(tm, Phase::Top)
                  && at(ts, Phase::Top)->sigma && *at(ts, Phase::Top)->sigma == *at(tm, Phase::Top)->sigma
                  && at(ts, Phase::Top)->sigmaKind == uint8_t(SigmaKind::Propagated));
            CHECK("the route tags it Triangulated", r.thoraxSigned.tier == RotationTier::Triangulated);
            const auto lhT = buildBodyRotationSeries(trackBodyRotationTriangulated(in, /*leadIsLeft*/ false, ph, false, true), ph);
            const MetricSeries *tsl = find(lhT, "thoraxRotationSigned");
            CHECK("left-handed golfer: the signed thorax is mirrored (≈ +90° at the top)",
                  tsl && at(tsl, Phase::Top) && std::fabs(at(tsl, Phase::Top)->value - 90.0) < 0.5);
            CHECK("pelvis only asked for: no signed thorax",
                  !find(buildBodyRotationSeries(trackBodyRotationTriangulated(in, true, ph, true, false), ph),
                        "thoraxRotationSigned"));

            // A thorax refusal on this route takes its signed twin down with it.
            TriangulatedTurnInput lateT = in;
            for (size_t i = 0; i < lateT.t_us.size(); ++i) if (lateT.t_us[i] < 400000) lateT.thoraxBearing[i] = std::numeric_limits<double>::quiet_NaN();
            bool refusedCovers = false;
            for (const GateRecord &g : trackBodyRotationTriangulated(lateT, true, ph, false, true).gates)
                if (g.subject == QLatin1String("thoraxRotation") && g.covers.contains(QStringLiteral("thoraxRotationSigned")))
                    refusedCovers = true;
            CHECK("a refused shoulder line covers thoraxRotationSigned", refusedCovers);

            // AN IMU-BOUND THORAX STILL WINS. The stage's own merge (BodyRotationTriangulatedStage:
            // wantThorax = no thoraxRotation yet; a key already written is skipped), replayed here
            // because analysis_pipeline.cpp is not built into the test tree.
            std::vector<double> chest;
            for (int64_t t : in.t_us) chest.push_back(t >= 800000 && t < 1000000 ? -80.0 : 0.0);
            FusedStreams fs;
            fs.timeGrid = in.t_us;
            fs.segments.push_back(yawStream(SegmentRole::Thorax, in.t_us, chest));
            std::vector<MetricSeries> merged = buildBodyRotationSeries(trackBodyRotation(PoseTrack2D{}, fs, 0, 0, true, ph), ph);
            const bool wantPelvis = !find(merged, "pelvisRotation"), wantThorax = !find(merged, "thoraxRotation");
            CHECK("IMU thorax: the stage asks the cameras for the pelvis only", wantPelvis && !wantThorax);
            for (MetricSeries &m : buildBodyRotationSeries(trackBodyRotationTriangulated(in, true, ph, wantPelvis, wantThorax), ph))
                if (!find(merged, m.key.toLatin1().constData())) merged.push_back(std::move(m));
            const MetricSeries *tsImu = find(merged, "thoraxRotationSigned");
            int nSigned = 0;
            for (const MetricSeries &m : merged) if (m.key == QLatin1String("thoraxRotationSigned")) ++nSigned;
            CHECK("IMU thorax: one thoraxRotationSigned, the IMU's (−80 at the top, no σ)",
                  nSigned == 1 && tsImu && at(tsImu, Phase::Top) && std::fabs(at(tsImu, Phase::Top)->value + 80.0) < 0.5
                  && !tsImu->sigma.has_value());
            CHECK("…while the cameras still fill the signed pelvis", find(merged, "pelvisRotationSigned") != nullptr);
        }
        // No usable frame near Address ⇒ refuse rather than reference the wrong zero.
        TriangulatedTurnInput late = in;
        for (size_t i = 0; i < late.t_us.size(); ++i) if (late.t_us[i] < 400000) late.pelvisBearing[i] = std::numeric_limits<double>::quiet_NaN();
        CHECK("no bearing near Address ⇒ no pelvis turn at all",
              !find(buildBodyRotationSeries(trackBodyRotationTriangulated(late, true, ph, true, false), ph), "pelvisRotation"));
        // An unusable stretch mid-swing is masked, never graded.
        TriangulatedTurnInput gap = in;
        for (size_t i = 0; i < gap.t_us.size(); ++i) if (gap.t_us[i] > 850000 && gap.t_us[i] < 950000) gap.pelvisBearing[i] = std::numeric_limits<double>::quiet_NaN();
        const auto gapSeries = buildBodyRotationSeries(trackBodyRotationTriangulated(gap, true, ph, true, false), ph);
        const MetricSeries *pg = find(gapSeries, "pelvisRotation");
        CHECK("a gap is masked: no Top sample from a bridged value", pg && !pg->valid.empty() && !at(pg, Phase::Top));
    }

    return g_fail == 0 ? 0 : 1;
}
