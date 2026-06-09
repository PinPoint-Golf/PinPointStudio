// Characterization of the IMU→scene viz binding chains (Track B, Phase 2.1).
//
// Pins the ACTUAL current avatar math BEFORE any constant is refolded in 2.2:
//   * worldToScene() basis change (viz_frame.h) — Rx(-90°) world↔scene.
//   * ArmVizView per-segment world orientation  W = R0 · q_anat · rollFix
//     (ArmVizView.qml: leftRestQuat/rightRestQuat=R0, restRollDeg=-99 → rollFix).
//   * The exact decomposition R0 = worldToScene() · restOffset that 2.2 will make
//     explicit (and the fact that the left-handed R0 IS the pure basis change).
//   * ArmBoneController's bone-local chain parentWorld⁻¹ · boneWorld
//     (arm_bone_controller.cpp:186-191).
//
// Qt's QQuaternion::operator* is the Hamilton product, identical to ArmVizView.qml's
// quatMul, so the chains are replicated faithfully. The frozen W / local-rotation
// values are the invariant: when 2.2 re-expresses R0 as worldToScene()·restOffset, the
// recomputed W must still equal these.

#include "../../Gui/viz_frame.h"

#include <QQuaternion>
#include <QVector3D>
#include <cmath>
#include <cstdio>

using namespace pinpoint::viz;

static int g_fail = 0;

static void checkNear(const char *label, double got, double want, double tol)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  [%s] %-30s got %9.4f  want %9.4f  (tol %.4g)\n",
                ok ? "PASS" : "FAIL", label, got, want, tol);
    if (!ok) ++g_fail;
}
static void checkVec(const char *label, const QVector3D &g, const QVector3D &w, float tol)
{
    const bool ok = (g - w).length() <= tol;
    std::printf("  [%s] %-30s got (%.3f,%.3f,%.3f)  want (%.3f,%.3f,%.3f)\n",
                ok ? "PASS" : "FAIL", label, g.x(), g.y(), g.z(), w.x(), w.y(), w.z());
    if (!ok) ++g_fail;
}
static bool quatNear(const QQuaternion &g, const QQuaternion &w, float tol)
{
    auto d = [](const QQuaternion &a, const QQuaternion &b) {
        return std::max(std::max(std::abs(a.scalar()-b.scalar()), std::abs(a.x()-b.x())),
                        std::max(std::abs(a.y()-b.y()), std::abs(a.z()-b.z())));
    };
    return std::min(d(g, w), d(g, QQuaternion(-w.scalar(), -w.x(), -w.y(), -w.z()))) <= tol;
}
static void checkQuat(const char *label, const QQuaternion &g, const QQuaternion &w, float tol)
{
    const bool ok = quatNear(g, w, tol);
    std::printf("  [%s] %-30s got (%.4f,%.4f,%.4f,%.4f)  want (%.4f,%.4f,%.4f,%.4f)\n",
                ok ? "PASS" : "FAIL", label, g.scalar(), g.x(), g.y(), g.z(),
                w.scalar(), w.x(), w.y(), w.z());
    if (!ok) ++g_fail;
}

// ArmVizView constants (verbatim).
static const QQuaternion kLeftRest (0.0237f, -0.983f, 0.0583f, -0.1727f);  // R0, right-handed lead
static const QQuaternion kRightRest(0.7071f, -0.7071f, 0.0f, 0.0f);         // R0, left-handed lead
static QQuaternion rollFix()
{
    const double h = -99.0 * M_PI / 360.0;                  // restRollDeg=-99, half-angle
    return QQuaternion(float(std::cos(h)), 0.0f, float(std::sin(h)), 0.0f);  // about +Y
}
// ArmVizView: W = R0 · anat · rollFix  (Hamilton product == quatMul).
static QQuaternion segWorld(const QQuaternion &R0, const QQuaternion &anat)
{
    return R0 * anat * rollFix();
}

