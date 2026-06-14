// Characterization of the IMU→scene viz binding chains (Track B, Phase 2.1).
//
// Pins the ACTUAL current avatar math BEFORE any constant is refolded in 2.2:
//   * worldToScene() basis change (viz_frame.h) — Rx(-90°) world↔scene.
//   * ArmVizView per-segment world orientation  W = R0 · q_anat · rollFix
//     (ArmVizView.qml: leftRestQuat/rightRestQuat=R0, restRollDeg=-99 → rollFix).
//   * The explicit decomposition R0 = worldToScene() · restOffset (now in ArmVizView,
//     Phase 2.2) reproduces the legacy hand-tuned R0 — i.e. the avatar is unchanged.
//     (The left-handed R0 IS the pure basis change.)
//
// Qt's QQuaternion::operator* is the Hamilton product, identical to ArmVizView.qml's
// quatMul, so the chains are replicated faithfully. The frozen W / local-rotation
// values are the invariant: when 2.2 re-expresses R0 as worldToScene()·restOffset, the
// recomputed W must still equal these.

#include "../../Gui/cameras/viz_frame.h"

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

// ArmVizView R0, post-Phase-2.2: R0 = worldToScene() · restOffset (verbatim QML constants).
static const QQuaternion kLeftRestOffset (0.71184f, -0.67833f, 0.16334f, -0.08089f);
static const QQuaternion kRightRestOffset(1.0f, 0.0f, 0.0f, 0.0f);          // left-handed: basis change only
static const QQuaternion kLeftRest  = (worldToScene() * kLeftRestOffset).normalized();   // R0, right-handed lead
static const QQuaternion kRightRest = (worldToScene() * kRightRestOffset).normalized();  // R0, left-handed lead
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

    // -- B. R0 = worldToScene() · restOffset reproduces the original hand-tuned R0 --
    std::printf("\n-- B. R0 = worldToScene() · restOffset --\n");
    {
        // Right-handed lead: the composition reproduces the legacy hand-tuned leftRestQuat
        // (so the avatar is unchanged by the explicit factoring).
        checkQuat("R0(right) ≈ legacy leftRestQuat", kLeftRest,
                  QQuaternion(0.0237f, -0.983f, 0.0583f, -0.1727f), 1e-3f);
        // Left-handed lead: R0 IS the pure basis change (restOffset == identity).
        checkQuat("R0(left)  == worldToScene()", kRightRest, worldToScene(), 1e-4f);
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

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
