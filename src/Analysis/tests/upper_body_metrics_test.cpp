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

// Standalone test for the upper-body frontal-plane metrics
// (src/Analysis/upper_body_metrics.{h,cpp}). Synthetic tracks only — no fixture.
// Mirrors lower_body_metrics_test.cpp in structure and style.
//
// WHAT THIS TEST IS FOR: every channel here carries a SIGN, and a sign is the one thing a synthetic
// track can pin exactly and a corpus cannot. Half the cases below therefore build a pose whose
// answer is known by construction and assert the direction, not just the magnitude — and §5 runs
// the whole suite again through a MIRRORED camera with a left-handed golfer, because a convention
// that only holds for a right-hander filmed from one side is not a convention.

#include "../../Body/upper_body_metrics.h"
#include "../../Metrics/gate_catalogue.h"   // the record's sentence and primaryRecord (§15)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
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

// COCO-17 body indices.
constexpr int kLSh = 5, kRSh = 6, kLEl = 7, kREl = 8, kLWr = 9, kRWr = 10;
constexpr int kLHip = 11, kRHip = 12, kLAnk = 15, kRAnk = 16;

constexpr int kW = 1000, kH = 1000;   // square frame: normalized == px/1000, so angles are readable

struct Upper {
    QPointF lSh, rSh, lEl, rEl, lWr, rWr, lHip, rHip, lAnk, rAnk;
};

static PoseFrame2D makeUpper(int64_t t, const Upper &p, float conf = 0.9f)
{
    PoseFrame2D f;
    f.t_us = t;
    const auto set = [&](int i, QPointF v) { f.kp[i] = v; f.conf[i] = conf; };
    set(kLSh, p.lSh);   set(kRSh, p.rSh);
    set(kLEl, p.lEl);   set(kREl, p.rEl);
    set(kLWr, p.lWr);   set(kRWr, p.rWr);
    set(kLHip, p.lHip); set(kRHip, p.rHip);
    set(kLAnk, p.lAnk); set(kRAnk, p.rAnk);
    return f;
}

// A square, level address pose. LEAD = LEFT keypoints, and the lead ankle sits at the SMALLER x, so
// the lead side is image −x — the same deliberately awkward arrangement lower_body_metrics_test
// uses, because a producer that assumed lead == +x would pass a friendlier fixture.
static Upper addressPose()
{
    Upper p;
    p.lSh  = QPointF(0.42, 0.30);   p.rSh  = QPointF(0.58, 0.30);
    // 120 px apart, INSIDE the 160 px shoulders. The fixture used to splay them to 200 px, wider
    // than the shoulders, which no address has: the arms hang together at address and separate
    // through the swing, and that ordering is the whole reason the elbow line takes an absolute
    // pixel floor rather than a ratio against its address span (§11).
    p.lEl  = QPointF(0.44, 0.42);   p.rEl  = QPointF(0.56, 0.42);
    p.lWr  = QPointF(0.48, 0.52);   p.rWr  = QPointF(0.52, 0.52);
    p.lHip = QPointF(0.45, 0.55);   p.rHip = QPointF(0.55, 0.55);
    p.lAnk = QPointF(0.44, 0.90);   p.rAnk = QPointF(0.56, 0.90);
    return p;
}

// Mirror a pose about x = 0.5 and swap left/right, which is exactly what a mirrored camera filming
// a left-handed golfer produces: the SAME posture, every image coordinate flipped.
static Upper mirrored(const Upper &p)
{
    const auto mx = [](QPointF v) { return QPointF(1.0 - v.x(), v.y()); };
    Upper m;
    m.lSh  = mx(p.rSh);   m.rSh  = mx(p.lSh);
    m.lEl  = mx(p.rEl);   m.rEl  = mx(p.lEl);
    m.lWr  = mx(p.rWr);   m.rWr  = mx(p.lWr);
    m.lHip = mx(p.rHip);  m.rHip = mx(p.lHip);
    m.lAnk = mx(p.rAnk);  m.rAnk = mx(p.lAnk);
    return m;
}

static PoseTrack2D trackOf(const std::vector<Upper> &poses)
{
    PoseTrack2D t;
    int64_t us = 0;
    for (const Upper &p : poses) {
        t.frames.push_back(makeUpper(us, p));
        us += 10000;   // 100 fps
    }
    return t;
}

// A ladder carrying the four phases this module samples. THE FINISH IS ON IT DELIBERATELY: an
// unsegmented phase now emits no sample at all, where the producer used to coast to the first frame
// and hand back a first-frame reading labelled "Finish". §9 below asserts that all four samples are
// present, and it was passing on the fabricated one — the balance measures read the finish, so a
// frame-0 value under that label would have been graded as the golfer's finish position.
static std::vector<PhaseEvent> phasesAt(int64_t addrUs, int64_t topUs, int64_t impactUs)
{
    return { { Phase::Address, addrUs, 1.f, SegmentRole::Unknown },
             { Phase::Top,     topUs,  1.f, SegmentRole::Unknown },
             { Phase::Impact,  impactUs, 1.f, SegmentRole::Unknown },
             { Phase::Finish,  impactUs + 10000, 1.f, SegmentRole::Unknown } };
}

// Value of a named series at the given phase sample.
static bool seriesAt(const std::vector<MetricSeries> &all, const char *key, Phase p, double &out)
{
    for (const MetricSeries &m : all) {
        if (m.key != QLatin1String(key)) continue;
        for (const PhaseSample &s : m.phaseSamples)
            if (s.phase == p) { out = s.value; return true; }
    }
    return false;
}

static bool hasSeries(const std::vector<MetricSeries> &all, const char *key)
{
    for (const MetricSeries &m : all)
        if (m.key == QLatin1String(key)) return true;
    return false;
}

// The five channels whose PHASE DOMAIN is Address→Impact (design §5.1, and the `domain` field on
// MetricDescriptor). Past impact the body has turned, and a frontal-plane reading of a rotating
// torso is projection rather than the quantity the metric names — so those five sample P1/P4/P7 and
// NOTHING at the finish. The other four are read at the finish (the balance and swing-width
// measures), so they keep the default Address/Top/Impact/Finish.
//
// A domain violation is not a small error: an out-of-domain sample is persisted, drawn and graded
// like any other, so it is a confident reading of a quantity that does not exist at that instant —
// the same class of fault as the ±90° body-line degeneracy the gates in this file exist for.
static bool isAddressToImpactMetric(const QString &key)
{
    return key == QLatin1String("secondaryAxisTilt")
        || key == QLatin1String("spineSideBend")
        || key == QLatin1String("thoraxLateralDrift")
        || key == QLatin1String("shoulderPlaneAngle")
        || key == QLatin1String("elbowAlignment")
        || key == QLatin1String("trunkRise");   // posture_metrics_design.md §3.2: read P1..P7 only
}

// How many phase samples a fully segmented swing should give this series: three inside the narrowed
// domain, four outside it.
static size_t expectedPhaseSamples(const QString &key)
{
    return isAddressToImpactMetric(key) ? 3u : 4u;
}

static bool hasPhaseSample(const MetricSeries &m, Phase p)
{
    for (const PhaseSample &s : m.phaseSamples)
        if (s.phase == p) return true;
    return false;
}

// ── §12's fixtures: a held address with ROUND numbers, and a smoother record to go with it ──
//
// Shoulders 200 px apart and level, hips 100 px, elbows 120 px, ankles 200 px, all on a 1000 px
// frame, so every σ §12 asserts is a closed form a reader can check by hand. Deliberately NOT
// `addressPose()`: that fixture's spans are 160 / 100 / 120 and the arithmetic below would stop being
// legible, and a σ test whose expected value is illegible has been checked by nobody.
//
// Lead is LEFT and the lead ankle sits at the SMALLER x, the same awkward arrangement the rest of the
// file uses — σ is a magnitude and must not care, which the sign-flipped fixture is what proves.
static Upper sigmaUpperPose()
{
    Upper p;
    p.lSh  = QPointF(0.40, 0.30);   p.rSh  = QPointF(0.60, 0.30);   // shoulder line 200 px, level
    p.lEl  = QPointF(0.44, 0.48);   p.rEl  = QPointF(0.56, 0.48);   // elbow line 120 px (> 25 px floor)
    p.lWr  = QPointF(0.48, 0.58);   p.rWr  = QPointF(0.52, 0.58);
    p.lHip = QPointF(0.45, 0.55);   p.rHip = QPointF(0.55, 0.55);   // hip line 100 px, level
    p.lAnk = QPointF(0.40, 0.90);   p.rAnk = QPointF(0.60, 0.90);   // stance 200 px, level
    return p;
}

// The same address with the TRAIL SHOULDER 60 px LOWER — 16.7° of shoulder tilt on a 200 px line.
//
// §12a's fixture is level everywhere, which leaves two things untested: `lineTiltSigmaDeg` divides by
// the EUCLIDEAN length (identical to |Δx| on a level line), and `heightAboveLineSigmaPx` carries a
// sqrt(1 + s²) factor that is exactly 1 when the line's slope s is 0. Both mutations pass every level
// case.
//
// ⚠ THE DROP IS 60 px, NOT THE 30 px THE HIP LINE TAKES, and the difference is deliberate. This line is
// 200 px long where the hip line is 100, so 30 px would be 8.5° here, leaving sqrt(1 + s²) a 1.1 %
// effect and the test barely able to see it. 60 px puts both fixtures at the SAME 16.7° and both
// discriminators at the same 4.4 %, which is a fixture chosen for what it can detect rather than for a
// round number.
static Upper sigmaUpperPoseTilted()
{
    Upper p = sigmaUpperPose();
    p.rSh = QPointF(0.60, 0.36);       // trail shoulder (lead is LEFT here) 60 px lower
    return p;
}

// The smoother's per-keypoint honesty record for one frame, with a CONSTANT σ on every joint. Tier
// Meas is what a smoothed keypoint inside a confirmed run carries; the producer reads sigma > 0 and
// never `tier`, deliberately — `tier` is the overlay's paint policy, and a Pred keypoint has a
// perfectly good posterior σ that it would be wrong to throw away.
static PoseKpAux auxWith(double sigmaPx)
{
    PoseKpAux a;
    for (int k = 0; k < kWholeBodyJoints; ++k) {
        a.tier[size_t(k)]  = uint8_t(PoseTier::Meas);
        a.sigma[size_t(k)] = float(sigmaPx);
    }
    return a;
}

// A track whose SMOOTHED companion is byte-identical to its raw frames, so every emitted value is the
// same number with and without `smoothedAux` — which is what lets §12e assert that adding σ moves
// nothing. `sigmas` is one entry per frame; EMPTY means no aux at all (a pre-smoother swing).
static PoseTrack2D sigmaTrackOf(const std::vector<Upper> &poses, const std::vector<double> &sigmas)
{
    PoseTrack2D t;
    int64_t us = 0;
    for (const Upper &p : poses) {
        const PoseFrame2D f = makeUpper(us, p);
        t.frames.push_back(f);
        t.smoothed.push_back(f);
        us += 10000;                                  // 100 fps, as trackOf uses
    }
    for (double sg : sigmas)
        t.smoothedAux.push_back(auxWith(sg));
    return t;
}

// A per-keypoint σ record with a DISTINCT value on every joint the module reads.
//
// §12a gives every joint the same σ, which pins no asymmetric COEFFICIENT: swap sigA and sigB in
// trailElbowHeight, or the lead and trail shoulder in leadUpperArmToChest, or the two ankles in
// thoraxLateralDrift, and a constant σ hides all three. σ is attached to the PHYSICAL keypoint here, so
// running the same fixture with `leadIsLeft` both ways makes the two runs each other's control.
static PoseKpAux auxPerJoint(double lSh, double rSh, double lEl, double rEl,
                             double lWr, double rWr, double lHip, double rHip,
                             double lAnk, double rAnk)
{
    PoseKpAux a;
    for (int k = 0; k < kWholeBodyJoints; ++k)
        a.tier[size_t(k)] = uint8_t(PoseTier::Meas);
    a.sigma[size_t(kLSh)]  = float(lSh);   a.sigma[size_t(kRSh)]  = float(rSh);
    a.sigma[size_t(kLEl)]  = float(lEl);   a.sigma[size_t(kREl)]  = float(rEl);
    a.sigma[size_t(kLWr)]  = float(lWr);   a.sigma[size_t(kRWr)]  = float(rWr);
    a.sigma[size_t(kLHip)] = float(lHip);  a.sigma[size_t(kRHip)] = float(rHip);
    a.sigma[size_t(kLAnk)] = float(lAnk);  a.sigma[size_t(kRAnk)] = float(rAnk);
    return a;
}

// A held track carrying one prebuilt aux record on every frame.
static PoseTrack2D trackWithAux(const std::vector<Upper> &poses, const PoseKpAux &aux)
{
    PoseTrack2D t;
    int64_t us = 0;
    for (const Upper &p : poses) {
        const PoseFrame2D f = makeUpper(us, p);
        t.frames.push_back(f);
        t.smoothed.push_back(f);
        t.smoothedAux.push_back(aux);
        us += 10000;
    }
    return t;
}

static const MetricSeries *findSeriesU(const std::vector<MetricSeries> &v, const char *key)
{
    for (const MetricSeries &m : v)
        if (m.key == QLatin1String(key)) return &m;
    return nullptr;
}

// Relative agreement. C11 asks for 5 %; every case below passes at 1e-5 because BOTH sides are closed
// forms — the expected value is written out from the fixture's own geometry, never read back out of
// the producer.
static bool nearRel(double got, double want, double rel)
{
    return std::fabs(got - want) <= rel * std::fabs(want);
}

// Run the producer over a two-pose track (address held, then the test pose at the Top) and return
// the emitted series.
static std::vector<MetricSeries> runOn(const Upper &addr, const Upper &top, bool leadIsLeft = true)
{
    std::vector<Upper> poses;
    for (int i = 0; i < 6; ++i) poses.push_back(addr);    // a held address for the robust reference
    for (int i = 0; i < 6; ++i) poses.push_back(top);
    const PoseTrack2D track = trackOf(poses);
    const UpperBodyResult r = trackUpperBody(track, kW, kH, leadIsLeft, 20000);
    return buildUpperBodySeries(r, phasesAt(20000, 90000, 110000));
}