int main()
{
    std::printf("=== viz frame / binding-chain characterization ===\n\n");

    // -- A. worldToScene() basis change: world +Z→scene +Y, +Y→−Z, +X→+X --
    std::printf("-- A. worldToScene() = Rx(-90°) --\n");
    {
        const QQuaternion C = worldToScene();
        checkVec("world +Z → scene +Y", C.rotatedVector(QVector3D(0, 0, 1)), QVector3D(0, 1, 0), 1e-4f);
        checkVec("world +Y → scene -Z", C.rotatedVector(QVector3D(0, 1, 0)), QVector3D(0, 0, -1), 1e-4f);
        checkVec("world +X → scene +X", C.rotatedVector(QVector3D(1, 0, 0)), QVector3D(1, 0, 0), 1e-4f);
    }

    // -- B. R0 decomposition (the 2.2 target): R0 = worldToScene() · restOffset --
    std::printf("\n-- B. R0 = worldToScene() · restOffset --\n");
    {
        // Left-handed lead: R0 IS the pure basis change (no per-GLB rest offset).
        checkQuat("rightRest == worldToScene()", kRightRest, worldToScene(), 1e-3f);

        // Right-handed lead: restOffset = worldToScene()⁻¹ · leftRestQuat is non-trivial;
        // recomposing must reproduce leftRestQuat exactly (so 2.2 can store restOffset).
        const QQuaternion restOffset = worldToScene().conjugated() * kLeftRest;
        std::printf("  [DIAG] right-handed restOffset = (%.5f, %.5f, %.5f, %.5f)\n",
                    restOffset.scalar(), restOffset.x(), restOffset.y(), restOffset.z());
        checkQuat("worldToScene()·restOffset == leftRest", worldToScene() * restOffset, kLeftRest, 1e-4f);
    }

    // -- C. ArmVizView per-segment world orientation W = R0·anat·rollFix (FROZEN) --
    std::printf("\n-- C. ArmVizView W = R0·q_anat·rollFix --\n");
    {
        const QQuaternion rest;                                            // anat == I (arm-down)
        const QQuaternion flex = QQuaternion::fromAxisAndAngle(0, 0, 1, 30); // a flexed q_anat

        const QQuaternion wRestR = segWorld(kLeftRest,  rest);
        const QQuaternion wFlexR = segWorld(kLeftRest,  flex);
        const QQuaternion wRestL = segWorld(kRightRest, rest);
        std::printf("  [DIAG] W rest(right) = (%.5f,%.5f,%.5f,%.5f)\n",
                    wRestR.scalar(), wRestR.x(), wRestR.y(), wRestR.z());
        std::printf("  [DIAG] W flex(right) = (%.5f,%.5f,%.5f,%.5f)\n",
                    wFlexR.scalar(), wFlexR.x(), wFlexR.y(), wFlexR.z());
        std::printf("  [DIAG] W rest(left)  = (%.5f,%.5f,%.5f,%.5f)\n",
                    wRestL.scalar(), wRestL.x(), wRestL.y(), wRestL.z());
        // GOLDEN — frozen from current code. 2.2 must reproduce these after refolding
        // R0 = worldToScene()·restOffset.
        checkQuat("W rest (right-handed)", wRestR, QQuaternion( 0.05972f, -0.76973f,  0.01984f,  0.63532f), 1e-3f);
        checkQuat("W flex (right-handed)", wFlexR, QQuaternion( 0.28018f, -0.72904f,  0.15041f,  0.60618f), 1e-3f);
        checkQuat("W rest (left-handed)",  wRestL, QQuaternion( 0.45922f, -0.45922f, -0.53768f,  0.53768f), 1e-3f);
    }

    // -- D. ArmBoneController bone-local chain: localRot = parentWorld⁻¹ · boneWorld --
    std::printf("\n-- D. ArmBoneController parentWorld⁻¹·boneWorld --\n");
    {
        const QQuaternion shoulderRest = QQuaternion::fromAxisAndAngle(1, 0, 0, 5);
        const QQuaternion upperWorld   = QQuaternion::fromAxisAndAngle(0, 0, 1, 20);
        const QQuaternion foreWorld    = QQuaternion::fromAxisAndAngle(0, 0, 1, 35);
        const QQuaternion handWorld    = (QQuaternion::fromAxisAndAngle(0, 0, 1, 35)
                                          * QQuaternion::fromAxisAndAngle(1, 0, 0, 12)).normalized();
        const QQuaternion armLocal  = shoulderRest.inverted() * upperWorld;
        const QQuaternion foreLocal = upperWorld.inverted()   * foreWorld;
        const QQuaternion handLocal = foreWorld.inverted()    * handWorld;
        std::printf("  [DIAG] armLocal  = (%.5f,%.5f,%.5f,%.5f)\n", armLocal.scalar(), armLocal.x(), armLocal.y(), armLocal.z());
        std::printf("  [DIAG] foreLocal = (%.5f,%.5f,%.5f,%.5f)\n", foreLocal.scalar(), foreLocal.x(), foreLocal.y(), foreLocal.z());
        std::printf("  [DIAG] handLocal = (%.5f,%.5f,%.5f,%.5f)\n", handLocal.scalar(), handLocal.x(), handLocal.y(), handLocal.z());
        checkQuat("arm local",  armLocal,  QQuaternion(0.98387f, -0.04296f, 0.00757f, 0.17348f), 1e-3f);
        checkQuat("fore local", foreLocal, QQuaternion(0.99144f,  0.00000f, 0.00000f, 0.13053f), 1e-3f);
        checkQuat("hand local", handLocal, QQuaternion(0.99452f,  0.10453f, 0.00000f, 0.00000f), 1e-3f);
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
