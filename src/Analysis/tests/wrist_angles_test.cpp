// Standalone characterization + property test for wrist_angles.h.
// Run via CTest (src/Analysis/tests/CMakeLists.txt):
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
//   cmake --build build/analyzer-tests && ctest --test-dir build/analyzer-tests --output-on-failure
//
// Verifies the decomposition MATH (axial isolation, magnitude, singularity) with hard
// asserts, against the hardware-locked imu_calibration anatomical frame
// (Z=flexion, Y=long/axial, X=deviation — see wrist_angles.h).

#include "../wrist_angles.h"

#include <QQuaternion>
#include <QVector3D>
#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static QVector3D X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);

static QQuaternion R(const QVector3D &axis, float deg) { return QQuaternion::fromAxisAndAngle(axis, deg); }

#define CHECK_NEAR(label, got, want, tolDeg)                                            \
    do {                                                                               \
        double g = radToDeg(got), w = (want);                                          \
        bool ok = std::abs(g - w) <= (tolDeg);                                         \
        std::printf("  [%s] %-30s got %7.2f°  want %7.2f°  (tol %.1f)\n",              \
                    ok ? "PASS" : "FAIL", label, g, w, double(tolDeg));                \
        if (!ok) ++g_fail;                                                             \
    } while (0)

#define REPORT(label, feR, rudR)                                                       \
    std::printf("  [DIAG] %-34s feRad=%7.2f°  rudRad=%7.2f°\n", label, radToDeg(feR), radToDeg(rudR))

int main()
{
    const QQuaternion I;   // identity = neutral Address reference
    std::printf("=== wrist_angles.h characterization ===\n\n");

    std::printf("-- A. identity --\n");
    {
        WristAngles w = wristFlexExtDeviation(I);
        CHECK_NEAR("fe(identity)", w.feRad, 0.0, 0.3);
        CHECK_NEAR("rud(identity)", w.rudRad, 0.0, 0.3);
        ForearmElbow e = forearmPronElbowFlex(I);
        CHECK_NEAR("pron(identity)", e.pronRad, 0.0, 0.3);
        CHECK_NEAR("flex(identity)", e.flexRad, 0.0, 0.3);
    }

    std::printf("\n-- B. hand axial (Y) twist must NOT leak into FE/RUD --\n");
    {
        WristAngles w = wristFlexExtDeviation(R(Y, 35));   // pure forearm/hand roll
        CHECK_NEAR("fe(axial 35)",  w.feRad,  0.0, 0.5);
        CHECK_NEAR("rud(axial 35)", w.rudRad, 0.0, 0.5);
    }

    std::printf("\n-- C. axial invariance: FE/RUD unchanged when axial roll is added --\n");
    {
        const QQuaternion base = (R(X, 25) * R(Z, 15)).normalized();   // flexion + deviation, no Y twist
        WristAngles w0 = wristFlexExtDeviation(base);
        for (float a : {-40.f, 40.f, 80.f}) {
            WristAngles wa = wristFlexExtDeviation((base * R(Y, a)).normalized());
            CHECK_NEAR("fe  invariance",  wa.feRad,  radToDeg(w0.feRad),  0.6);
            CHECK_NEAR("rud invariance",  wa.rudRad, radToDeg(w0.rudRad), 0.6);
        }
    }

    std::printf("\n-- D. physical-axis mapping (cal frame: flexion about Z, deviation about X) --\n");
    {
        WristAngles wz = wristFlexExtDeviation(R(Z, 20));  // physical FLEXION (bow)
        CHECK_NEAR("flexion Rz(20) -> fe",  wz.feRad,  20.0, 0.5);
        CHECK_NEAR("flexion Rz(20) -> rud", wz.rudRad,  0.0, 0.5);
        WristAngles wx = wristFlexExtDeviation(R(X, 20));  // physical DEVIATION (ulnar)
        CHECK_NEAR("deviation Rx(20) -> rud", wx.rudRad, 20.0, 0.5);
        CHECK_NEAR("deviation Rx(20) -> fe",  wx.feRad,   0.0, 0.5);
    }

    std::printf("\n-- E. combined flexion+deviation is cross-talk-free (ZXY: Rz·Rx·Ry) --\n");
    {
        WristAngles wc = wristFlexExtDeviation((R(Z, 30) * R(X, 20)).normalized());
        CHECK_NEAR("combined -> fe",  wc.feRad,  30.0, 0.5);
        CHECK_NEAR("combined -> rud", wc.rudRad, 20.0, 0.5);
        // flexion + deviation + axial roll all at once → axial must still drop out.
        WristAngles wa = wristFlexExtDeviation((R(Z, 30) * R(X, 20) * R(Y, 50)).normalized());
        CHECK_NEAR("flex+dev+axial -> fe",  wa.feRad,  30.0, 0.8);
        CHECK_NEAR("flex+dev+axial -> rud", wa.rudRad, 20.0, 0.8);
    }

    std::printf("\n-- F. forearm pronation + elbow flexion + 180° singularity --\n");
    {
        ForearmElbow ep = forearmPronElbowFlex(R(Y, 40));   // pure long-axis twist
        CHECK_NEAR("pron(Ry 40)", ep.pronRad, 40.0, 0.5);
        CHECK_NEAR("flex(Ry 40)", ep.flexRad,  0.0, 0.5);
        ForearmElbow ef = forearmPronElbowFlex(R(X, 90));   // pure elbow flexion
        CHECK_NEAR("flex(Rx 90)", ef.flexRad, 90.0, 0.5);
        CHECK_NEAR("pron(Rx 90)", ef.pronRad,  0.0, 0.5);
        ForearmElbow es = forearmPronElbowFlex(R(X, 175));  // near the 180° singularity
        CHECK_NEAR("flex(Rx 175) [singular]", es.flexRad, 175.0, 1.0);
        if (std::isnan(es.flexRad) || std::isnan(es.pronRad)) { std::printf("  [FAIL] NaN at singularity\n"); ++g_fail; }
    }

    std::printf("\n-- G. leftArm is a no-op (lead/left arm needs no mirror — hardware-locked) --\n");
    {
        WristAngles wr = wristFlexExtDeviation(R(Z, 20), /*leftArm=*/false);
        WristAngles wl = wristFlexExtDeviation(R(Z, 20), /*leftArm=*/true);
        CHECK_NEAR("fe  leftArm no-op", wl.feRad, radToDeg(wr.feRad), 0.3);
        ForearmElbow er = forearmPronElbowFlex(R(Y, 30), false);
        ForearmElbow el = forearmPronElbowFlex(R(Y, 30), true);
        CHECK_NEAR("pron leftArm no-op", el.pronRad, radToDeg(er.pronRad), 0.3);
    }

    std::printf("\n=== %s (%d assert failures) ===\n", g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