int main()
{
    std::printf("=== upper body metrics ===\n");

    // ── 1. Address: a square, level pose reads ~zero on every signed channel ────────────────────
    {
        const Upper a = addressPose();
        const auto series = runOn(a, a);
        CHECK("all eleven series produced", series.size() == 11);   // + leadArmFlexion (face-on, D5)

        double v = 0.0;
        CHECK("secondaryAxisTilt present", seriesAt(series, "secondaryAxisTilt", Phase::Address, v));
        CHECK("square address ⇒ axis tilt ~0", near(v, 0.0, 0.5));
        CHECK("shoulderPlaneAngle present", seriesAt(series, "shoulderPlaneAngle", Phase::Address, v));
        CHECK("level shoulders ⇒ plane ~0", near(v, 0.0, 0.5));
        CHECK("spineSideBend present", seriesAt(series, "spineSideBend", Phase::Address, v));
        CHECK("level lines ⇒ side bend ~0", near(v, 0.0, 0.5));
        CHECK("elbowAlignment present", seriesAt(series, "elbowAlignment", Phase::Address, v));
        CHECK("level elbows ⇒ elbow line ~0", near(v, 0.0, 0.5));
        CHECK("trailElbowHeight present", seriesAt(series, "trailElbowHeight", Phase::Address, v));
        // The elbows sit 0.12 BELOW the shoulder line; the span is 0.16. −0.12/0.16 = −75 %.
        CHECK("elbow below the shoulder line reads negative", near(v, -75.0, 1.0));
    }

    // ── 2. secondaryAxisTilt is TRAIL-positive ─────────────────────────────────────────────────
    // The one lateral channel that is not lead-positive. Lead is image −x here, so leaning the
    // spine AWAY from the target moves the neck toward +x, and the answer must be POSITIVE.
    {
        const Upper a = addressPose();
        Upper t = a;
        t.lSh = QPointF(0.47, 0.30);   t.rSh = QPointF(0.63, 0.30);   // whole shoulder girdle → +x
        const auto series = runOn(a, t);

        double v = 0.0;
        CHECK("axis tilt at the top", seriesAt(series, "secondaryAxisTilt", Phase::Top, v));
        CHECK("spine leaning away from the target is POSITIVE", v > 3.0);
    }

    // ── 3. spineSideBend is the shoulder line AGAINST the hip line ─────────────────────────────
    // Tilting BOTH lines together is a whole-body lean, not side bend, and must read ~zero. Tilting
    // only the shoulders is side bend and must not.
    {
        const Upper a = addressPose();

        // Both lines tilted by the same ANGLE, which is not the same as the same rise: the
        // shoulders span 0.16 and the hips 0.10, so equal drops would be 14° and 22° and the
        // difference — the thing being measured — would not be zero. Equal ratios, equal angles.
        Upper both = a;
        both.lSh  = QPointF(0.42, 0.28);  both.rSh  = QPointF(0.58, 0.32);   // dy 0.04 / 0.16
        both.lHip = QPointF(0.45, 0.5375); both.rHip = QPointF(0.55, 0.5625); // dy 0.025 / 0.10
        double v = 0.0;
        CHECK("side bend at the top (parallel lines)",
              seriesAt(runOn(a, both), "spineSideBend", Phase::Top, v));
        CHECK("a whole-body lean is NOT side bend", near(v, 0.0, 0.6));

        Upper shoulders = a;                             // trail shoulder dropped, hips level
        shoulders.lSh = QPointF(0.42, 0.28);  shoulders.rSh = QPointF(0.58, 0.34);
        CHECK("side bend at the top (shoulders only)",
              seriesAt(runOn(a, shoulders), "spineSideBend", Phase::Top, v));
        // Trail (right, +x) shoulder LOWER ⇒ shoulder tilt negative ⇒ hip − shoulder positive.
        CHECK("trail shoulder dropping under the turn is POSITIVE side bend", v > 5.0);
    }

    // ── 4. thoraxLateralDrift is measured from the TRAIL ankle, lead-positive ──────────────────
    // NOT address-referenced: the measure over it is a Delta, so the series must carry the absolute
    // distance and let the reducer do the referencing. Address is checked non-zero for that reason.
    {
        const Upper a = addressPose();
        Upper t = a;
        t.lSh = QPointF(0.40, 0.30);   t.rSh = QPointF(0.56, 0.30);   // chest toward the LEAD side (−x)
        t.lHip = QPointF(0.45, 0.55);  t.rHip = QPointF(0.55, 0.55);
        const auto series = runOn(a, t);

        double addr = 0.0, top = 0.0;
        CHECK("thoraxLateralDrift at address", seriesAt(series, "thoraxLateralDrift", Phase::Address, addr));
        CHECK("thoraxLateralDrift at the top", seriesAt(series, "thoraxLateralDrift", Phase::Top, top));
        CHECK("absolute, not address-referenced (address is not zero)", std::fabs(addr) > 10.0);
        CHECK("chest moving toward the lead side INCREASES it", top > addr);
    }

    // ── 5. Every sign survives a mirrored camera and a left-handed golfer ──────────────────────
    // The whole point of the absolute-denominator line form and of resolving lead-ness from the
    // address geometry. Same posture, flipped image, opposite handedness — same numbers.
    {
        const Upper a = addressPose();
        Upper t = a;
        t.lSh = QPointF(0.47, 0.30);   t.rSh = QPointF(0.63, 0.30);   // lean away from the target

        const auto plain    = runOn(a, t, true);
        const auto flipped  = runOn(mirrored(a), mirrored(t), false);

        for (const char *key : { "secondaryAxisTilt", "spineSideBend", "shoulderPlaneAngle",
                                 "elbowAlignment", "trailElbowHeight", "leadHandWidth",
                                 "leadUpperArmToChest", "leadArmToTorso", "thoraxLateralDrift",
                                 "trunkRise" }) {
            double p = 0.0, f = 0.0;
            const bool okP = seriesAt(plain, key, Phase::Top, p);
            const bool okF = seriesAt(flipped, key, Phase::Top, f);
            std::printf("    %-20s plain %8.3f   mirrored %8.3f\n", key, p, f);
            CHECK(key, okP && okF && near(p, f, 0.5));
        }
    }

    // ── 6. leadArmToTorso: zero when the arm hangs along the torso, larger as it leaves ────────
    {
        const Upper a = addressPose();
        Upper hang = a;
        // Lead arm straight down the torso line: shoulder→elbow parallel to neck→pelvis (the neck at
        // x 0.50, the pelvis at x 0.50 — vertical), the upper arm its address length (0.12 vs 0.122).
        // Until 13 Nov 2026 this case collapsed the shoulders to one point, pinning that the angle did
        // not depend on the shoulder line. It does now: its torso starts at the shoulder midpoint,
        // which is guesswork once the shoulders overlap — §14 pins that refusal.
        hang.lEl = QPointF(0.42, 0.42);
        double v = 0.0;
        const auto series = runOn(a, hang);
        CHECK("leadArmToTorso at the top", seriesAt(series, "leadArmToTorso", Phase::Top, v));
        CHECK("arm hanging along the torso reads ~0", near(v, 0.0, 2.0));

        Upper out = a;
        out.lEl = QPointF(0.28, 0.30);    // arm straight out to the lead side
        CHECK("leadArmToTorso, arm out", seriesAt(runOn(a, out), "leadArmToTorso", Phase::Top, v));
        CHECK("an arm away from the torso reads much larger", v > 60.0);
    }

    // ── 7. Refusals: no fabricated numbers ─────────────────────────────────────────────────────
    {
        // A shoulder span under the floor is a denominator that cannot carry a percentage.
        Upper narrow = addressPose();
        narrow.lSh = QPointF(0.499, 0.30);   narrow.rSh = QPointF(0.501, 0.30);   // 2 px apart
        const auto tiny = runOn(narrow, narrow);
        CHECK("a sub-floor shoulder span produces NOTHING", tiny.empty());

        // Below the confidence gate every point is unresolved, so there is no reference at all.
        PoseTrack2D dark = trackOf({ addressPose(), addressPose(), addressPose() });
        for (PoseFrame2D &f : dark.frames)
            f.conf.fill(0.05f);
        const UpperBodyResult r = trackUpperBody(dark, kW, kH, true, 0);
        CHECK("an all-low-confidence track produces NOTHING",
              buildUpperBodySeries(r, phasesAt(0, 10000, 20000)).empty());

        // Zero frame dimensions cannot de-normalize anything.
        const UpperBodyResult z = trackUpperBody(trackOf({ addressPose() }), 0, 0, true, 0);
        CHECK("zero frame dims refuse", !z.valid);

        // An empty track is not an error, it is an absence.
        const UpperBodyResult e = trackUpperBody(PoseTrack2D{}, kW, kH, true, 0);
        CHECK("an empty track refuses", !e.valid);
    }

    // ── 8. The smoothed companion track wins when it exists ────────────────────────────────────
    {
        PoseTrack2D t = trackOf(std::vector<Upper>(8, addressPose()));
        Upper tilted = addressPose();
        tilted.lSh = QPointF(0.42, 0.26);   tilted.rSh = QPointF(0.58, 0.34);
        t.smoothed = trackOf(std::vector<Upper>(8, tilted)).frames;

        const UpperBodyResult r = trackUpperBody(t, kW, kH, true, 0);
        const auto series = buildUpperBodySeries(r, phasesAt(0, 40000, 70000));
        double v = 0.0;
        CHECK("shoulderPlaneAngle from the smoothed track",
              seriesAt(series, "shoulderPlaneAngle", Phase::Address, v));
        CHECK("the SMOOTHED pose is what was measured", std::fabs(v) > 20.0);
    }

    // ── 9. Each series samples its DOMAIN's phases, and no others ──────────────────────────────
    {
        const Upper a = addressPose();
        const auto series = runOn(a, a);
        bool perDomain = !series.empty();
        for (const MetricSeries &m : series)
            perDomain = perDomain && m.phaseSamples.size() == expectedPhaseSamples(m.key);
        CHECK("Address/Top/Impact inside the narrowed domain, plus Finish outside it", perDomain);

        // THE DOMAIN RULE, asserted directly rather than inferred from a count. The five
        // Address→Impact channels must never carry a Finish reading: past impact the torso has
        // turned and a frontal-plane angle or lateral distance is measuring the rotation, not the
        // posture — a number that is wrong in a way no corridor can catch, because it looks exactly
        // like a real one. Every one of the nine used to sample the finish, so this had been in every
        // swing.json.
        bool noFinishInDomain = true;
        for (const MetricSeries &m : series)
            if (isAddressToImpactMetric(m.key))
                noFinishInDomain = noFinishInDomain && !hasPhaseSample(m, Phase::Finish);
        CHECK("NO Address→Impact channel emits a Finish sample", noFinishInDomain);

        // …and the four that are genuinely read there still do. The balance and swing-width measures
        // have no other instant to read, so narrowing them too would delete the metric.
        bool finishKept = true;
        for (const MetricSeries &m : series)
            if (!isAddressToImpactMetric(m.key))
                finishKept = finishKept && hasPhaseSample(m, Phase::Finish);
        CHECK("…while the four read at the finish keep it", finishKept);

        // …and when the ladder has NO finish, the sample is absent rather than taken at frame 0.
        // The balance measures read this phase, so a fabricated value here would be graded as the
        // golfer's finish position on any swing the segmenter failed to close out. It is the four
        // finish-samplers that carry this case now — the other five never ask for the phase — so
        // three samples each is the answer for every series either way.
        {
            const std::vector<PhaseEvent> noFinish{
                { Phase::Address, 20000,  1.f, SegmentRole::Unknown },
                { Phase::Top,     90000,  1.f, SegmentRole::Unknown },
                { Phase::Impact,  110000, 1.f, SegmentRole::Unknown } };
            std::vector<Upper> poses;
            for (int i = 0; i < 6; ++i) poses.push_back(a);
            for (int i = 0; i < 6; ++i) poses.push_back(a);
            const UpperBodyResult r = trackUpperBody(trackOf(poses), kW, kH, true, 20000);
            const auto partial = buildUpperBodySeries(r, noFinish);
            bool threeOnly = !partial.empty();
            for (const MetricSeries &m : partial)
                threeOnly = threeOnly && m.phaseSamples.size() == 3;
            CHECK("an unsegmented finish emits NO sample, not a frame-0 one", threeOnly);
        }
        CHECK("no stray keys", hasSeries(series, "leadHandWidth")
                                   && !hasSeries(series, "hipAlignment")
                                   && !hasSeries(series, "shoulderAlignment"));
    }

    // ── 10. The body lines' foreshortening gate ────────────────────────────────────────────────
    //
    // `lineTiltDeg` divides by the line's horizontal separation, so a golfer turning out of the
    // image plane collapses two joints into the same image column and the angle runs to ±90° while
    // the posture it claims to describe has not changed. On the swing that prompted this work
    // `shoulderPlaneAngle` reads +88° AT THE TOP — a GRADED phase sample. Below
    // upperBody.minShoulderSpanRatio the frame has NO shoulder line and is absent from the channel;
    // the resample still bridges it so the curve stays continuous, and MetricSeries::valid says
    // which of that curve is a measurement.
    //
    // The track rotates about the vertical from square to 80° over 20 frames, so |dx| / address |dx|
    // is exactly cos θ and the 0.40 gate bites at 66.4° — frames 16..19 of the turn, grid 22..25.
    {
        const double kPi = 3.14159265358979323846;
        const int kAddr = 6, kTurn = 20;

        const auto find = [](const std::vector<MetricSeries> &all,
                             const char *key) -> const MetricSeries * {
            for (const MetricSeries &m : all)
                if (m.key == QLatin1String(key)) return &m;
            return nullptr;
        };
        // The gated tail is indices 22..25, and for a NARROWED channel the phase domain marks
        // everything past the Impact sample (index 24) as well — index 25, which the gate has already
        // zeroed, so the expectation is the same either way. The domain's HEAD is open, so nothing at
        // the front is marked (applyPhaseDomainMask says why; §13 tests it on its own).
        const auto maskedTail = [](const MetricSeries *m) {
            if (m == nullptr || m->valid.size() != m->t_us.size()) return false;
            for (size_t i = 0; i < m->valid.size(); ++i)
                if (m->valid[i] != (i >= 22 ? 0u : 1u)) return false;
            return true;
        };
        // "Nothing was gated on this channel": EMPTY for a whole-swing metric, and for a narrowed one
        // exactly the phase domain's tail — the single zero at index 25, past Impact on index 24.
        const auto domainOnly = [](const MetricSeries *m) {
            if (m == nullptr) return false;
            if (!isAddressToImpactMetric(m->key)) return m->valid.empty();
            if (m->valid.size() != m->t_us.size()) return false;
            for (size_t i = 0; i < m->valid.size(); ++i)
                if (m->valid[i] != (i > 24 ? 0u : 1u)) return false;
            return true;
        };

        // Address at 20 ms; Top on a frame the gate PASSES (200 ms, θ = 58.9°); Impact and Finish
        // inside the run it REFUSES (240 / 250 ms). One sample must survive and two must not exist.
        const std::vector<PhaseEvent> phases{
            { Phase::Address, 20000,  1.f, SegmentRole::Unknown },
            { Phase::Top,     200000, 1.f, SegmentRole::Unknown },
            { Phase::Impact,  240000, 1.f, SegmentRole::Unknown },
            { Phase::Finish,  250000, 1.f, SegmentRole::Unknown } };

        // The address window must cover the HELD ADDRESS ONLY: with the shipped 250 ms the reference
        // median would be taken over the turn as well, and the ratio's denominator would be the very
        // foreshortening it is supposed to detect.
        UpperBodyConfig cfg; cfg.addrWindowUs = 30000;

        const auto rotate = [&](bool shoulders, bool hips) {
            std::vector<Upper> poses;
            for (int k = 0; k < kAddr; ++k) poses.push_back(addressPose());
            for (int k = 0; k < kTurn; ++k) {
                const double th   = 80.0 * k / double(kTurn - 1);
                const double c    = std::cos(th * kPi / 180.0);
                const double rise = 0.020 * k / double(kTurn - 1);
                Upper p = addressPose();
                if (shoulders) {
                    p.lSh = QPointF(0.50 - 0.08 * c, 0.30);
                    p.rSh = QPointF(0.50 + 0.08 * c, 0.30 - rise);
                }
                if (hips) {
                    p.lHip = QPointF(0.50 - 0.05 * c, 0.55);
                    p.rHip = QPointF(0.50 + 0.05 * c, 0.55 - rise);
                }
                poses.push_back(p);
            }
            const UpperBodyResult r = trackUpperBody(trackOf(poses), kW, kH, true, 20000, cfg);
            return buildUpperBodySeries(r, phases);
        };

        // (a) The shoulders turn; the hips stay square.
        {
            const auto series = rotate(true, false);
            const MetricSeries *sp = find(series, "shoulderPlaneAngle");
            CHECK("shoulderPlaneAngle still emitted — a gated run is not a refused metric",
                  sp != nullptr);
            CHECK("its mask is 0 across the foreshortened run and 1 elsewhere", maskedTail(sp));
            if (sp) {
                double worst = 0.0;
                for (double v : sp->value) worst = std::max(worst, std::fabs(v));
                // Ungated, the last frame reads atan2(20 px, 27.8 px) ≈ 36° and is heading for 90°.
                CHECK("nothing in the series approaches ±90°", worst < 25.0);
                // TWO different absences, and they must not be confused. This channel's domain is
                // Address→Impact, so it never asks for the Finish at all; of the three it does ask
                // for, the Impact instant sits inside the gated run and is refused. What is left is
                // Address and Top.
                bool addrAndTopOnly = sp->phaseSamples.size() == 2;
                for (const PhaseSample &s : sp->phaseSamples)
                    addrAndTopOnly = addrAndTopOnly
                                     && (s.phase == Phase::Address || s.phase == Phase::Top);
                CHECK("A PHASE INSTANT INSIDE THE INVALID RUN EMITS NO SAMPLE (Impact)",
                      addrAndTopOnly);
                CHECK("…and the out-of-domain Finish was never asked for in the first place",
                      !hasPhaseSample(*sp, Phase::Finish));
            }

            // spineSideBend is a DIFFERENCE of two tilts, so a degenerate shoulder line poisons it
            // even though the hip line is perfect.
            CHECK("spineSideBend goes with the shoulder line", maskedTail(find(series, "spineSideBend")));

            // trailElbowHeight is a HEIGHT, not a tilt, and is gated all the same: heightAboveLine
            // interpolates the shoulder line's y at the elbow's x, so it divides by the very dx that
            // is collapsing. It is the worse case of the two — an angle saturates at 90°, an
            // unbounded % shoulder width does not — so it carries shoulderPlaneAngle's mask exactly.
            CHECK("trailElbowHeight is masked on the same run as the shoulder line it divides by",
                  maskedTail(find(series, "trailElbowHeight")));
            // leadArmToTorso is masked on it too (13 Nov 2026): its torso direction starts at the
            // shoulder MIDPOINT, which is guesswork once the shoulders overlap in the picture.
            CHECK("leadArmToTorso is masked on the shoulder line's run (its torso starts at the shoulder midpoint)",
                  maskedTail(find(series, "leadArmToTorso")));

            // The elbows never turned, so their line was never gated.
            const MetricSeries *el = find(series, "elbowAlignment");
            CHECK("elbowAlignment is untouched — a different line, its own span",
                  domainOnly(el) && el->phaseSamples.size() == expectedPhaseSamples(el->key));

            // The audited un-gated set: their divisors are address constants or Euclidean lengths,
            // which do not vanish as the body turns, so withholding them would withhold a real
            // measurement. secondaryAxisTilt divides by the VERTICAL neck→pelvis rise, and the neck
            // is the shoulders' MIDPOINT — which is exactly as well located when they foreshorten.
            for (const char *key : { "secondaryAxisTilt", "thoraxLateralDrift", "leadHandWidth",
                                     "leadUpperArmToChest" }) {
                const MetricSeries *m = find(series, key);
                // Sample count per the metric's DOMAIN — three for the Address→Impact pair at the
                // front of this list, four for the three that are read at the finish.
                CHECK(key, m && domainOnly(m)
                             && m->phaseSamples.size() == expectedPhaseSamples(m->key));
            }
        }

        // (b) The hips turn; the shoulders stay square. The other half of "BOTH lines valid".
        {
            const auto series = rotate(false, true);
            CHECK("spineSideBend goes with the HIP line too", maskedTail(find(series, "spineSideBend")));
            const MetricSeries *sp = find(series, "shoulderPlaneAngle");
            CHECK("…while the shoulder line, which never turned, carries no gate zeros",
                  domainOnly(sp) && sp->phaseSamples.size() == expectedPhaseSamples(sp->key));
        }

        // (c) The gate that never fires changes NOTHING. This is the promise the corpus gate is
        // judged on: a swing with no gated frame serialises exactly as it did before
        // MetricSeries::valid existed. EMPTY means all valid — never an all-ones array.
        {
            const auto series = rotate(false, false);
            bool untouched = series.size() == 11;
            for (const MetricSeries &m : series)
                untouched = untouched && domainOnly(&m)
                            && m.phaseSamples.size() == expectedPhaseSamples(m.key);
            // ⚠ "EMPTY" is now the promise for a WHOLE-SWING channel only. A narrowed one carries
            // its post-Impact zeros even on a swing where no frame was ever gated, which is a real
            // content change and is what the phase 2 corpus gate accounts for — the four channels
            // that predate this work are among them.
            CHECK("no gated frame ⇒ no gate zeros anywhere, and every phase sample its domain allows",
                  untouched);
        }
    }

    // ── 11. The elbow line's ABSOLUTE floor, and a gated run mid-track ─────────────────────────
    //
    // Why this gate is not a ratio like the other two: the elbows are at their NARROWEST at address
    // (the arms hang together and separate through the swing), so |dx| / address |dx| is 1.0 at
    // address by construction and ≥1 after it. A ratio could never fire — least of all at address,
    // which is exactly where `elbowAlignment` is read and where a 20 px separation is pure keypoint
    // noise. Only an absolute floor can refuse the frame the metric is graded on.
    {
        const auto find = [](const std::vector<MetricSeries> &all,
                             const char *key) -> const MetricSeries * {
            for (const MetricSeries &m : all)
                if (m.key == QLatin1String(key)) return &m;
            return nullptr;
        };
        // 15 px apart on a 1000 px frame — under the 25 px floor.
        const auto narrowElbows = [] {
            Upper p = addressPose();
            p.lEl = QPointF(0.4925, 0.42);   p.rEl = QPointF(0.5075, 0.42);
            return p;
        };

        // (a) Narrow throughout ⇒ the metric does not exist. Not a 0°, not a noisy angle: absent.
        {
            std::vector<Upper> poses(12, narrowElbows());
            const UpperBodyResult r = trackUpperBody(trackOf(poses), kW, kH, true, 20000);
            const auto series = buildUpperBodySeries(r, phasesAt(20000, 60000, 100000));
            CHECK("a 15 px elbow separation produces NO elbowAlignment at all",
                  !hasSeries(series, "elbowAlignment"));
            CHECK("…and costs nothing else — the shoulder line is untouched",
                  hasSeries(series, "shoulderPlaneAngle") && series.size() == 10);
        }

        // (b) GATED is not the same as MISSING, mid-track. §10's runs sit at the tail, where the
        // extent rule marks them on its own; a run INSIDE the track can only be marked by the mask's
        // own reasoning, and the first version of that reasoning got it wrong — on a real swing a
        // 23-frame gated shoulder run came back with 30 of its frames flagged VALID because each sat
        // within 60 ms of a measurement, and a P4 sample of 25.8° was emitted from the bridge.
        //
        // Two holes, the same length (10 frames) at the same spacing (7 ms ≈ 150 fps), OPPOSITE
        // answers:
        //   frames 10–19  elbows CONFIDENT, 15 px apart ⇒ the line is refused ⇒ GATED ⇒ 0.
        //   frames 30–39  every keypoint below the confidence gate ⇒ MISSING ⇒ bridged, valid.
        {
            const int64_t dt7 = 7000;
            PoseTrack2D t;
            for (int k = 0; k < 50; ++k) {
                const bool gated = (k >= 10 && k <= 19);
                const bool dark  = (k >= 30 && k <= 39);
                t.frames.push_back(makeUpper(k * dt7, gated ? narrowElbows() : addressPose(),
                                             dark ? 0.05f : 0.9f));
            }
            UpperBodyConfig cfg; cfg.addrWindowUs = 30000;   // the reference is the opening frames
            const std::vector<PhaseEvent> phases{
                { Phase::Address, 2 * dt7,  1.f, SegmentRole::Unknown },
                { Phase::Top,     15 * dt7, 1.f, SegmentRole::Unknown },   // inside the GATED run
                { Phase::Impact,  35 * dt7, 1.f, SegmentRole::Unknown },   // inside the CONF hole
                { Phase::Finish,  49 * dt7, 1.f, SegmentRole::Unknown } };
            const UpperBodyResult r = trackUpperBody(t, kW, kH, true, 2 * dt7, cfg);
            CHECK("the ten refused frames are recorded as GATED, not merely absent",
                  r.gatedElbowLine.size() == 10 && r.gatedElbowLine.front() == 10 * dt7
                      && r.gatedElbowLine.back() == 19 * dt7);

            const auto series = buildUpperBodySeries(r, phases);
            const MetricSeries *el = find(series, "elbowAlignment");
            CHECK("elbowAlignment emitted", el != nullptr);
            if (el) {
                // elbowAlignment is an Address→Impact channel: the phase domain marks everything
                // past the Impact sample (index 35), the gate accounts for 10..19, and the domain's
                // head is open so 0 and 1 are measurements.
                bool exact = el->valid.size() == 50;
                for (size_t i = 0; i < el->valid.size(); ++i) {
                    const uint8_t want = (i > 35 || (i >= 10 && i <= 19)) ? 0u : 1u;
                    exact = exact && (el->valid[i] == want);
                }
                CHECK("ALL TEN gated frames are 0 — no budget excuses refused geometry", exact);
                CHECK("the same-length CONFIDENCE hole stays valid inside the domain, bridged as it "
                      "always was (36..39 are 0 for the DOMAIN's tail, not for the hole)",
                      el->valid.size() == 50 && el->valid[30] == 1u && el->valid[34] == 1u
                          && el->valid[35] == 1u);
                CHECK("the tail rule is INCLUSIVE at Impact and the head is open, so nothing here "
                      "came from the extent rule",
                      el->valid.size() == 50 && el->valid.front() == 1u && el->valid[9] == 1u
                          && el->valid[20] == 1u && el->valid[35] == 1u);
                bool top = false, impact = false;
                for (const PhaseSample &s : el->phaseSamples) {
                    if (s.phase == Phase::Top)    top = true;
                    if (s.phase == Phase::Impact) impact = true;
                }
                CHECK("a phase instant inside the GATED run emits nothing", !top);
                CHECK("…while one inside the confidence hole still does", impact);
            }

            // The shoulder line was never refused on this track, and its only hole is the confidence
            // one, inside the budget. Nothing gated, nothing over budget, so: no mask at all.
            const MetricSeries *sp = find(series, "shoulderPlaneAngle");
            bool spInDomain = sp && sp->valid.size() == 50 && sp->valid[36] == 0u;
            if (sp)
                for (size_t i = 0; i <= 35; ++i)
                    spInDomain = spInDomain && sp->valid[i] == 1u;
            CHECK("a channel with nothing gated and no over-budget hole has no 0 in its domain",
                  spInDomain);
        }
    }

    // ── 12. One ratio, TWO references: the whole-swing-absent path ─────────────────────────────
    //
    // `LowerBodyState::hipLineValid` is NOT how this module learns about the hip line — the two
    // modules are separate analysis stages with no shared result, so each resolves the line against
    // its own address reference. This module's reference admission test asks for shoulders, ankles
    // and the lead arm, NOT the hips, so an address whose hips were unconfident leaves it with no hip
    // denominator at all — and then `spineSideBend` is absent for the WHOLE swing while the
    // lower-body module, whose admission test does require the hips, still produces `hipLineTilt`.
    //
    // That is a real asymmetry and it is pinned here so it is documented behaviour rather than a
    // silent one. A ratio with no denominator is not a measurement, and inventing one from the
    // mid-swing frames would put the gate's denominator inside the collapse it is meant to detect.
    {
        std::vector<Upper> poses(12, addressPose());
        PoseTrack2D t = trackOf(poses);
        for (size_t i = 0; i < 6; ++i) {                    // the address block only
            t.frames[i].conf[kLHip] = 0.05f;
            t.frames[i].conf[kRHip] = 0.05f;
        }
        UpperBodyConfig cfg; cfg.addrWindowUs = 30000;      // so the reference IS that block
        const UpperBodyResult r = trackUpperBody(t, kW, kH, true, 20000, cfg);
        CHECK("the reference still resolves without the hips", r.ref.valid);
        CHECK("…but it has no hip denominator", r.ref.hipDxPx == 0.0);
        const auto series = buildUpperBodySeries(r, phasesAt(20000, 60000, 100000));
        CHECK("spineSideBend is absent for the whole swing, not gated frame by frame",
              !hasSeries(series, "spineSideBend"));
        CHECK("…while the shoulder line, which HAS its reference, is produced",
              hasSeries(series, "shoulderPlaneAngle"));
    }

    // ── 13. THE PHASE DOMAIN: outside it, the sample is not a measurement ──────────────────────
    //
    // Design §5.1's table narrows five of these nine to ADDRESS→IMPACT: secondaryAxisTilt,
    // spineSideBend, thoraxLateralDrift, shoulderPlaneAngle and elbowAlignment. Past impact the
    // thorax has turned toward the target, so the frontal projection of a lateral quantity or a line
    // angle is measuring rotation, not the quantity. The producers already refused to SAMPLE those
    // channels at the Finish (kP1toP7Samples, §3); this is the other half of the same statement —
    // every grid sample PAST IMPACT is marked invalid, so no reducer can read one either.
    //
    // ⚠ THE TAIL ONLY: the pre-Address head stays valid, because the chart does not clip the start
    // side (a domain whose first phase is Address is the DEFAULT first, so the card's window still
    // starts at the series' first sample, and marking the head made every clamped card `partial`) and
    // because a still golfer referenced to address is a real reading of address posture — the
    // still-address gate window is measured on exactly those samples.
    //
    // ⚠ It cannot be done in the reducers: series_reduce.h's extremum deliberately does not clip its
    // support to the query, because the diagnostics span cache and the whole-window card agree only
    // while a sample's windowed mean is query-independent (W2: 20 disagreements in 514 measures with
    // the support clipped, 0 without). Marking the samples closes the leak once, for every consumer.
    {
        // Nothing gated anywhere — a held address — so the only zeros there can be are the domain's,
        // over a grid that runs past impact, which is every real swing (the window is padded).
        std::vector<Upper> poses(12, addressPose());        // 0 .. 110000 at 100 fps
        const UpperBodyResult r = trackUpperBody(trackOf(poses), kW, kH, true, 20000);
        // Address on index 2, Impact on index 10, Finish on 11 (phasesAt adds it at impact + 10 ms).
        const auto series = buildUpperBodySeries(r, phasesAt(20000, 60000, 100000));
        const auto bare   = buildUpperBodySeries(r, {});    // no ladder ⇒ no domain, same values

        const auto find = [](const std::vector<MetricSeries> &all,
                             const char *key) -> const MetricSeries * {
            for (const MetricSeries &m : all)
                if (m.key == QLatin1String(key)) return &m;
            return nullptr;
        };

        bool narrowedShape = !series.empty(), wholeSwingEmpty = !series.empty();
        for (const MetricSeries &m : series) {
            if (isAddressToImpactMetric(m.key)) {
                narrowedShape = narrowedShape && m.valid.size() == m.t_us.size();
                for (size_t i = 0; i < m.valid.size(); ++i)
                    narrowedShape = narrowedShape && (m.valid[i] == (i > 10 ? 0u : 1u));
            } else {
                wholeSwingEmpty = wholeSwingEmpty && m.valid.empty();
            }
        }
        CHECK("every narrowed channel is 0 after Impact and 1 up to and including it — the "
              "pre-Address head stays VALID",
              narrowedShape);
        CHECK("every whole-swing channel carries NO mask at all — trailElbowHeight, leadHandWidth, "
              "leadUpperArmToChest, leadArmToTorso",
              wholeSwingEmpty);

        const MetricSeries *sp = find(series, "shoulderPlaneAngle");
        CHECK("the Impact sample itself is VALID — 1 at it, 0 after it, so a P7 reading survives",
              sp && sp->valid.size() == 12 && sp->valid[10] == 1u && sp->valid[11] == 0u
                  && hasPhaseSample(*sp, Phase::Impact));

        // THE VALUES DO NOT MOVE: the curve is still continuous and still drawn (dashed outside the
        // domain) and still hovers. Only what may be REDUCED changed.
        const MetricSeries *spBare = find(bare, "shoulderPlaneAngle");
        bool sameValues = sp && spBare && sp->value.size() == spBare->value.size();
        if (sameValues)
            for (size_t i = 0; i < sp->value.size(); ++i)
                sameValues = sameValues && sp->value[i] == spBare->value[i];
        CHECK("value[] is untouched — absence lives in the mask, never in the curve", sameValues);

        // AN UNSEGMENTED IMPACT IS UNBOUNDED: no instant, no tail to mark, and a guess would withdraw
        // real measurements. With the head open that leaves NO mask at all. (applyPhaseDomainMask's
        // own cases are pinned in lower_body_metrics_test §6g, where the helper lives next door.)
        const std::vector<PhaseEvent> noImpact{ { Phase::Address, 20000, 1.f, SegmentRole::Unknown },
                                                { Phase::Top,     60000, 1.f, SegmentRole::Unknown } };
        // Held in a local: `find` returns a pointer INTO the vector, and a temporary dies at the
        // end of the full expression (this assertion first failed on exactly that dangling read).
        const auto noImpSeries = buildUpperBodySeries(r, noImpact);
        const MetricSeries *spNoImp = find(noImpSeries, "shoulderPlaneAngle");
        CHECK("no Impact in the ladder ⇒ no marking at all, `valid` stays EMPTY",
              spNoImp && spNoImp->valid.empty());
    }

    // ── 12. σ propagation (design §5.3, contract C11) ──────────────────────────────────────────
    //
    // WHAT THESE CASES ARE FOR. `MetricSeries::sigma` is 1σ MEASUREMENT noise and the display layer
    // rounds every printed digit to it, so a σ wrong by a factor makes the chart round to the wrong
    // place and say so confidently. Each series' arithmetic is therefore pinned from the fixture's own
    // geometry with the derivation written out.
    //
    // THE FIXTURE, in pixels (sigmaUpperPose on a 1000 px frame):
    //   shoulders (400,300)/(600,300)   hips (450,550)/(550,550)   elbows (440,480)/(560,480)
    //   lead wrist (480,580)            ankles (400,900)/(600,900)
    //   ⇒ neck (500,300)  pelvisCentre (500,550)  thoraxCentre (500,425)
    //
    // THE DERIVED POINTS' σ, with σ_kp = 2 px on every joint (anatomy_vocabulary's midpoints):
    //   neck   = mid(shoulders)          σ = 0.5·sqrt(8)  = 1.4142136 px
    //   pelvis = mid(hips)               σ = 0.5·sqrt(8)  = 1.4142136 px
    //   thorax = 0.25·(4 joints)         σ = 0.25·sqrt(16) = 1.0 px   ← the quietest point here
    //
    // THE SERIES, each from its own formula (see the comment above each push in the producer):
    //   secondaryAxisTilt   sqrt(σ_neck²+σ_pelvis²)/250 rad→deg      = 0.4583662°  (spine 250 px)
    //   shoulderPlaneAngle  sqrt(8)/200 rad→deg                      = 0.8102847°
    //   elbowAlignment      sqrt(8)/120 rad→deg                      = 1.3504745°  (shorter line, more σ)
    //   spineSideBend       sqrt(σ_hipTilt² + σ_shTilt²)             = 1.8118516°  (the noisiest here)
    //   thoraxLateralDrift  THE FULL FORM — see below                 = 3.5399506 %
    //   trailElbowHeight    sqrt(1+s²)·sqrt((1−t)²σ²+t²σ²+σ²)/200·100, t = 0.2, s = 0 ⇒ 1.2961481 %
    //   leadHandWidth       sqrt(σ_wrist²+σ_thorax²)/armLen·100
    //   leadUpperArmToChest sqrt((τ−0.75)²σ²+0.0625·3σ²+τ²σ²)/200·100, τ from the fixture
    //   leadArmToTorso      UNSET, on purpose — see the note in the producer and §12f below
    //
    // thoraxLateralDrift carries a THIRD term that dominates the other two. Keypoint noise in either
    // ankle ROTATES the stance line by dφ ≈ σ/L, and a point h px off that line then moves h·dφ ALONG
    // it. The thorax centre (y = 425) sits h = 475 px above an L = 200 px ankle line, so h/L = 2.375
    // and the rotation terms are the bulk of the answer. Differentiating the whole projection gives
    //   var = σ_thorax² + (1 + (h/L)²)σ_tAnk² + (h/L)²σ_lAnk²
    //       = 1 + 6.640625·4 + 5.640625·4 = 50.125 px²  ⇒  7.0799011 px  ⇒  ·100/200 = 3.5399506 %
    // A `sqrt(σ_thorax² + σ_tAnk²)` reading — the (h/L) = 0 case, and the shape C11 pinned for this
    // channel's two lower-body twins — gives 1.1180340 %, a factor of 3.2 LOW. Design principle 3 rules
    // that out, so all three channels of this geometry now carry the full propagation.
    {
        std::printf("=== 12a) a constant keypoint σ gives each series its closed-form σ ===\n");
        const std::vector<Upper> poses(12, sigmaUpperPose());
        const PoseTrack2D track = sigmaTrackOf(poses, std::vector<double>(12, 2.0));
        // Impact ON THE LAST GRID SAMPLE so the phase-domain tail marks nothing and every frame
        // contributes; §12d is where the mask does the work.
        const auto phases = phasesAt(20000, 60000, 110000);
        const UpperBodyResult r = trackUpperBody(track, kW, kH, /*leadIsLeft=*/true, 20000);
        CHECK("the fixture resolved as designed (shoulders 200 px, hips 100 px, stance 200 px)",
              near(r.ref.shoulderSpanPx, 200.0, 1e-9) && near(r.ref.shoulderDxPx, 200.0, 1e-9)
                  && near(r.ref.hipDxPx, 100.0, 1e-9) && near(r.ref.stanceSpanPx, 200.0, 1e-9));

        // THE PARALLELISM INVARIANT: one σ per pushed value on every channel that carries σ. This is
        // the property that makes a per-sample σ safe to reduce — a channel that pushed a value
        // without one would pair every later σ with the wrong sample.
        CHECK("every σ track is parallel to its channel's values",
              r.sigma.axisTilt.size()         == r.axisTilt.size()
                  && r.sigma.sideBend.size()         == r.sideBend.size()
                  && r.sigma.thoraxDrift.size()      == r.thoraxDrift.size()
                  && r.sigma.shoulderPlane.size()    == r.shoulderPlane.size()
                  && r.sigma.elbowLine.size()        == r.elbowLine.size()
                  && r.sigma.trailElbowHeight.size() == r.trailElbowHeight.size()
                  && r.sigma.leadHandWidth.size()    == r.leadHandWidth.size()
                  && r.sigma.leadArmGap.size()       == r.leadArmGap.size()
                  && r.sigma.trunkRise.size()        == r.trunkRise.size());

        const auto series = buildUpperBodySeries(r, phases);
        CHECK("all eleven series emitted", series.size() == 11);

        const double toDeg  = 57.29577951308232;
        const double q2     = std::sqrt(8.0);          // σ of a difference of two 2 px keypoints
        const double sigTh  = 1.0;                     // the thorax centre: 0.25·sqrt(4·2²)
        // thoraxLateralDrift's lever ratio, from the FIXTURE: the thorax centre (y = 425) sits 475 px
        // above the ankle line (y = 900), which is 200 px long.
        const double levT2  = (475.0 / 200.0) * (475.0 / 200.0);
        const double sigNk  = 0.5 * q2;                // neck and pelvisCentre alike
        // trailElbowHeight: p = trail elbow (560,480), a = trail shoulder (600,300),
        // b = lead shoulder (400,300). dx = −200, t = (560−600)/−200 = 0.2, slope s = 0 (level line).
        const double tEl    = 0.2;
        const double sigTeh = std::sqrt((1.0 - tEl) * (1.0 - tEl) * 4.0 + tEl * tEl * 4.0 + 4.0)
                              * 100.0 / 200.0;
        // leadHandWidth: the address lead-arm length is shoulder→elbow + elbow→hand, from the fixture.
        const double armLen = std::hypot(40.0, 180.0) + std::hypot(40.0, 100.0);
        // leadUpperArmToChest: τ is the along-fraction of the foot of the perpendicular from the
        // thorax onto lead shoulder→lead elbow, u = (40,180), thorax − shoulder = (100,125).
        const double tau    = (100.0 * 40.0 + 125.0 * 180.0) / (40.0 * 40.0 + 180.0 * 180.0);
        const double sigGap = std::sqrt((tau - 0.75) * (tau - 0.75) * 4.0 + 0.0625 * 3.0 * 4.0
                                        + tau * tau * 4.0) * 100.0 / 200.0;

        struct Want { const char *key; double sigma; };
        const Want wants[] = {
            { "secondaryAxisTilt",   std::sqrt(sigNk * sigNk + sigNk * sigNk) / 250.0 * toDeg },
            { "shoulderPlaneAngle",  q2 / 200.0 * toDeg },
            { "elbowAlignment",      q2 / 120.0 * toDeg },
            { "spineSideBend",       std::sqrt((q2 / 100.0 * toDeg) * (q2 / 100.0 * toDeg)
                                               + (q2 / 200.0 * toDeg) * (q2 / 200.0 * toDeg)) },
            // ∂/∂T = û (0.0625 per contributing joint); ∂/∂B = −û − (h/L)n̂; ∂/∂A = (h/L)n̂
            { "thoraxLateralDrift",  std::sqrt(sigTh * sigTh + (1.0 + levT2) * 4.0
                                               + levT2 * 4.0) / 200.0 * 100.0 },
            { "trailElbowHeight",    sigTeh },
            { "leadHandWidth",       std::sqrt(4.0 + sigTh * sigTh) / armLen * 100.0 },
            { "leadUpperArmToChest", sigGap },
            // A distance between the neck and the pelvis centre (two √2 px midpoints), over the
            // 250 px address trunk chord: sqrt(2 + 2)/250·100.
            { "trunkRise",           std::sqrt(sigNk * sigNk + sigNk * sigNk) / 250.0 * 100.0 },
        };
        for (const Want &w : wants) {
            const MetricSeries *m = findSeriesU(series, w.key);
            char label[176];
            std::snprintf(label, sizeof(label), "%s σ = %.7f (closed form)", w.key, w.sigma);
            CHECK(label, m && m->sigma.has_value() && nearRel(*m->sigma, w.sigma, 1e-5));
        }
        // The two figures worth reading as absolutes: the side bend is the noisiest angle here because
        // it adds two tilts and the HIP line is the shorter of them, and the axis tilt is the quietest
        // because the spine is the longest lever in the module.
        const MetricSeries *sb = findSeriesU(series, "spineSideBend");
        const MetricSeries *at = findSeriesU(series, "secondaryAxisTilt");
        CHECK("spineSideBend ≈ 1.81° of σ — 4× the axis tilt's, from the same 2 px keypoints",
              sb && sb->sigma && near(*sb->sigma, 1.8118516, 5e-5));
        CHECK("secondaryAxisTilt ≈ 0.46° of σ (a 250 px lever and two quiet midpoints)",
              at && at->sigma && near(*at->sigma, 0.4583662, 5e-5));
        // And the one channel where the stance line's ROTATION is the answer rather than a correction:
        // 3.54 % of stance width, against 1.12 % for the reading that leaves it out.
        const MetricSeries *td0 = findSeriesU(series, "thoraxLateralDrift");
        CHECK("thoraxLateralDrift's σ is 3.54 % — 7.08 px, and h/L = 2.375 supplies most of it",
              td0 && td0->sigma && near(*td0->sigma, 3.5399506, 5e-5));
    }

    // ── 12b) no smoother ⇒ no σ anywhere ───────────────────────────────────────────────────────
    {
        std::printf("=== 12b) smoothedAux empty ⇒ every sigma UNSET ===\n");
        const std::vector<Upper> poses(12, sigmaUpperPose());
        const auto phases = phasesAt(20000, 60000, 110000);
        const auto bare = buildUpperBodySeries(
            trackUpperBody(sigmaTrackOf(poses, {}), kW, kH, true, 20000), phases);
        bool none = bare.size() == 11;
        for (const MetricSeries &m : bare)
            none = none && !m.sigma.has_value();
        CHECK("no series carries σ — and none carries 0 either", none);

        // A smoothed track whose σ are all ZERO says the same thing: the smoother ran and produced no
        // value for those keypoints (PoseKpAux::sigma's own sentinel), so there is still no budget.
        const auto zeros = buildUpperBodySeries(
            trackUpperBody(sigmaTrackOf(poses, std::vector<double>(12, 0.0)), kW, kH, true, 20000),
            phases);
        bool zn = zeros.size() == 11;
        for (const MetricSeries &m : zeros)
            zn = zn && !m.sigma.has_value();
        CHECK("σ = 0 on every joint is ABSENT, not a perfect measurement", zn);
    }

    // ── 12c) one joint unsmoothed ⇒ those frames drop out, the median does not move ─────────────
    //
    // The rule: every joint a value was built from must report a σ, because an error budget with a
    // hole in it is UNKNOWN rather than small. With a constant σ elsewhere the median is unchanged,
    // which shows the frames were dropped rather than down-weighted. The derived points make this
    // reach further than it looks — one dark shoulder takes out the neck, the thorax, and therefore
    // FIVE of the eight σ.
    {
        std::printf("=== 12c) frames with a missing joint σ are excluded ===\n");
        const std::vector<Upper> poses(12, sigmaUpperPose());
        const auto phases = phasesAt(20000, 60000, 110000);
        const double toDeg = 57.29577951308232;
        const double q2 = std::sqrt(8.0);

        PoseTrack2D some = sigmaTrackOf(poses, std::vector<double>(12, 2.0));
        some.smoothedAux[3].sigma[kLSh] = 0.f;      // lead = LEFT here, so this is the LEAD shoulder
        some.smoothedAux[4].sigma[kLSh] = 0.f;
        const auto ss = buildUpperBodySeries(trackUpperBody(some, kW, kH, true, 20000), phases);
        const MetricSeries *sp = findSeriesU(ss, "shoulderPlaneAngle");
        const MetricSeries *el = findSeriesU(ss, "elbowAlignment");
        CHECK("the median of a constant σ is unmoved by dropping two frames",
              sp && sp->sigma && nearRel(*sp->sigma, q2 / 200.0 * toDeg, 1e-5));
        CHECK("a channel that never reads that joint keeps every frame",
              el && el->sigma && nearRel(*el->sigma, q2 / 120.0 * toDeg, 1e-5));

        // The same joint dark on EVERY frame. Absence is PER SERIES, which is what makes it
        // informative: the elbow line neither reads the shoulder nor any point derived from it.
        PoseTrack2D all = sigmaTrackOf(poses, std::vector<double>(12, 2.0));
        for (PoseKpAux &a : all.smoothedAux) a.sigma[kLSh] = 0.f;
        const auto as = buildUpperBodySeries(trackUpperBody(all, kW, kH, true, 20000), phases);
        const char *shoulderKeys[] = { "shoulderPlaneAngle", "spineSideBend", "trailElbowHeight",
                                       "secondaryAxisTilt", "thoraxLateralDrift", "leadHandWidth",
                                       "leadUpperArmToChest", "trunkRise" };
        bool gone = true;
        for (const char *k : shoulderKeys) {
            const MetricSeries *m = findSeriesU(as, k);
            gone = gone && m && !m->sigma.has_value();
        }
        CHECK("every channel that reads the lead shoulder — directly or through neck/thorax — "
              "loses its σ entirely", gone);
        const MetricSeries *el2 = findSeriesU(as, "elbowAlignment");
        CHECK("…and elbowAlignment, which reads neither, keeps its own",
              el2 && el2->sigma.has_value());
    }

    // ── 12d) the validity mask is honoured: the post-Impact tail does not vote ──────────────────
    //
    // ⚠ THE PHASE DOMAIN'S TAIL IS THE ONLY WAY A σ ENTRY CAN EXIST AND STILL BE MASKED OUT, which is
    // why it is the case that tests the mask. A GATED frame and an OVER-BRIDGED frame both leave the
    // channel with no sample at that instant, so there is no σ entry there to exclude — the channel's
    // sparsity does that. Past impact the sample IS there, is drawn, and is invalid.
    //
    // Eight held frames at σ = 2 px, then TWELVE at σ = 20 px, Impact on frame 7. The tail is the
    // MAJORITY on purpose: if it voted the median would be the high value, so the low answer cannot be
    // luck. The four whole-swing channels are the control — their medians DO move to the tail's value,
    // which shows the exclusion comes from the mask and not from the code ignoring the tail.
    {
        std::printf("=== 12d) masked frames do not enter the median ===\n");
        const std::vector<Upper> poses(20, sigmaUpperPose());
        std::vector<double> sigs;
        for (int k = 0; k < 20; ++k) sigs.push_back(k <= 7 ? 2.0 : 20.0);
        const auto phases = phasesAt(20000, 50000, 70000);          // Impact on frame 7
        const auto series = buildUpperBodySeries(
            trackUpperBody(sigmaTrackOf(poses, sigs), kW, kH, true, 20000), phases);

        const double toDeg = 57.29577951308232;
        const double q2 = std::sqrt(8.0);
        const MetricSeries *sp = findSeriesU(series, "shoulderPlaneAngle");
        CHECK("the tail really is masked (so the case tests what it claims)",
              sp && sp->valid.size() == 20 && sp->valid[7] == 1u && sp->valid[8] == 0u);
        CHECK("shoulderPlaneAngle σ is the HEAD's, though the tail is 12 frames of 20",
              sp && sp->sigma && nearRel(*sp->sigma, q2 / 200.0 * toDeg, 1e-5));
        const MetricSeries *el = findSeriesU(series, "elbowAlignment");
        const MetricSeries *td = findSeriesU(series, "thoraxLateralDrift");
        const double levT2 = (475.0 / 200.0) * (475.0 / 200.0);
        const double tdHead = std::sqrt(1.0 + (1.0 + levT2) * 4.0 + levT2 * 4.0) / 200.0 * 100.0;
        CHECK("…and so are the other Address→Impact channels",
              el && el->sigma && nearRel(*el->sigma, q2 / 120.0 * toDeg, 1e-5)
                  && td && td->sigma && nearRel(*td->sigma, tdHead, 1e-5));

        // The control: trailElbowHeight and leadHandWidth are whole-swing quantities, so every frame
        // is valid for them and the 20 px tail is the majority. Every formula here is homogeneous of
        // degree one in the keypoint σ, so their σ is exactly TEN times the head's.
        const double tEl = 0.2;
        const double sigTehHead = std::sqrt((1.0 - tEl) * (1.0 - tEl) * 4.0 + tEl * tEl * 4.0 + 4.0)
                                  * 100.0 / 200.0;
        const MetricSeries *teh = findSeriesU(series, "trailElbowHeight");
        const double armLen = std::hypot(40.0, 180.0) + std::hypot(40.0, 100.0);
        const MetricSeries *hw = findSeriesU(series, "leadHandWidth");
        CHECK("an UNMASKED channel does see the tail — the mask is doing the excluding",
              teh && teh->sigma && nearRel(*teh->sigma, 10.0 * sigTehHead, 1e-5)
                  && hw && hw->sigma
                  && nearRel(*hw->sigma, 10.0 * std::sqrt(4.0 + 1.0) / armLen * 100.0, 1e-5));
    }

    // ── 12e) σ moves no number ─────────────────────────────────────────────────────────────────
    //
    // Design §6: stages 5.1–5.3 change no persisted `value`. Asserted BITWISE rather than to a
    // tolerance — σ is read off a parallel track and written to one optional field, so there is no
    // rounding for it to hide behind.
    {
        std::printf("=== 12e) value[] and phaseSamples are bit-identical with and without σ ===\n");
        const std::vector<Upper> poses(12, sigmaUpperPose());
        const auto phases = phasesAt(20000, 60000, 110000);
        const auto withSig = buildUpperBodySeries(
            trackUpperBody(sigmaTrackOf(poses, std::vector<double>(12, 2.0)), kW, kH, true, 20000),
            phases);
        const auto without = buildUpperBodySeries(
            trackUpperBody(sigmaTrackOf(poses, {}), kW, kH, true, 20000), phases);

        bool same = withSig.size() == without.size() && !withSig.empty();
        for (size_t i = 0; same && i < withSig.size(); ++i) {
            const MetricSeries &a = withSig[i], &b = without[i];
            same = same && a.key == b.key && a.unit == b.unit
                   && a.t_us == b.t_us && a.value == b.value && a.valid == b.valid
                   && a.phaseSamples.size() == b.phaseSamples.size();
            for (size_t j = 0; same && j < a.phaseSamples.size(); ++j)
                same = same && a.phaseSamples[j].phase == b.phaseSamples[j].phase
                       && a.phaseSamples[j].t_us == b.phaseSamples[j].t_us
                       && a.phaseSamples[j].value == b.phaseSamples[j].value;
        }
        CHECK("every key, t_us, value, valid and phaseSample is identical", same);
    }

    // ── 12f) leadArmToTorso carries NO σ, deliberately ─────────────────────────────────────────
    //
    // C11's escape hatch, used once and pinned here so nobody "fixes" it by hand later. The obvious
    // derivation — the angle between two vectors is the difference of their direction angles, so
    // sqrt(σ_arm² + σ_torso²) — fails twice:
    //
    //   * THE VALUE IS UNSIGNED (an acos, 0–180). The frontal projection cannot say which side of the
    //     torso the arm left on, so the published number near the small angles a hanging arm actually
    //     makes is E|Δ|, biased away from zero by ≈0.8σ, and acos's derivative is singular there. A
    //     symmetric ± σ around it would misstate both the centre and the spread.
    //   * `Neck` IS THE SHOULDER MIDPOINT, so it contains the LEAD SHOULDER — the same keypoint the
    //     arm vector starts from. The two direction angles are CORRELATED and a quadrature sum drops
    //     a covariance term of known sign.
    //
    // Absent is the honest answer, and absent is what the display layer already handles (no chip, and
    // today's one-unit rounding). Fixing it properly means a SIGNED lead-arm-to-torso angle, which is
    // a producer and content change rather than an uncertainty one.
    {
        std::printf("=== 12f) leadArmToTorso leaves sigma UNSET ===\n");
        const std::vector<Upper> poses(12, sigmaUpperPose());
        const auto series = buildUpperBodySeries(
            trackUpperBody(sigmaTrackOf(poses, std::vector<double>(12, 2.0)), kW, kH, true, 20000),
            phasesAt(20000, 60000, 110000));
        const MetricSeries *lat = findSeriesU(series, "leadArmToTorso");
        CHECK("the series is still produced — only its σ is withheld",
              lat != nullptr && !lat->value.empty());
        CHECK("…and its σ is UNSET, not 0", lat && !lat->sigma.has_value());
        // Every OTHER series on the same track does carry one, so the absence is this channel's own
        // decision and not a track-wide failure.
        size_t withSigma = 0;
        for (const MetricSeries &m : series)
            if (m.sigma.has_value()) ++withSigma;
        CHECK("ten of the eleven carry σ on the same track", withSigma == 10);
    }

    // ── 12g) A TILTED line: Euclidean L, and the sqrt(1+s²) factor ─────────────────────────────
    //
    // §12a's fixture is level, so L == |Δx| and s == 0: a mutation replacing the Euclidean length with
    // |Δx|, or dropping heightAboveLineSigmaPx's sqrt(1 + s²), passes it untouched. Drop the trail
    // shoulder 60 px (16.7° on a 200 px line) and both separate by 4.4 %, asserted from both sides.
    //
    // ⚠ AND A REAL PROPERTY WORTH KNOWING: trailElbowHeight's σ comes back at EXACTLY the level
    // fixture's 1.2961481 %, and that is not a coincidence or a bug. sqrt(1 + s²) IS L/|Δx|, and the
    // unit conversion divides by the address shoulder span — which, on a HELD address, is that same L.
    // The two cancel and leave sqrt(w)·100/|Δx|. So the tilt cannot move this number while the address
    // reference equals the live frame; what it CAN do is separate the correct answer from the
    // factor-dropped one, which now divides by 208.8 px without the compensating 1.0440307 and lands
    // 4.2 % low. That is what the second assertion pins.
    {
        std::printf("=== 12g) a tilted line: Euclidean L and sqrt(1+s^2) ===\n");
        const std::vector<Upper> poses(12, sigmaUpperPoseTilted());
        const auto phases = phasesAt(20000, 60000, 110000);
        const auto series = buildUpperBodySeries(
            trackUpperBody(sigmaTrackOf(poses, std::vector<double>(12, 2.0)), kW, kH, true, 20000),
            phases);

        const double q2    = std::sqrt(8.0);
        const double toDeg = 57.29577951308232;
        const double shL   = std::hypot(200.0, 60.0);          // 208.8061 px, from the fixture
        const MetricSeries *sp = findSeriesU(series, "shoulderPlaneAngle");
        CHECK("shoulderPlaneAngle σ = sqrt(8)/208.8061 · 180/π = 0.7761120°",
              sp && sp->sigma && nearRel(*sp->sigma, q2 / shL * toDeg, 1e-5));
        CHECK("…and NOT sqrt(8)/|Δx|, which is 4.40 % larger — the ∂θ/∂Δx term is carried",
              sp && sp->sigma && !nearRel(*sp->sigma, q2 / 200.0 * toDeg, 1e-3));

        // trailElbowHeight: t = (560−600)/(400−600) = 0.2 and s = (300−360)/(400−600) = 0.3, so the
        // factor is sqrt(1.09) = 1.0440307, and the unit divisor is the address Euclidean span 208.8061.
        const double t = 0.2, sl = 0.3;
        const double w = (1.0 - t) * (1.0 - t) * 4.0 + t * t * 4.0 + 4.0;      // 6.72 px²
        const MetricSeries *teh = findSeriesU(series, "trailElbowHeight");
        CHECK("trailElbowHeight σ carries sqrt(1+s²) with s = 0.3",
              teh && teh->sigma
                  && nearRel(*teh->sigma,
                             std::sqrt((1.0 + sl * sl) * w) * 100.0 / shL, 1e-5));
        CHECK("…and is 4.2 % above the value that drops the factor",
              teh && teh->sigma && !nearRel(*teh->sigma, std::sqrt(w) * 100.0 / shL, 1e-3));
        CHECK("…which lands on the LEVEL fixture's 1.2961481 % exactly, because sqrt(1+s²) = L/|Δx| "
              "cancels the address span on a held pose",
              teh && teh->sigma && near(*teh->sigma, 1.2961481, 5e-5));
    }

    // ── 12h) DISTINCT per-joint σ: the coefficients, and the handedness plumbing ────────────────
    //
    // Every case above gives every joint the same σ, which pins no asymmetric coefficient: trailElbow-
    // Height's (1−t)²σ_a² + t²σ_b², leadUpperArmToChest's (τ−0.75) against 0.25, and thoraxLateralDrift's
    // (1 + (h/L)²) on the TRAIL ankle against (h/L)² on the lead one are all invisible under a constant.
    // σ here is pinned to the PHYSICAL keypoint — left shoulder 2, right 3, left elbow 5, right 6, left
    // wrist 7, right 8, left hip 2, right 3, left ankle 1, right ankle 4 — and the same fixture runs with
    // `leadIsLeft` both ways, so the two passes are each other's control.
    //
    // ⚠ WHAT THIS CANNOT PIN, stated rather than implied. The three line tilts and secondaryAxisTilt are
    // SYMMETRIC in their two inputs, and so are the derived midpoints; no fixture can detect their ends
    // being exchanged, because their coefficients are genuinely equal. Those are asserted to be the SAME
    // in both passes, which is the honest claim about them. spineSideBend inherits that symmetry.
    {
        std::printf("=== 12h) distinct per-joint σ, both handedness ===\n");
        const std::vector<Upper> poses(12, sigmaUpperPose());
        const PoseKpAux aux = auxPerJoint(/*lSh=*/2.0, /*rSh=*/3.0, /*lEl=*/5.0, /*rEl=*/6.0,
                                          /*lWr=*/7.0, /*rWr=*/8.0, /*lHip=*/2.0, /*rHip=*/3.0,
                                          /*lAnk=*/1.0, /*rAnk=*/4.0);
        const PoseTrack2D track = trackWithAux(poses, aux);
        const auto phases = phasesAt(20000, 60000, 110000);
        const double toDeg  = 57.29577951308232;
        const double armLen = std::hypot(40.0, 180.0) + std::hypot(40.0, 100.0);   // mirror-symmetric
        const double levT   = 475.0 / 200.0, levT2 = levT * levT;
        const double tau    = (100.0 * 40.0 + 125.0 * 180.0) / (40.0 * 40.0 + 180.0 * 180.0);
        const double tEl    = 0.2;
        const auto q = [](double a, double b) { return std::sqrt(a * a + b * b); };

        for (int pass = 0; pass < 2; ++pass) {
            const bool leadIsLeft = (pass == 0);
            // The σ each ROLE sees, given that the aux is pinned to physical left/right.
            const double sLSh = leadIsLeft ? 2.0 : 3.0, sTSh = leadIsLeft ? 3.0 : 2.0;
            const double sLEl = leadIsLeft ? 5.0 : 6.0, sTEl = leadIsLeft ? 6.0 : 5.0;
            const double sLWr = leadIsLeft ? 7.0 : 8.0;
            const double sLHip = leadIsLeft ? 2.0 : 3.0, sTHip = leadIsLeft ? 3.0 : 2.0;
            const double sLAnk = leadIsLeft ? 1.0 : 4.0, sTAnk = leadIsLeft ? 4.0 : 1.0;
            // The derived points: symmetric in their pairs, so identical in both passes.
            const double sNk = 0.5 * q(sLSh, sTSh), sPl = 0.5 * q(sLHip, sTHip);
            const double sTh = 0.25 * std::sqrt(sLSh * sLSh + sTSh * sTSh
                                                + sLHip * sLHip + sTHip * sTHip);
            const double sHipTilt = q(sLHip, sTHip) / 100.0 * toDeg;
            const double sShTilt  = q(sLSh, sTSh) / 200.0 * toDeg;

            const auto series = buildUpperBodySeries(
                trackUpperBody(track, kW, kH, leadIsLeft, 20000), phases);

            struct Want { const char *key; double sigma; };
            const Want wants[] = {
                // symmetric ⇒ identical in both passes, which is the claim
                { "secondaryAxisTilt",   q(sNk, sPl) / 250.0 * toDeg },
                { "shoulderPlaneAngle",  sShTilt },
                { "elbowAlignment",      q(sLEl, sTEl) / 120.0 * toDeg },
                { "spineSideBend",       q(sHipTilt, sShTilt) },
                // asymmetric: the TRAIL ankle is the reference (1 + (h/L)²), the lead is pure rotation
                { "thoraxLateralDrift",  std::sqrt(sTh * sTh + (1.0 + levT2) * sTAnk * sTAnk
                                                   + levT2 * sLAnk * sLAnk) / 200.0 * 100.0 },
                // asymmetric: a = TRAIL shoulder carries (1−t)², b = LEAD shoulder carries t²
                { "trailElbowHeight",    std::sqrt((1.0 - tEl) * (1.0 - tEl) * sTSh * sTSh
                                                   + tEl * tEl * sLSh * sLSh
                                                   + sTEl * sTEl) * 100.0 / 200.0 },
                { "leadHandWidth",       q(sLWr, sTh) / armLen * 100.0 },
                // asymmetric: the LEAD shoulder's coefficient is (τ−0.75), everything else 0.25 or τ
                { "leadUpperArmToChest", std::sqrt((tau - 0.75) * (tau - 0.75) * sLSh * sLSh
                                                   + 0.0625 * (sTSh * sTSh + sLHip * sLHip
                                                               + sTHip * sTHip)
                                                   + tau * tau * sLEl * sLEl) * 100.0 / 200.0 },
            };
            for (const Want &w : wants) {
                const MetricSeries *m = findSeriesU(series, w.key);
                char label[200];
                std::snprintf(label, sizeof(label), "leadIsLeft=%d  %s σ = %.7f",
                              leadIsLeft ? 1 : 0, w.key, w.sigma);
                CHECK(label, m && m->sigma.has_value() && nearRel(*m->sigma, w.sigma, 1e-5));
            }

            // The three swaps a constant σ could not have caught, each excluded explicitly.
            const MetricSeries *td = findSeriesU(series, "thoraxLateralDrift");
            CHECK("thoraxLateralDrift's two ankle coefficients are NOT interchangeable",
                  td && td->sigma
                      && !nearRel(*td->sigma,
                                  std::sqrt(sTh * sTh + (1.0 + levT2) * sLAnk * sLAnk
                                            + levT2 * sTAnk * sTAnk) / 2.0, 1e-4));
            const MetricSeries *teh = findSeriesU(series, "trailElbowHeight");
            CHECK("trailElbowHeight does not exchange its two shoulder weights",
                  teh && teh->sigma
                      && !nearRel(*teh->sigma,
                                  std::sqrt((1.0 - tEl) * (1.0 - tEl) * sLSh * sLSh
                                            + tEl * tEl * sTSh * sTSh + sTEl * sTEl) / 2.0, 1e-4));
            const MetricSeries *gap = findSeriesU(series, "leadUpperArmToChest");
            CHECK("leadUpperArmToChest gives (τ−0.75) to the LEAD shoulder, not the trail one",
                  gap && gap->sigma
                      && !nearRel(*gap->sigma,
                                  std::sqrt((tau - 0.75) * (tau - 0.75) * sTSh * sTSh
                                            + 0.0625 * (sLSh * sLSh + sLHip * sLHip
                                                        + sTHip * sTHip)
                                            + tau * tau * sLEl * sLEl) / 2.0, 1e-4));
        }
        // The headline numbers, so the swap reads as two figures rather than as an argument:
        // thoraxLateralDrift 5.3271856 % → 4.9627519 %, trailElbowHeight 3.2372828 % → 2.6419690 %,
        // leadUpperArmToChest 2.0350386 % → 2.3947684 %, while shoulderPlaneAngle holds at 1.0329144°.
        const auto left  = buildUpperBodySeries(trackUpperBody(track, kW, kH, true,  20000), phases);
        const auto right = buildUpperBodySeries(trackUpperBody(track, kW, kH, false, 20000), phases);
        const MetricSeries *tl = findSeriesU(left,  "thoraxLateralDrift");
        const MetricSeries *tr = findSeriesU(right, "thoraxLateralDrift");
        const MetricSeries *hl = findSeriesU(left,  "trailElbowHeight");
        const MetricSeries *hr = findSeriesU(right, "trailElbowHeight");
        const MetricSeries *sl = findSeriesU(left,  "shoulderPlaneAngle");
        const MetricSeries *sr = findSeriesU(right, "shoulderPlaneAngle");
        CHECK("swapping the lead side MOVES the asymmetric σ …",
              tl && tr && tl->sigma && tr->sigma && near(*tl->sigma, 5.3271856, 5e-5)
                  && near(*tr->sigma, 4.9627519, 5e-5)
                  && hl && hr && hl->sigma && hr->sigma && near(*hl->sigma, 3.2372828, 5e-5)
                  && near(*hr->sigma, 2.6419690, 5e-5));
        CHECK("… and leaves the symmetric ones alone",
              sl && sr && sl->sigma && sr->sigma && near(*sl->sigma, 1.0329144, 5e-5)
                  && near(*sr->sigma, 1.0329144, 5e-5));
    }

    // ── 12i) the parity off-switch withholds σ too ──────────────────────────────────────────────
    //
    // `channel.maxBridgeUs` < 0 means "emit no validity mask at all", to reproduce the pre-mask bytes
    // for a parity run. σ goes with the masks rather than beside them: written without one it would be a
    // median over the gated and post-Impact frames too — a different number from the masked one, on a
    // new key, in the one run whose whole job is to produce the old bytes.
    {
        std::printf("=== 12i) maxBridgeUs < 0 ⇒ no mask AND no σ ===\n");
        const std::vector<Upper> poses(12, sigmaUpperPose());
        const auto phases = phasesAt(20000, 50000, 80000);      // a real post-Impact tail
        UpperBodyConfig off; off.maxBridgeUs = -1;
        const auto series = buildUpperBodySeries(
            trackUpperBody(sigmaTrackOf(poses, std::vector<double>(12, 2.0)), kW, kH, true,
                           20000, off), phases);
        bool none = series.size() == 11;
        for (const MetricSeries &m : series)
            none = none && m.valid.empty() && !m.sigma.has_value();
        CHECK("no mask and no σ on any series — the switch restores the old bytes whole", none);

        const auto on = buildUpperBodySeries(
            trackUpperBody(sigmaTrackOf(poses, std::vector<double>(12, 2.0)), kW, kH, true, 20000),
            phases);
        size_t withSigma = 0;
        for (const MetricSeries &m : on)
            if (m.sigma.has_value()) ++withSigma;
        CHECK("…while the default run has σ on the ten that propagate one", withSigma == 10);
    }

    // ── 14. trunkRise: forward-bend CHANGE read face-on (posture_metrics_design.md §3.2) ─────────
    //
    // The face-on image shortens the hip-midpoint→shoulder-midpoint chord by the cosine of its lean
    // toward the camera, so a chord L·cos β against its address L·cos β₀ is the reading. The fixture
    // builds exactly that: a 300 px trunk (hips at y 0.60) bent β toward the camera, so its image
    // length is 300·cos β and every expectation below is a cosine a reader can check by hand.
    //
    // THIRTY held address frames, not runOn's six: the address reference is a median over ±250 ms,
    // and with six address frames against six test frames that median would be half the test pose.
    // Here the window (0..270 ms) holds address frames only, so the reference is the address chord
    // exactly. Test pose from 300 ms; Top at 350, Impact at 380, three frames past it.
    {
        std::printf("=== 14) trunkRise ===\n");
        const double kPi = 3.14159265358979323846;
        const auto bentAt = [&](double betaDeg, double shoulderHalfSpan = 0.08) {
            Upper p = addressPose();
            const double chord = 0.300 * std::cos(betaDeg * kPi / 180.0);   // normalized, kW == kH
            p.lHip = QPointF(0.45, 0.60);   p.rHip = QPointF(0.55, 0.60);
            const double shY = 0.60 - chord;
            p.lSh = QPointF(0.50 - shoulderHalfSpan, shY);
            p.rSh = QPointF(0.50 + shoulderHalfSpan, shY);
            return p;
        };
        const std::vector<PhaseEvent> trPhases = phasesAt(20000, 350000, 380000);
        const auto trackFor = [&](const Upper &addr, const Upper &later) {
            std::vector<Upper> poses(30, addr);
            for (int i = 0; i < 12; ++i) poses.push_back(later);
            return trackOf(poses);
        };
        const auto runTrunk = [&](const Upper &addr, const Upper &later) {
            return buildUpperBodySeries(trackUpperBody(trackFor(addr, later), kW, kH, true, 20000), trPhases);
        };
        // (a) Units and sign: 34° at address, standing up to 24° by impact. ℓ grows, so POSITIVE,
        // and it is cos 24°/cos 34° − 1 in %, 10.19 % — the design's "about 10 %".
        {
            const auto series = runTrunk(bentAt(34.0), bentAt(24.0));
            const MetricSeries *tr = findSeriesU(series, "trunkRise");
            CHECK("trunkRise produced, in %", tr && tr->unit == QStringLiteral("%"));
            double addr = 99.0, imp = 0.0;
            CHECK("trunkRise at address", seriesAt(series, "trunkRise", Phase::Address, addr));
            CHECK("…is 0 — the address chord against itself", near(addr, 0.0, 1e-6));
            CHECK("trunkRise at impact", seriesAt(series, "trunkRise", Phase::Impact, imp));
            const double want = 100.0 * (std::cos(24.0 * kPi / 180.0) / std::cos(34.0 * kPi / 180.0) - 1.0);
            std::printf("    34° → 24°: trunkRise %.3f %% (closed form %.3f %%)\n", imp, want);
            CHECK("standing up 34° → 24° reads POSITIVE, cos 24°/cos 34° − 1 = +10.19 %",
                  imp > 0.0 && near(imp, want, 0.05) && near(want, 10.19, 0.01));
            CHECK("…with no Finish sample — the domain is P1..P7", tr && !hasPhaseSample(*tr, Phase::Finish));
            bool tailMasked = tr && tr->valid.size() == tr->t_us.size();
            if (tailMasked)
                for (size_t i = 0; i < tr->t_us.size(); ++i)
                    if (tr->t_us[i] > 380000 && tr->valid[i]) tailMasked = false;
            CHECK("…and every grid sample past Impact is marked invalid", tailMasked);
        }
        // (b) The slope near 34°: one degree of bend lost is about +1.2 % (design §3.2).
        {
            double imp = 0.0;
            CHECK("trunkRise at impact (34° → 33°)",
                  seriesAt(runTrunk(bentAt(34.0), bentAt(33.0)), "trunkRise", Phase::Impact, imp));
            std::printf("    34° → 33°: trunkRise %.3f %% per degree\n", imp);
            CHECK("one degree of bend lost near 34° reads ≈ +1.2 % (1.1 … 1.3)", imp > 1.1 && imp < 1.3);
        }
        // (c) Bending FURTHER over reads negative.
        {
            double imp = 0.0;
            CHECK("trunkRise at impact (34° → 40°)",
                  seriesAt(runTrunk(bentAt(34.0), bentAt(40.0)), "trunkRise", Phase::Impact, imp));
            CHECK("bending further over reads NEGATIVE", imp < -5.0);
        }
        // (d) A SIDE TILT does not move it: the chord rotated within the image plane keeps its
        // length. 15° of lean away from the target with the bend held.
        {
            Upper tilted = bentAt(34.0);
            const double chord = 0.300 * std::cos(34.0 * kPi / 180.0);
            const double dx = chord * std::sin(15.0 * kPi / 180.0), dy = chord * std::cos(15.0 * kPi / 180.0);
            tilted.lSh = QPointF(0.42 + dx, 0.60 - dy);   tilted.rSh = QPointF(0.58 + dx, 0.60 - dy);
            double imp = 99.0;
            CHECK("trunkRise at impact (side tilt)",
                  seriesAt(runTrunk(bentAt(34.0), tilted), "trunkRise", Phase::Impact, imp));
            CHECK("a 15° side tilt with the bend held reads ≈ 0", near(imp, 0.0, 0.05));
        }
        // (e) THE 40 % SHOULDER-SPAN REFUSAL. The trunk stands up AND the shoulders foreshorten to
        // 30 % of their address span (a turned trunk at the top): no sample, refused not bridged —
        // and at 50 % the same trunk reads.
        {
            const auto narrowT = trackFor(bentAt(34.0), bentAt(24.0, 0.08 * 0.30));
            const UpperBodyResult r = trackUpperBody(narrowT, kW, kH, true, 20000);
            const auto narrow = buildUpperBodySeries(r, trPhases);
            double v = 0.0;
            const MetricSeries *tr = findSeriesU(narrow, "trunkRise");
            CHECK("shoulders at 30 % of their address span ⇒ NO trunkRise sample at the top or impact",
                  tr && !seriesAt(narrow, "trunkRise", Phase::Top, v)
                      && !seriesAt(narrow, "trunkRise", Phase::Impact, v));
            bool refused = tr && tr->valid.size() == tr->t_us.size();
            if (refused)
                for (size_t i = 0; i < tr->t_us.size(); ++i)
                    if (tr->t_us[i] >= 300000 && tr->valid[i]) refused = false;
            CHECK("…every refused instant is invalid — the gate is never bridged", refused);
            bool gateRecord = false;
            for (const GateRecord &g : upperBodyGates(r, trPhases))
                if (g.gate == QStringLiteral("body.shoulderLine") && g.subject == QStringLiteral("trunkRise"))
                    gateRecord = true;
            CHECK("…and the shoulder-line gate leaves a record on trunkRise", gateRecord);
            CHECK("at 50 % of the address span the same trunk is read",
                  seriesAt(runTrunk(bentAt(34.0), bentAt(24.0, 0.08 * 0.50)), "trunkRise", Phase::Impact, v)
                      && v > 9.0);
        }
        // (f) No hips at address ⇒ no trunk chord ⇒ trunkRise absent, the rest unaffected.
        {
            PoseTrack2D t = trackFor(bentAt(34.0), bentAt(34.0));
            for (PoseFrame2D &f : t.frames) { f.conf[kLHip] = 0.05f; f.conf[kRHip] = 0.05f; }
            const auto series = buildUpperBodySeries(trackUpperBody(t, kW, kH, true, 20000), trPhases);
            CHECK("hips never seen ⇒ NO trunkRise, while the shoulder line is still produced",
                  !hasSeries(series, "trunkRise") && hasSeries(series, "shoulderPlaneAngle"));
        }
    }

    // ── 15. leadArmToTorso is gated on its two directions (13 Nov 2026, plane stack FA) ─────────
    //
    // The torso's (the shoulder line, ratio 0.40, as trunkRise) and the upper arm's (its image length
    // 0.45 … 1.4 × address). Refused instants go on the series' gated list — invalid, never bridged —
    // and the grade's instants (Impact, P8) get a REFUSED record naming body.armTorsoLines.
    {
        std::printf("=== 15) leadArmToTorso: shoulder line and upper-arm gates ===\n");
        // Six held address frames (0–50 ms), then six test frames (60–110 ms) with Impact at 70 ms
        // and P8 at 100 ms inside them.
        const std::vector<PhaseEvent> ph{
            { Phase::Address,              20000,  1.f, SegmentRole::Unknown },
            { Phase::Impact,               70000,  1.f, SegmentRole::Unknown },
            { Phase::ShaftParallelThrough, 100000, 1.f, SegmentRole::Unknown },
            { Phase::Finish,               110000, 1.f, SegmentRole::Unknown } };
        // The address window covers the HELD address only (as §11): the shipped 250 ms would take the
        // reference median over the test frames too, and the ratios' denominators with it.
        UpperBodyConfig base;  base.addrWindowUs = 30000;
        const auto runArm = [&](const Upper &test, const UpperBodyConfig &cfg) {
            std::vector<Upper> poses(6, addressPose());
            for (int i = 0; i < 6; ++i) poses.push_back(test);
            return trackUpperBody(trackOf(poses), kW, kH, true, 20000, cfg);
        };
        // The gate wide open — the channel as it was before 13 Nov 2026.
        UpperBodyConfig open = base;
        open.minShoulderSpanRatio = 0.0;  open.minUpperArmRatio = 0.0;  open.maxUpperArmRatio = 1e9;
        // Lead shoulder (0.42, 0.30); address upper arm (0.02, 0.12), 121.7 px. Scale it by k.
        const auto armAt = [](double k) {
            Upper p = addressPose();
            p.lEl = QPointF(0.42 + 0.02 * k, 0.30 + 0.12 * k);
            return p;
        };
        const auto atImpact = [&](const UpperBodyResult &r, double &v) {
            return seriesAt(buildUpperBodySeries(r, ph), "leadArmToTorso", Phase::Impact, v);
        };
        const auto refusedAt = [&](const UpperBodyResult &r, const char *phase) {
            for (const GateRecord &g : upperBodyGates(r, ph))
                if (g.gate == QStringLiteral("body.armTorsoLines") && g.subject == QStringLiteral("leadArmToTorso")
                    && g.effect == GateEffect::Refused
                    && g.evidence.value(QStringLiteral("phase")).toString() == QLatin1String(phase))
                    return true;
            return false;
        };
        const auto masked = [&](const UpperBodyResult &r) {
            for (const GateRecord &g : upperBodyGates(r, ph))
                if (g.gate == QStringLiteral("body.armTorsoLines") && g.effect == GateEffect::Masked
                    && g.evidence.value(QStringLiteral("frames")).toInt() == 6)
                    return true;
            return false;
        };

        // (a) 0.7 × the address upper arm: the same value as before the gate. The arm's direction is
        //     unchanged, so atan(0.02 / 0.12) = 9.46° from the vertical torso — and the record is clean.
        {
            const UpperBodyResult gated = runArm(armAt(0.7), base), was = runArm(armAt(0.7), open);
            double v = 0.0, w = 0.0;
            CHECK("upper arm at 0.7 × address: read at impact", atImpact(gated, v) && atImpact(was, w));
            std::printf("    0.7×: %.4f° (gate open %.4f°)\n", v, w);
            CHECK("…the same value as with the gate open (9.46°)", v == w && near(v, 9.46, 0.05));
            CHECK("…nothing gated, no record", gated.gatedArmTorso.empty() && !masked(gated)
                                                  && !refusedAt(gated, "P7") && !refusedAt(gated, "P8"));
        }
        // (b) 0.15 × and 2.5 ×: no value, gated, refused at both of the grade's instants.
        for (const double k : { 0.15, 2.5 }) {
            const UpperBodyResult r = runArm(armAt(k), base);
            double v = 0.0;
            const auto series = buildUpperBodySeries(r, ph);
            const MetricSeries *m = findSeriesU(series, "leadArmToTorso");
            bool invalid = m && m->valid.size() == m->t_us.size();
            if (invalid)
                for (size_t i = 6; i < m->t_us.size(); ++i) invalid = invalid && m->valid[i] == 0u;
            char lab[5][128];
            std::snprintf(lab[0], sizeof lab[0], "upper arm at %.2f × address: no value at impact", k);
            std::snprintf(lab[1], sizeof lab[1], "…%.2f ×: every such frame on the gated list and invalid (never bridged)", k);
            std::snprintf(lab[2], sizeof lab[2], "…%.2f ×: a Masked record, and a Refused one at P7 and at P8", k);
            std::snprintf(lab[3], sizeof lab[3], "…%.2f ×: the evidence names the arm, not the shoulders", k);
            std::snprintf(lab[4], sizeof lab[4], "…%.2f ×: leadUpperArmToChest is still read", k);
            CHECK(lab[0], !atImpact(r, v));
            CHECK(lab[1], invalid && r.gatedArmTorso.size() == 6);
            CHECK(lab[2], masked(r) && refusedAt(r, "P7") && refusedAt(r, "P8"));
            CHECK(lab[3], !r.gatedArmTorsoArmRatio.empty() && near(r.gatedArmTorsoArmRatio.front(), k, 0.01)
                              && near(r.gatedArmTorsoShoulderRatio.front(), 1.0, 0.01));
            // The neighbours that read the same arm are not gated by it.
            CHECK(lab[4], seriesAt(series, "leadUpperArmToChest", Phase::Impact, v));
        }
        // (c) The shoulders collapsed to 0.3 × their address span (a chest turned square to the
        //     target line): no value, gated, refused — while the arm itself is the address arm.
        {
            Upper turned = addressPose();
            turned.lSh = QPointF(0.476, 0.30);  turned.rSh = QPointF(0.524, 0.30);   // |dx| 48 px of 160
            const UpperBodyResult r = runArm(turned, base);
            double v = 0.0;
            CHECK("shoulders at 0.3 × their address span: no value at impact", !atImpact(r, v));
            CHECK("…gated, with the shoulder ratio in the evidence",
                  r.gatedArmTorso.size() == 6 && near(r.gatedArmTorsoShoulderRatio.front(), 0.3, 0.01));
            CHECK("…refused at P7 and at P8", refusedAt(r, "P7") && refusedAt(r, "P8"));
            // The record is the one a missing grade names, and its sentence fills.
            const std::vector<GateRecord> recs = upperBodyGates(r, ph);
            const GateRecord *primary = pinpoint::gates::primaryRecord(recs, QStringLiteral("leadArmToTorso"));
            const pinpoint::gates::GateEntry *entry = pinpoint::gates::gateEntry(QStringLiteral("body.armTorsoLines"));
            CHECK("…the Refused record is the primary record for leadArmToTorso",
                  primary && primary->gate == QStringLiteral("body.armTorsoLines")
                      && primary->effect == GateEffect::Refused);
            if (primary && entry) {
                const QString text = pinpoint::gates::evidenceText(*entry, primary->evidence);
                std::printf("    %s\n", qPrintable(text));
                CHECK("…its sentence names the instant and fills every figure",
                      text.contains(QStringLiteral("none at impact")) && !text.contains(QLatin1Char('{')));
            }
            CHECK("…and the gate open reads it (the old behaviour)", atImpact(runArm(turned, open), v));
        }
        // (d) Only the grade's instants are refused: the test frames start at 60 ms, so an Impact at
        //     30 ms (inside the held address) is read, and only P8 gets a Refused record.
        {
            const std::vector<PhaseEvent> early{
                { Phase::Address,              20000,  1.f, SegmentRole::Unknown },
                { Phase::Impact,               30000,  1.f, SegmentRole::Unknown },
                { Phase::ShaftParallelThrough, 100000, 1.f, SegmentRole::Unknown } };
            const UpperBodyResult r = runArm(armAt(0.15), base);
            bool p7 = false, p8 = false;
            for (const GateRecord &g : upperBodyGates(r, early))
                if (g.gate == QStringLiteral("body.armTorsoLines") && g.effect == GateEffect::Refused) {
                    p7 = p7 || g.evidence.value(QStringLiteral("phase")).toString() == QLatin1String("P7");
                    p8 = p8 || g.evidence.value(QStringLiteral("phase")).toString() == QLatin1String("P8");
                }
            CHECK("an Impact the gate left alone gets no Refused record; the gated P8 does", !p7 && p8);
            // No P8 on the ladder ⇒ no P8 record (the grade is then missing for the phase, not the gate).
            const std::vector<PhaseEvent> noP8{ early[0], early[1] };
            bool any = false;
            for (const GateRecord &g : upperBodyGates(r, noP8))
                any = any || (g.gate == QStringLiteral("body.armTorsoLines") && g.effect == GateEffect::Refused);
            CHECK("no P8 on the ladder ⇒ no Refused record for it", !any);
        }
        // (d2) One gated frame at impact and its neighbours UNREADABLE (the lead elbow lost, e.g. to
        //      motion blur): the grader has nothing at P7, so the Refused record must still fire.
        //      Frames 60 / 70 / 80 ms lie in Impact ± 15 ms; 70 is gated (arm 0.15 ×), 60 and 80 have
        //      no lead elbow. Counting every grid frame would have called that window "two readings".
        {
            std::vector<Upper> poses(6, addressPose());
            for (int i = 0; i < 6; ++i) poses.push_back(addressPose());
            poses[7] = armAt(0.15);                                  // 70 ms
            PoseTrack2D tr = trackOf(poses);
            tr.frames[6].conf[kLEl] = 0.05f;                         // 60 ms: unreadable
            tr.frames[8].conf[kLEl] = 0.05f;                         // 80 ms: unreadable
            const UpperBodyResult r = trackUpperBody(tr, kW, kH, true, 20000, base);
            const GateRecord *p7 = nullptr;
            const std::vector<GateRecord> recs = upperBodyGates(r, ph);
            for (const GateRecord &g : recs)
                if (g.gate == QStringLiteral("body.armTorsoLines") && g.effect == GateEffect::Refused
                    && g.evidence.value(QStringLiteral("phase")).toString() == QLatin1String("P7"))
                    p7 = &g;
            CHECK("one gated frame + unreadable neighbours at impact ⇒ the Refused record at P7 still fires",
                  r.gatedArmTorso.size() == 1 && p7 != nullptr);
            CHECK("…its ofFrames counts only the frame that could have read (1 of 1)",
                  p7 && p7->evidence.value(QStringLiteral("frames")).toInt() == 1
                      && p7->evidence.value(QStringLiteral("ofFrames")).toInt() == 1);
            CHECK("…and P8, read normally, gets none", !refusedAt(r, "P8"));
        }
        // (e) The two knobs are sweepable.
        {
            QVariantMap ov;
            ov.insert(QStringLiteral("upperBody.minUpperArmRatio"), 0.1);
            ov.insert(QStringLiteral("upperBody.maxUpperArmRatio"), 3.0);
            UpperBodyConfig c = UpperBodyConfig::fromOverrides(ov);
            CHECK("upperBody.minUpperArmRatio / maxUpperArmRatio override",
                  c.minUpperArmRatio == 0.1 && c.maxUpperArmRatio == 3.0);
            CHECK("defaults 0.45 / 1.4", UpperBodyConfig().minUpperArmRatio == 0.45
                                           && UpperBodyConfig().maxUpperArmRatio == 1.4);
            c.addrWindowUs = base.addrWindowUs;
            double v = 0.0;
            CHECK("…and 0.15 × reads with the floor lowered to 0.1", atImpact(runArm(armAt(0.15), c), v));
        }
    }

    // ── 16. leadArmFlexion from the face-on camera (13 Nov 2026, plane stack D5) ─────────────────
    //
    // 180° − the angle at the lead elbow between the lead shoulder and the lead wrist; 0 = straight,
    // + = more bend (the wrist sensors' sign). Gated on the upper arm's and the forearm's image length
    // against address, and on nothing else (no shoulder line). The fixture hangs the lead upper arm
    // straight DOWN 100 px × kU from the lead shoulder and the forearm 100 px × kF from the elbow,
    // turned β off the arm's line — so the bend is β by construction, and every σ below is a closed form
    // written from the fixture's own lengths.
    {
        std::printf("=== 16) leadArmFlexion: the face-on lead elbow ===\n");
        const auto armPose = [](double betaDeg, double kU = 1.0, double kF = 1.0) {
            Upper p = sigmaUpperPose();
            const double b = betaDeg / 57.29577951308232;
            p.lSh = QPointF(0.40, 0.30);
            p.lEl = QPointF(0.40, 0.30 + 0.10 * kU);
            p.lWr = QPointF(p.lEl.x() + 0.10 * kF * std::sin(b), p.lEl.y() + 0.10 * kF * std::cos(b));
            return p;
        };
        const std::vector<PhaseEvent> ph{
            { Phase::Address, 20000,  1.f, SegmentRole::Unknown },
            { Phase::Impact,  80000,  1.f, SegmentRole::Unknown },
            { Phase::Finish,  150000, 1.f, SegmentRole::Unknown } };
        UpperBodyConfig base;  base.addrWindowUs = 30000;   // the held address only, as §15

        // (a) A KNOWN BEND AT EVERY FRAME: six address frames at 10°, then β = 4 + 5·i for i = 0…9.
        {
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 10; ++i) poses.push_back(armPose(4.0 + 5.0 * i));
            const UpperBodyResult r = trackUpperBody(trackOf(poses), kW, kH, true, 20000, base);
            const auto series = buildUpperBodySeries(r, ph);
            const MetricSeries *m = findSeriesU(series, "leadArmFlexion");
            bool exact = m && m->value.size() == poses.size();
            double worst = 0.0;
            for (size_t j = 0; exact && j < poses.size(); ++j) {
                const double want = j < 6 ? 10.0 : 4.0 + 5.0 * double(j - 6);
                worst = std::max(worst, std::fabs(m->value[j] - want));
            }
            std::printf("    worst |bend − β| over 16 frames: %.2e°\n", worst);
            CHECK("the bend at every frame is β (to 1e-9°)", exact && worst < 1e-9);
            double v = 0.0;
            CHECK("…read at impact (80 ms, β = 14°)", seriesAt(series, "leadArmFlexion", Phase::Impact, v)
                                                          && near(v, 14.0, 1e-9));
            CHECK("…published as the faceOn route, Estimated (not Direct)",
                  m && m->routeId == QStringLiteral("faceOn") && !m->routeDirect);
            CHECK("…nothing gated, no σ on a track with no smoother record",
                  r.gatedLeadElbow.empty() && m && !m->sigma.has_value());
            // The two other routes' sign: a straight arm reads 0, a bent one reads +.
            const auto straight = buildUpperBodySeries(
                trackUpperBody(trackOf(std::vector<Upper>(12, armPose(0.0))), kW, kH, true, 20000, base), ph);
            CHECK("a straight lead arm reads 0°", seriesAt(straight, "leadArmFlexion", Phase::Impact, v) && near(v, 0.0, 1e-6));
            // A mirrored camera filming a left-handed golfer: the same posture, the same number.
            std::vector<Upper> mir;
            for (const Upper &p : poses) mir.push_back(mirrored(p));
            const auto ms = buildUpperBodySeries(trackUpperBody(trackOf(mir), kW, kH, false, 20000, base), ph);
            CHECK("…and the mirror of a left-hander reads the same bend",
                  seriesAt(ms, "leadArmFlexion", Phase::Impact, v) && near(v, 14.0, 1e-9));
        }

        // (b) σ: the three keypoints through the angle, the elbow's two terms combined as vectors.
        //     A held pose, β = 30°, upper arm 100 px, forearm 80 px (kF 0.8), so the shoulder and wrist
        //     terms are told apart; σ_S = 1, σ_E = 3, σ_W = 2 px on the LEAD (left) joints.
        {
            const std::vector<Upper> poses(12, armPose(30.0, 1.0, 0.8));
            const PoseKpAux aux = auxPerJoint(1.0, 1.0, 3.0, 1.0, 2.0, 1.0, 1.0, 1.0, 1.0, 1.0);
            const auto series = buildUpperBodySeries(
                trackUpperBody(trackWithAux(poses, aux), kW, kH, true, 20000, base), ph);
            const MetricSeries *m = findSeriesU(series, "leadArmFlexion");
            const double lu = 100.0, lv = 80.0, cosT = std::cos(150.0 / 57.29577951308232);
            const double var = 1.0 / (lu * lu) + 4.0 / (lv * lv)
                               + 9.0 * (1.0 / (lu * lu) + 1.0 / (lv * lv) - 2.0 * cosT / (lu * lv));
            const double want = std::sqrt(var) * 57.29577951308232;
            std::printf("    σ = %.4f° (closed form %.4f°)\n", m && m->sigma ? *m->sigma : -1.0, want);
            CHECK("σ is the propagated closed form (4.043°)", m && m->sigma && nearRel(*m->sigma, want, 1e-6));
            // Treating the elbow's two terms as independent drops −2·cos θ/(|u||v|)·σ_E², 22 % low here (3.15°).
            const double naive = std::sqrt(1.0 / (lu * lu) + 4.0 / (lv * lv) + 9.0 * (1.0 / (lu * lu) + 1.0 / (lv * lv)))
                                 * 57.29577951308232;
            CHECK("…and not the sum that drops the elbow's shared term", m && m->sigma && std::fabs(*m->sigma - naive) > 0.2);
            // One unsmoothed lead joint ⇒ no budget at all for the elbow (never a partial one).
            PoseKpAux dark = aux;  dark.sigma[size_t(kLWr)] = 0.f;
            const auto noSig = buildUpperBodySeries(
                trackUpperBody(trackWithAux(poses, dark), kW, kH, true, 20000, base), ph);
            const MetricSeries *n = findSeriesU(noSig, "leadArmFlexion");
            CHECK("a lead wrist with no σ leaves the elbow's σ UNSET", n && !n->sigma.has_value());
        }

        // (c) A FORESHORTENED SEGMENT IS GATED, with the record. Six address frames (β 10°), then six
        //     test frames (60–110 ms) around Impact at 80 ms with one segment scaled.
        const auto refusedAtImpact = [&](const UpperBodyResult &r) -> const GateRecord * {
            static std::vector<GateRecord> recs;
            recs = upperBodyGates(r, ph);
            for (const GateRecord &g : recs)
                if (g.gate == QStringLiteral("body.leadElbowLines") && g.subject == QStringLiteral("leadArmFlexion")
                    && g.effect == GateEffect::Refused
                    && g.evidence.value(QStringLiteral("phase")).toString() == QLatin1String("P7"))
                    return &g;
            return nullptr;
        };
        const auto maskedElbow = [&](const UpperBodyResult &r) {
            for (const GateRecord &g : upperBodyGates(r, ph))
                if (g.gate == QStringLiteral("body.leadElbowLines") && g.effect == GateEffect::Masked
                    && g.evidence.value(QStringLiteral("frames")).toInt() == 6)
                    return true;
            return false;
        };
        struct Case { const char *what; double kU, kF; };
        for (const Case &c : { Case{ "upper arm 0.30 ×", 0.30, 1.0 }, Case{ "forearm 0.30 ×", 1.0, 0.30 },
                               Case{ "forearm 2.0 ×", 1.0, 2.0 } }) {
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 6; ++i) poses.push_back(armPose(40.0, c.kU, c.kF));
            const UpperBodyResult r = trackUpperBody(trackOf(poses), kW, kH, true, 20000, base);
            const auto series = buildUpperBodySeries(r, ph);
            const MetricSeries *m = findSeriesU(series, "leadArmFlexion");
            bool invalid = m && m->valid.size() == m->t_us.size();
            if (invalid)
                for (size_t i = 6; i < m->t_us.size(); ++i) invalid = invalid && m->valid[i] == 0u;
            double v = 0.0;
            const GateRecord *rec = refusedAtImpact(r);
            char lab[4][128];
            std::snprintf(lab[0], sizeof lab[0], "%s: no bend at impact", c.what);
            std::snprintf(lab[1], sizeof lab[1], "…%s: every such frame gated and invalid (never bridged)", c.what);
            std::snprintf(lab[2], sizeof lab[2], "…%s: a Masked record, and a Refused one at P7", c.what);
            std::snprintf(lab[3], sizeof lab[3], "…%s: the evidence carries both segments' ratios", c.what);
            CHECK(lab[0], !seriesAt(series, "leadArmFlexion", Phase::Impact, v));
            CHECK(lab[1], invalid && r.gatedLeadElbow.size() == 6);
            CHECK(lab[2], maskedElbow(r) && rec != nullptr);
            CHECK(lab[3], rec && near(rec->evidence.value(QStringLiteral("armRatio")).toDouble(), c.kU, 0.01)
                              && near(rec->evidence.value(QStringLiteral("forearmRatio")).toDouble(), c.kF, 0.01));
        }
        // The Refused record is the one a missing grade names, and its sentence fills.
        {
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 6; ++i) poses.push_back(armPose(40.0, 1.0, 0.30));
            const UpperBodyResult r = trackUpperBody(trackOf(poses), kW, kH, true, 20000, base);
            const std::vector<GateRecord> recs = upperBodyGates(r, ph);
            const GateRecord *primary = pinpoint::gates::primaryRecord(recs, QStringLiteral("leadArmFlexion"));
            const pinpoint::gates::GateEntry *entry = pinpoint::gates::gateEntry(QStringLiteral("body.leadElbowLines"));
            CHECK("the Refused record is the primary record for leadArmFlexion",
                  primary && primary->gate == QStringLiteral("body.leadElbowLines") && primary->effect == GateEffect::Refused);
            if (primary && entry) {
                const QString text = pinpoint::gates::evidenceText(*entry, primary->evidence);
                std::printf("    %s\n", qPrintable(text));
                CHECK("…its sentence names impact, fills every figure and never names the shoulders",
                      text.contains(QStringLiteral("none at impact")) && !text.contains(QLatin1Char('{'))
                          && !text.contains(QStringLiteral("shoulders")));
            }
        }
        // (d) NO SHOULDER GATE: the shoulders collapsed to 0.15 × their address span refuse
        //     leadArmToTorso, and leave the elbow read.
        {
            Upper turned = armPose(25.0);
            turned.rSh = QPointF(0.43, 0.30);    // |dx| 30 px of 200
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 6; ++i) poses.push_back(turned);
            const UpperBodyResult r = trackUpperBody(trackOf(poses), kW, kH, true, 20000, base);
            const auto series = buildUpperBodySeries(r, ph);
            double v = 0.0, w = 0.0;
            CHECK("shoulders edge-on: the elbow still reads 25°",
                  seriesAt(series, "leadArmFlexion", Phase::Impact, v) && near(v, 25.0, 1e-9) && r.gatedLeadElbow.empty());
            CHECK("…where leadArmToTorso is refused", !seriesAt(series, "leadArmToTorso", Phase::Impact, w));
        }
        // (e) The forearm knobs are sweepable; the defaults are 0.45 / 1.4.
        {
            QVariantMap ov;
            ov.insert(QStringLiteral("upperBody.minForearmRatio"), 0.2);
            ov.insert(QStringLiteral("upperBody.maxForearmRatio"), 2.5);
            UpperBodyConfig c = UpperBodyConfig::fromOverrides(ov);
            CHECK("upperBody.minForearmRatio / maxForearmRatio override", c.minForearmRatio == 0.2 && c.maxForearmRatio == 2.5);
            CHECK("defaults 0.45 / 1.4", UpperBodyConfig().minForearmRatio == 0.45 && UpperBodyConfig().maxForearmRatio == 1.4);
            c.addrWindowUs = base.addrWindowUs;
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 6; ++i) poses.push_back(armPose(40.0, 1.0, 0.30));
            double v = 0.0;
            CHECK("…and a 0.30 × forearm reads 40° with the floor lowered to 0.2",
                  seriesAt(buildUpperBodySeries(trackUpperBody(trackOf(poses), kW, kH, true, 20000, c), ph),
                           "leadArmFlexion", Phase::Impact, v) && near(v, 40.0, 1e-9));
        }
        // (f) THE WRIST SENSORS' ELBOW WINS: with a sensor leadArmFlexion in the document, the face-on
        //     series is erased and its records stop speaking for the key; with none, nothing moves.
        {
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 6; ++i) poses.push_back(armPose(40.0, 1.0, 0.30));
            const UpperBodyResult r = trackUpperBody(trackOf(poses), kW, kH, true, 20000, base);
            std::vector<MetricSeries> series = buildUpperBodySeries(r, ph);
            std::vector<GateRecord> recs = upperBodyGates(r, ph);
            GateRecord cascade;  cascade.gate = QStringLiteral("body.upperAddress");
            cascade.subject = QStringLiteral("secondaryAxisTilt");
            cascade.covers = { QStringLiteral("spineSideBend"), QStringLiteral("leadArmFlexion") };
            recs.push_back(cascade);
            const size_t nSeries = series.size(), nRecs = recs.size();
            CHECK("no sensor elbow ⇒ the face-on one stays, with its records",
                  applyLeadElbowSensorPrecedence(series, recs) == 0 && series.size() == nSeries && recs.size() == nRecs);
            MetricSeries sensor;  sensor.key = QStringLiteral("leadArmFlexion");
            sensor.t_us = { 0, 10000 };  sensor.value = { 5.0, 6.0 };
            series.push_back(sensor);
            const int dropped = applyLeadElbowSensorPrecedence(series, recs);
            int faceOn = 0, sensors = 0;
            for (const MetricSeries &m : series)
                if (m.key == QStringLiteral("leadArmFlexion")) (m.routeId == QStringLiteral("faceOn") ? faceOn : sensors)++;
            bool noElbowRecord = true, coversClean = false;
            for (const GateRecord &g : recs) {
                noElbowRecord = noElbowRecord && g.gate != QStringLiteral("body.leadElbowLines");
                if (g.gate == QStringLiteral("body.upperAddress"))
                    coversClean = !g.covers.contains(QStringLiteral("leadArmFlexion")) && g.covers.contains(QStringLiteral("spineSideBend"));
            }
            CHECK("a sensor elbow ⇒ the face-on series is dropped and the sensor's stands",
                  dropped == 1 && faceOn == 0 && sensors == 1);
            CHECK("…the face-on gate's records go, and the cascade no longer covers the key", noElbowRecord && coversClean);
        }
        // (g) A sensor elbow and NO face-on series — the face-on address reference was refused, so the
        //     camera wrote nothing but its cascade still covers the key. The covers are cleaned anyway.
        {
            std::vector<MetricSeries> series;
            MetricSeries sensor;  sensor.key = QStringLiteral("leadArmFlexion");
            sensor.t_us = { 0, 10000 };  sensor.value = { 5.0, 6.0 };
            series.push_back(sensor);
            std::vector<GateRecord> recs;
            for (const char *gate : { "body.upperAddress", "body.shoulderSpan" }) {
                GateRecord g;  g.gate = QLatin1String(gate);  g.subject = QStringLiteral("secondaryAxisTilt");
                g.effect = GateEffect::Refused;
                g.covers = { QStringLiteral("spineSideBend"), QStringLiteral("leadArmFlexion") };
                recs.push_back(g);
            }
            const int dropped = applyLeadElbowSensorPrecedence(series, recs);
            bool clean = recs.size() == 2;
            for (const GateRecord &g : recs)
                clean = clean && !g.covers.contains(QStringLiteral("leadArmFlexion")) && g.covers.contains(QStringLiteral("spineSideBend"));
            CHECK("a sensor elbow with no face-on series: nothing dropped, the address refusals no longer cover the key",
                  dropped == 0 && series.size() == 1 && clean);
        }
        // (h) THE 3-D FIT'S MEASURED ELBOW (leadArmFlexion3d, plane stack D2) sits between the two:
        //     wristImus > 3-D > face-on. It stands beside the face-on series (a different key, which
        //     chicken_wing reads), and goes wherever the sensors' elbow reads.
        {
            MetricSeries faceOn;  faceOn.key = QStringLiteral("leadArmFlexion");  faceOn.routeId = QStringLiteral("faceOn");
            faceOn.t_us = { 0, 10000 };  faceOn.value = { 12.0, 14.0 };
            MetricSeries fit3d;  fit3d.key = QStringLiteral("leadArmFlexion3d");  fit3d.routeId = QStringLiteral("faceOn+dtl");
            fit3d.t_us = { 0, 10000 };  fit3d.value = { 20.0, 22.0 };
            std::vector<MetricSeries> series{ faceOn, fit3d };
            std::vector<GateRecord> recs;
            CHECK("no sensor elbow ⇒ the 3-D elbow and the face-on one both stand",
                  applyLeadElbowSensorPrecedence(series, recs) == 0 && series.size() == 2);
            MetricSeries sensor;  sensor.key = QStringLiteral("leadArmFlexion");
            sensor.t_us = { 0, 10000 };  sensor.value = { 5.0, 6.0 };
            series.push_back(sensor);
            const int dropped = applyLeadElbowSensorPrecedence(series, recs);
            CHECK("a sensor elbow ⇒ the 3-D elbow goes with the face-on one, the sensor's alone stands",
                  dropped == 2 && series.size() == 1 && series.front().key == QStringLiteral("leadArmFlexion")
                      && series.front().routeId.isEmpty());
            // (h′) D6: bent_lead_arm's 3-D change (leadElbowBendChange3d) and its unmeasured-elbow refusal go too.
            MetricSeries change;  change.key = QStringLiteral("leadElbowBendChange3d");  change.routeId = QStringLiteral("faceOn+dtl");
            std::vector<MetricSeries> s2{ sensor, change };
            std::vector<GateRecord> r2{ GateRecord{ QStringLiteral("body3d.leadElbowUnmeasured"),
                                                    QStringLiteral("leadElbowBendChange3d"), GateEffect::Refused,
                                                    {}, {}, {}, { QStringLiteral("leadElbowBendChange3d") } } };
            CHECK("(D6) a sensor elbow ⇒ the 3-D change and body3d.leadElbowUnmeasured go as well",
                  applyLeadElbowSensorPrecedence(s2, r2) == 1 && s2.size() == 1 && r2.empty());
        }
    }

    // ── 17. The face-on elbow's OCCLUSION gate (13 Nov 2026, plane stack D5 round 3) ──────────────
    //
    // A lead shoulder, elbow or wrist on the head (within 0.25 × the address shoulder width of the head
    // keypoints' centre), or one the pose model scored below 0.40 on its RAW confidence, is not seen. The
    // fixture is §16's arm (shoulders 200 px apart, so the head radius is 50 px), with the head keypoints
    // (nose, eyes, ears) set per frame: FAR (centre 180 px from the lead shoulder) or ON the lead shoulder
    // (centre 28 px from it — Jess's chin over her hidden shoulder).
    {
        std::printf("=== 17) leadArmFlexion: the occlusion gate ===\n");
        const auto armPose = [](double betaDeg) {
            Upper p = sigmaUpperPose();
            const double b = betaDeg / 57.29577951308232;
            p.lSh = QPointF(0.40, 0.30);
            p.lEl = QPointF(0.40, 0.40);
            p.lWr = QPointF(0.40 + 0.10 * std::sin(b), 0.40 + 0.10 * std::cos(b));
            return p;
        };
        const auto setHead = [](PoseFrame2D &f, QPointF c) {
            const QPointF off[5] = { { 0, 0 }, { -0.01, -0.01 }, { 0.01, -0.01 }, { -0.02, 0 }, { 0.02, 0 } };
            for (int k = 0; k < 5; ++k) { f.kp[size_t(k)] = c + off[k]; f.conf[size_t(k)] = 0.9f; }
        };
        const QPointF headFar(0.50, 0.15), headOnShoulder(0.42, 0.28);
        const std::vector<PhaseEvent> ph{
            { Phase::Address, 20000,  1.f, SegmentRole::Unknown },
            { Phase::Impact,  80000,  1.f, SegmentRole::Unknown },
            { Phase::Finish,  200000, 1.f, SegmentRole::Unknown } };
        UpperBodyConfig base;  base.addrWindowUs = 30000;
        // Six address frames (0–50 ms), then fifteen test frames (60–200 ms). `onHead(i)` says which test
        // frames put the head on the lead shoulder.
        const auto run = [&](double testBeta, const std::function<bool(int)> &onHead) {
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 15; ++i) poses.push_back(armPose(testBeta));
            PoseTrack2D t = trackOf(poses);
            for (size_t i = 0; i < t.frames.size(); ++i)
                setHead(t.frames[i], (i >= 6 && onHead(int(i) - 6)) ? headOnShoulder : headFar);
            return trackUpperBody(t, kW, kH, true, 20000, base);
        };
        const auto hiddenRecord = [&](const UpperBodyResult &r, const char *phase) -> GateRecord {
            for (const GateRecord &g : upperBodyGates(r, ph))
                if (g.gate == QStringLiteral("body.leadArmHidden") && g.subject == QStringLiteral("leadArmFlexion")
                    && g.effect == GateEffect::Refused
                    && g.evidence.value(QStringLiteral("phase")).toString() == QLatin1String(phase))
                    return g;
            return GateRecord{};
        };

        // (a) A visible straight arm, the head well away: read, ~0°, nothing hidden.
        {
            const UpperBodyResult r = run(0.0, [](int) { return false; });
            double v = -1.0;
            CHECK("a visible straight arm reads 0° at impact",
                  seriesAt(buildUpperBodySeries(r, ph), "leadArmFlexion", Phase::Impact, v) && near(v, 0.0, 1e-6));
            CHECK("…nothing hidden, no body.leadArmHidden record",
                  r.hiddenLeadElbow.empty() && hiddenRecord(r, "P7").gate.isEmpty());
        }
        // (b) The lead shoulder ON THE HEAD on every test frame: refused, with the record and its reason.
        {
            const UpperBodyResult r = run(40.0, [](int) { return true; });
            double v = 0.0;
            const auto series = buildUpperBodySeries(r, ph);
            const MetricSeries *m = findSeriesU(series, "leadArmFlexion");
            bool invalid = m && m->valid.size() == m->t_us.size();
            if (invalid) for (size_t i = 6; i < m->t_us.size(); ++i) invalid = invalid && m->valid[i] == 0u;
            CHECK("the lead shoulder on the head: no bend at impact", !seriesAt(series, "leadArmFlexion", Phase::Impact, v));
            CHECK("…every such frame hidden (not the length gate), invalid and never bridged",
                  invalid && r.hiddenLeadElbow.size() == 15 && r.gatedLeadElbow.empty()
                      && (r.hiddenLeadElbowWhy.front() & hidden_why::kShoulderOnHead));
            const GateRecord rec = hiddenRecord(r, "P7");
            CHECK("…a Refused body.leadArmHidden record at impact, code shoulderOnHead",
                  rec.gate == QStringLiteral("body.leadArmHidden")
                      && rec.evidence.value(QStringLiteral("code")).toString() == QLatin1String("shoulderOnHead")
                      && rec.evidence.value(QStringLiteral("shoulderOnHead")).toInt() > 0);
            const std::vector<GateRecord> recs = upperBodyGates(r, ph);
            const GateRecord *primary = pinpoint::gates::primaryRecord(recs, QStringLiteral("leadArmFlexion"));
            const pinpoint::gates::GateEntry *entry = pinpoint::gates::gateEntry(QStringLiteral("body.leadArmHidden"));
            CHECK("…it is the primary record for leadArmFlexion",
                  primary && primary->gate == QStringLiteral("body.leadArmHidden") && primary->effect == GateEffect::Refused);
            if (primary && entry) {
                const QString text = pinpoint::gates::evidenceText(*entry, primary->evidence);
                std::printf("    %s\n", qPrintable(text));
                CHECK("…its sentence says the shoulder was behind the head and fills every figure",
                      text.contains(QStringLiteral("behind the head")) && !text.contains(QLatin1Char('{')));
            }
        }
        // (c) A PREDICTED wrist whose RAW score is 0.3: the smoother lifts it to 0.5, which passes the
        //     detector gate, but the model never saw it. Refused. At a raw 0.45 it reads.
        for (const float rawWrist : { 0.3f, 0.45f }) {
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 15; ++i) poses.push_back(armPose(20.0));
            PoseTrack2D t = trackOf(poses);
            for (PoseFrame2D &f : t.frames) setHead(f, headFar);
            t.smoothed = t.frames;                                     // the smoother's track, parallel
            for (size_t i = 6; i < t.frames.size(); ++i) {
                t.frames[i].conf[size_t(kLWr)]   = rawWrist;           // the model's own score
                t.smoothed[i].conf[size_t(kLWr)] = std::max(rawWrist, 0.5f);   // pose_smoother's floor
            }
            const UpperBodyResult r = trackUpperBody(t, kW, kH, true, 20000, base);
            double v = 0.0;
            const bool read = seriesAt(buildUpperBodySeries(r, ph), "leadArmFlexion", Phase::Impact, v);
            if (rawWrist < 0.4f) {
                CHECK("a predicted wrist with a raw score of 0.3 is refused (its smoothed 0.5 does not count)",
                      !read && r.hiddenLeadElbow.size() == 15 && (r.hiddenLeadElbowWhy.front() & hidden_why::kLowConfidence)
                          && hiddenRecord(r, "P7").evidence.value(QStringLiteral("code")).toString() == QLatin1String("lowConfidence"));
            } else {
                CHECK("…and one scored 0.45 reads (20°)", read && near(v, 20.0, 1e-9) && r.hiddenLeadElbow.empty());
            }
        }
        // (d) THE WINDOW RULE: impact reads, but the lead shoulder goes behind the head from +40 to +100 ms
        //     (120–180 ms). The frames left in [impact, impact + 100 ms] stop at +30 ms, so 70 ms is unread —
        //     past the 30 ms limit — and the span gets a Refused record naming the gap. No P7 record.
        {
            const UpperBodyResult r = run(25.0, [](int i) { const int ms = 60 + 10 * i; return ms >= 120 && ms <= 180; });
            const GateRecord span = hiddenRecord(r, "afterImpact");
            double v = 0.0;
            CHECK("impact reads (25°)", seriesAt(buildUpperBodySeries(r, ph), "leadArmFlexion", Phase::Impact, v) && near(v, 25.0, 1e-9));
            CHECK("…no Refused record at impact", hiddenRecord(r, "P7").gate.isEmpty());
            CHECK("…a Refused record for the 100 ms after impact: 70 ms unread, over the 30 ms limit",
                  span.gate == QStringLiteral("body.leadArmHidden")
                      && near(span.evidence.value(QStringLiteral("gapMs")).toDouble(), 70.0, 1e-9)
                      && near(span.evidence.value(QStringLiteral("maxGapMs")).toDouble(), 30.0, 1e-9));
            // Two hidden frames (+40, +50 ms) leave 30 ms unread: covered, no span record.
            const UpperBodyResult twoHidden = run(25.0, [](int i) { const int ms = 60 + 10 * i; return ms == 120 || ms == 130; });
            CHECK("two hidden frames leave 30 ms unread: covered, no span record", hiddenRecord(twoHidden, "afterImpact").gate.isEmpty());
        }
        // (d2) ONE GATE NAMES THE REFUSAL (review, round 3): the span after impact loses one frame to the
        //      length band (+30 ms: a 0.30 × forearm) and six to the head (+40 … +90 ms). The span record
        //      comes from the gate that refused the most frames there, body.leadArmHidden, and it is the
        //      primary record — not "arm pointing at the camera" because that gate's records fired first.
        {
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 15; ++i) poses.push_back(armPose(25.0));
            Upper shortForearm = armPose(25.0);
            shortForearm.lWr = QPointF(0.40 + 0.03 * std::sin(25.0 / 57.29577951308232),
                                       0.40 + 0.03 * std::cos(25.0 / 57.29577951308232));
            poses[6 + 5] = shortForearm;                                   // 110 ms = +30 ms
            PoseTrack2D t = trackOf(poses);
            for (size_t i = 0; i < t.frames.size(); ++i) {
                const int ms = int(i) * 10;
                setHead(t.frames[i], (ms >= 120 && ms <= 170) ? headOnShoulder : headFar);
            }
            const UpperBodyResult r = trackUpperBody(t, kW, kH, true, 20000, base);
            const std::vector<GateRecord> recs = upperBodyGates(r, ph);
            int spanHidden = 0, spanLines = 0;
            for (const GateRecord &g : recs)
                if (g.effect == GateEffect::Refused && g.evidence.value(QStringLiteral("phase")).toString() == QLatin1String("afterImpact")) {
                    if (g.gate == QStringLiteral("body.leadArmHidden")) ++spanHidden;
                    if (g.gate == QStringLiteral("body.leadElbowLines")) ++spanLines;
                }
            const GateRecord *primary = pinpoint::gates::primaryRecord(recs, QStringLiteral("leadArmFlexion"));
            CHECK("six hidden + one foreshortened frame: hidden 6, gated 1",
                  r.hiddenLeadElbow.size() == 6 && r.gatedLeadElbow.size() == 1);
            CHECK("…ONE span record, from body.leadArmHidden (the gate with the most frames there)",
                  spanHidden == 1 && spanLines == 0);
            CHECK("…and it is the primary record for leadArmFlexion",
                  primary && primary->gate == QStringLiteral("body.leadArmHidden") && primary->effect == GateEffect::Refused);
        }
        // (d3) THE RAW-CONFIDENCE FALLBACK IS COUNTED, NOT SILENT: a smoothed track whose raw track is not
        //      parallel (a hand-built pair) is judged on the smoothed scores, and the result says on how many
        //      frames, for UpperBodyMetricsStage's once-per-swing warning. A parallel pair reports none.
        {
            std::vector<Upper> poses(6, armPose(10.0));
            for (int i = 0; i < 15; ++i) poses.push_back(armPose(20.0));
            PoseTrack2D t = trackOf(poses);
            for (PoseFrame2D &f : t.frames) setHead(f, headFar);
            t.smoothed = t.frames;
            for (size_t i = 6; i < t.frames.size(); ++i) {
                t.frames[i].conf[size_t(kLWr)]   = 0.3f;     // the model's own score
                t.smoothed[i].conf[size_t(kLWr)] = 0.5f;     // the smoother's floor
            }
            const UpperBodyResult parallel = trackUpperBody(t, kW, kH, true, 20000, base);
            t.frames.pop_back();                             // no longer parallel
            const UpperBodyResult skewed = trackUpperBody(t, kW, kH, true, 20000, base);
            double v = 0.0;
            CHECK("a parallel raw track: no fallback, the 0.3 wrist refused",
                  parallel.rawConfFallbackFrames == 0 && parallel.hiddenLeadElbow.size() == 15);
            CHECK("a non-parallel raw track: every frame counted as a fallback (21), and the rule reads the smoothed 0.5",
                  skewed.rawConfFallbackFrames == 21 && skewed.hiddenLeadElbow.empty()
                      && seriesAt(buildUpperBodySeries(skewed, ph), "leadArmFlexion", Phase::Impact, v));
        }
        // (e) The knobs are sweepable; the defaults are 0.25 and 0.40.
        {
            QVariantMap ov;
            ov.insert(QStringLiteral("upperBody.headRadiusRatio"), 0.1);
            ov.insert(QStringLiteral("upperBody.minRawConf"), 0.2);
            const UpperBodyConfig c = UpperBodyConfig::fromOverrides(ov);
            CHECK("upperBody.headRadiusRatio / minRawConf override", c.headRadiusRatio == 0.1 && c.minRawConf == 0.2);
            CHECK("defaults 0.25 / 0.40", UpperBodyConfig().headRadiusRatio == 0.25 && UpperBodyConfig().minRawConf == 0.40);
        }
    }

    std::printf(g_fail == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
