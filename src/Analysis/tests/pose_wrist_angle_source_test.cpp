// Standalone test for the WB4 pose-derived wrist-angle source (src/Analysis/
// pose_wrist_angle_source): apparent camera-plane lead-wrist FE/RUD from a
// synthetic pose track, recovered through the windowed-median sampler; the
// left-handed image mirror; gap handling; and the default-OFF flag. Pure — no
// OpenCV, no fixture. Own main()/check() macros.
//
//   cmake --build build/analyzer-tests --target pose_wrist_angle_source_test
//   ctest --test-dir build/analyzer-tests -R pose_wrist_angle_source --output-on-failure

#include "../pose_wrist_angle_source.h"
#include "../wrist_angle_sampler.h"
#include "../hand_axis.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static constexpr int W = 1920, H = 1080;
static constexpr double kPi = 3.14159265358979323846;

// Populate one arm's forearm + hand so the apparent FE = feDeg and apparent
// RUD = rudDeg (see the geometry note in the .cpp). Forearm points straight up
// (px angle −90°); the hand axis sits at (−90 + feDeg); the knuckle line at rudDeg.
static void setArm(PoseFrame2D &f, int elbow, int wrist, int base,
                   double feDeg, double rudDeg, float conf)
{
    const double a = (-90.0 + feDeg) * kPi / 180.0;   // hand-axis px angle
    const double r = rudDeg * kPi / 180.0;            // knuckle-line px angle
    f.kp[elbow] = QPointF(0.5, 0.6);
    f.kp[wrist] = QPointF(0.5, 0.6 - 1.0 / H);        // forearm px = (0,-1) ⇒ −90°
    f.conf[elbow] = conf; f.conf[wrist] = conf;
    // hand root + middle-MCP (FE), index + pinky MCP (RUD).
    f.kp[base]                    = QPointF(0.5, 0.5);
    f.kp[base + kHandMiddleMcp]   = QPointF(0.5 + std::cos(a) / W, 0.5 + std::sin(a) / H);
    f.kp[base + kHandIndexMcp]    = QPointF(0.4, 0.5);
    f.kp[base + kHandPinkyMcp]    = QPointF(0.4 + std::cos(r) / W, 0.5 + std::sin(r) / H);
    f.conf[base]                  = conf;
    f.conf[base + kHandMiddleMcp] = conf;
    f.conf[base + kHandIndexMcp]  = conf;
    f.conf[base + kHandPinkyMcp]  = conf;
}

static double feAt(int i)  { return 10.0 + 1.0 * i; }
static double rudAt(int i) { return -20.0 + 0.5 * i; }

// 41 frames @ 5 ms. Both arms carry the SAME geometry so the handedness mirror
// can be checked by swapping which side is "lead". gapLo..gapHi frames get
// low-confidence hands (⇒ available=false samples).
static PoseTrack2D buildTrack(int gapLo = 1000, int gapHi = -1)
{
    PoseTrack2D t;
    for (int i = 0; i <= 40; ++i) {
        PoseFrame2D f;
        f.t_us = int64_t(i) * 5000;
        const float conf = (i >= gapLo && i <= gapHi) ? 0.1f : 0.9f;
        setArm(f, 7, 9, kLeftHandFirstKp,  feAt(i), rudAt(i), conf);   // left arm
        setArm(f, 8, 10, kRightHandFirstKp, feAt(i), rudAt(i), conf);  // right arm (identical)
        t.frames.push_back(f);
    }
    return t;
}

static std::vector<PhaseEvent> phases()
{
    // Address(0)→P1 @frame3, Top(2)→P4 @frame20, Impact(5)→P7 @frame37.
    auto ev = [](Phase p, int64_t t) { PhaseEvent e; e.phase = p; e.t_us = t; e.conf = 1.0f; return e; };
    return { ev(Phase::Address, 15000), ev(Phase::Top, 100000), ev(Phase::Impact, 185000) };
}

int main()
{
    std::printf("pose_wrist_angle_source_test\n");

    // Default config is DARK.
    check(!PoseWristAngleConfig{}.enabled, "config default enabled == false (dark)");

    const std::vector<PhaseEvent> ph = phases();
    PoseWristAngleConfig cfg; cfg.enabled = true;

    // ── right-handed: recovered apparent FE/RUD track the ramp centres ──────────
    {
        const PoseTrack2D t = buildTrack();
        const PoseWristAngleSource src(t, ph, /*handedness=*/1, W, H, cfg);
        const PpWristAngleSet set = WristAngleSampler::sample(src);

        const PpWristAngleCell &feTop = set.cell(PpJointDof::LeadWristFlexExt, PpSwingPosition::P4);
        const PpWristAngleCell &feImp = set.cell(PpJointDof::LeadWristFlexExt, PpSwingPosition::P7);
        const PpWristAngleCell &rudTop = set.cell(PpJointDof::LeadWristRadUln, PpSwingPosition::P4);
        check(feTop.available() && near(feTop.valueDeg, feAt(20), 0.3), "RH: apparent FE @Top ≈ ramp (30°)");
        check(feImp.available() && near(feImp.valueDeg, feAt(37), 0.3), "RH: apparent FE @Impact ≈ ramp (47°)");
        check(rudTop.available() && near(rudTop.valueDeg, rudAt(20), 0.3), "RH: apparent RUD @Top ≈ ramp (−10°)");
        // Low confidence — the apparent-angle penalty (×0.5) is baked in.
        check(feTop.confidence > 0.f && feTop.confidence < 0.9f, "RH: source carries reduced confidence");
    }

    // ── handedness mirror: left-handed lead → both apparent angles negate ───────
    {
        const PoseTrack2D t = buildTrack();
        const PoseWristAngleSource src(t, ph, /*handedness=*/2, W, H, cfg);   // left-handed
        const PpWristAngleSet set = WristAngleSampler::sample(src);
        const PpWristAngleCell &feTop  = set.cell(PpJointDof::LeadWristFlexExt, PpSwingPosition::P4);
        const PpWristAngleCell &rudTop = set.cell(PpJointDof::LeadWristRadUln, PpSwingPosition::P4);
        check(feTop.available() && near(feTop.valueDeg, -feAt(20), 0.3), "LH: apparent FE @Top mirrored (−30°)");
        check(rudTop.available() && near(rudTop.valueDeg, -rudAt(20), 0.3), "LH: apparent RUD @Top mirrored (+10°)");
    }

    // ── gap handling: low-confidence hands around Top ⇒ Gap cell ────────────────
    {
        const PoseTrack2D t = buildTrack(/*gapLo=*/16, /*gapHi=*/24);
        const PoseWristAngleSource src(t, ph, 1, W, H, cfg);
        const PpWristAngleSet set = WristAngleSampler::sample(src);
        const PpWristAngleCell &feTop = set.cell(PpJointDof::LeadWristFlexExt, PpSwingPosition::P4);
        const PpWristAngleCell &feImp = set.cell(PpJointDof::LeadWristFlexExt, PpSwingPosition::P7);
        check(feTop.status == PpCellStatus::Gap, "gap: low-conf window ⇒ Gap (no fabricated value)");
        check(feImp.available(), "gap: an unaffected position stays Ok");
    }

    // ── empty / no-hands track ⇒ no series, every cell Gap ──────────────────────
    {
        PoseTrack2D empty;
        const PoseWristAngleSource src(empty, ph, 1, W, H, cfg);
        const PpWristAngleSet set = WristAngleSampler::sample(src);
        check(set.cell(PpJointDof::LeadWristFlexExt, PpSwingPosition::P1).status == PpCellStatus::Gap,
              "empty track ⇒ Gap");
    }

    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
