// ShaftTracker R6 kinematics (lead-arm → club, double-pendulum wrist-cock model)
// — standalone test. Pure: no Qt, no OpenCV.
//
// Validates the seed table reproduction, monotone interpolation, the trail→lead
// side flip at the release, σ_β widening at transition/finish, predictClubAngle
// composition, and the envelope-deviation metric. See
// docs/implementation/shaft_detection_skeleton_impl.md phase K2a.

#include "../shaft_kinematics.h"

#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis::kinematics;

static int g_fail = 0;

#define CHECK(label, cond)                                                              \
    do {                                                                                \
        const bool _c = (cond);                                                         \
        if (!_c) ++g_fail;                                                              \
        std::printf("  [%s] %s\n", _c ? "PASS" : "FAIL", label);                        \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                               \
    do {                                                                                \
        const double _g = (got), _w = (want), _t = (tol);                               \
        const bool _c = std::fabs(_g - _w) <= _t;                                       \
        if (!_c) ++g_fail;                                                              \
        std::printf("  [%s] %s got %.3f want %.3f (tol %.3f)\n",                        \
                    _c ? "PASS" : "FAIL", label, _g, _w, _t);                           \
    } while (0)

static double r2d(float r) { return r * 180.0 / 3.14159265358979323846; }

int main()
{
    std::printf("=== R6 seed table reproduced at knots ===\n");
    CHECK_NEAR("Address beta (deg)",   r2d(wristCockAt(0.00f).betaRad),  -5.0, 0.01);
    CHECK_NEAR("Top beta (deg)",       r2d(wristCockAt(0.50f).betaRad),  92.0, 0.01);
    CHECK_NEAR("Top sigma (deg)",      r2d(wristCockAt(0.50f).sigmaRad), 22.0, 0.01);
    CHECK_NEAR("Delivery beta (deg)",  r2d(wristCockAt(0.80f).betaRad),  48.0, 0.01);
    CHECK_NEAR("Finish beta (deg)",    r2d(wristCockAt(1.00f).betaRad), -95.0, 0.01);

    std::printf("=== monotone interpolation between knots ===\n");
    const double b55 = r2d(wristCockAt(0.55f).betaRad);    // Top(92) .. Transition(100)
    CHECK("0.55 between Top and Transition", b55 > 92.0 && b55 < 100.0);
    const double b425 = r2d(wristCockAt(0.425f).betaRad);  // MidBackswing(70) .. Top(92)
    CHECK("0.425 between MidBackswing and Top", b425 > 70.0 && b425 < 92.0);

    std::printf("=== side flips at the release (trail -> lead) ===\n");
    CHECK("Delivery is trail-side", wristCockAt(0.80f).side == +1);
    CHECK("Release is lead-side",   wristCockAt(0.95f).side == -1);
    const double bCross = r2d(wristCockAt(0.86f).betaRad); // near the +48 -> -8 crossover
    CHECK("near-collinear at the crossover (|beta| < 20)", std::fabs(bCross) < 20.0);

    std::printf("=== sigma_beta widens at transition and finish ===\n");
    CHECK("Transition wider than Top", wristCockAt(0.60f).sigmaRad > wristCockAt(0.50f).sigmaRad);
    CHECK("Finish wider than Impact",  wristCockAt(1.00f).sigmaRad > wristCockAt(0.90f).sigmaRad);

    std::printf("=== predictClubAngle composes phi_arm + chirality*beta ===\n");
    const float phiArm = static_cast<float>(30.0 * 3.14159265358979323846 / 180.0);
    CHECK_NEAR("Top, chirality +1 (deg)", r2d(predictClubAngle(phiArm, 0.50f, +1)), 30.0 + 92.0, 0.05);
    CHECK_NEAR("Top, chirality -1 (deg)", r2d(predictClubAngle(phiArm, 0.50f, -1)), 30.0 - 92.0, 0.05);

    std::printf("=== envelope deviation in sigma units ===\n");
    const ClubAnglePrediction top = wristCockAt(0.50f);
    const float pred = predictClubAngle(phiArm, 0.50f, +1);
    CHECK_NEAR("0 sigma at the prediction", envelopeDeviationSigma(pred, pred, top.sigmaRad), 0.0, 1e-4);
    const float off3 = pred + 3.f * top.sigmaRad;
    CHECK_NEAR("3 sigma at pred + 3 sigma", envelopeDeviationSigma(off3, pred, top.sigmaRad), 3.0, 0.02);

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASS",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
