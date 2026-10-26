// Characterization of the IMU→scene viz binding chains (Track B, Phase 2.1).
//
// Pins the ACTUAL current avatar math BEFORE any constant is refolded in 2.2:
//   * worldToScene() basis change (viz_frame.h) — Rx(-90°) world↔scene.
//   * ArmVizView per-segment world orientation  W = R0 · q_anat · rollFix
//     (ArmVizView.qml: leftRestQuat/rightRestQuat=R0, restRollDeg=-99 → rollFix).
//   * The explicit decomposition R0 = worldToScene() · restOffset (now in ArmVizView,
//     Phase 2.2) reproduces the legacy hand-tuned R0 — i.e. the avatar is unchanged.
//     (The left-handed R0 IS the pure basis change.)
//   * D. The trunk on BodyVizView (trunk design §6.7): the pelvis/thorax q_anat → the avatar's
//     bones (viz_frame.h trunkAvatarBones, what useTrunkOverride draws). Pins the trunk world →
//     avatar map (Rx(−90°) AND Ry(180°): the avatar faces the camera), every bone's GLB rest
//     rotation kept, the turn's direction per hand, the forward bend's direction (the basis change
//     alone bends the avatar BACKWARD — asserted, as the trap), the spine's share of the twist, the
//     planted feet, and a frozen golden for an address-bend-plus-turn.
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
// The trunk golden (D7), frozen from the first run of trunkAvatarBones.
static const QQuaternion GOLD_HIPS  (0.89240f,  0.23912f, -0.36964f, 0.09905f);
static const QQuaternion GOLD_SPINE (0.99163f, -0.06030f, -0.10219f, 0.05099f);
static const QQuaternion GOLD_SPINE1(0.99139f,  0.00000f, -0.12051f, 0.05125f);
static const QQuaternion GOLD_SPINE2(0.98758f,  0.05708f, -0.13117f, 0.06497f);
static const QVector3D   GOLD_HEAD  (-0.2773f, 1.5222f, 0.0670f);

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

    // -- D. The trunk on BodyVizView (trunk design §6.7, imu_frame_contract.md §5/§6) --
    // The binding BodyVizView's useTrunkOverride draws (viz_frame.h trunkAvatarBones, called through
    // BodyPoseAdapter.trunkBones). The chain below rebuilds the avatar's node hierarchy from the same
    // offsets, so every assertion is about where the BONES and LANDMARKS end up, not about the maths.
    std::printf("\n-- D. trunk: q_anat → BodyVizView bones (useTrunkOverride) --\n");
    {
        using namespace pinpoint::viz::bodyviz;
        struct P { QQuaternion r; QVector3D p; };
        auto child = [](const P &parent, const QVector3D &pos, const QQuaternion &rot) {
            return P{ (parent.r * rot).normalized(), parent.p + parent.r.rotatedVector(pos) };
        };
        // The landmarks of one pose: the trunk joints, both shoulders (the shoulder nodes' offsets
        // from Spine2), both hip joints, knees and ankles.
        struct L { P hips, spine, spine1, spine2, neck, head; QVector3D lSh, rSh, lHip, rHip, lKnee, rKnee, lAnkle, rAnkle; };
        auto landmarks = [&](const TrunkAvatarBones &b) {
            L l;
            l.hips   = P{ b.hips, kHipsPos };
            l.spine  = child(l.hips,   kSpinePos,  b.spine);
            l.spine1 = child(l.spine,  kSpine1Pos, b.spine1);
            l.spine2 = child(l.spine1, kSpine2Pos, b.spine2);
            l.neck   = child(l.spine2, kNeckPos,   QQuaternion());
            l.head   = child(l.neck,   kHeadPos,   QQuaternion());
            l.lSh = l.spine2.p + l.spine2.r.rotatedVector(QVector3D( 0.0611f, 0.0911f, 0.0076f));
            l.rSh = l.spine2.p + l.spine2.r.rotatedVector(QVector3D(-0.0611f, 0.0911f, 0.0076f));
            const P lUp = child(l.hips, kLeftUpLegPos,  b.leftUpLeg);
            const P rUp = child(l.hips, kRightUpLegPos, b.rightUpLeg);
            const P lLg = child(lUp, kLegPos, b.leftLeg);
            const P rLg = child(rUp, kLegPos, b.rightLeg);
            l.lHip = lUp.p; l.rHip = rUp.p; l.lKnee = lLg.p; l.rKnee = rLg.p;
            l.lAnkle = lLg.p + lLg.r.rotatedVector(QVector3D(0, 0.4210f, 0));
            l.rAnkle = rLg.p + rLg.r.rotatedVector(QVector3D(0, 0.4210f, 0));
            return l;
        };
        const QQuaternion I;
        const TrunkAvatarBones restB = trunkAvatarBones(I, I);
        const L rest = landmarks(restB);
        auto deg = [](const QQuaternion &a, const QQuaternion &b) {
            const QQuaternion d = (a.conjugated() * b).normalized();
            return 2.0 * std::atan2(QVector3D(d.x(), d.y(), d.z()).length(), std::abs(d.scalar())) * 180.0 / M_PI;
        };
        const QQuaternion T = trunkWorldToAvatar();
        auto D = [&](const QQuaternion &q) { return (T * q * T.conjugated()).normalized(); };

        // D1. The trunk world → the avatar: right → scene −X (the avatar faces the camera), anterior → +Z.
        checkVec("trunk +X (right) → scene −X", T.rotatedVector(QVector3D(1, 0, 0)), QVector3D(-1, 0, 0), 1e-4f);
        checkVec("trunk +Y (anterior) → +Z",    T.rotatedVector(QVector3D(0, 1, 0)), QVector3D(0, 0, 1), 1e-4f);
        checkVec("trunk +Z (up) → scene +Y",    T.rotatedVector(QVector3D(0, 0, 1)), QVector3D(0, 1, 0), 1e-4f);

        // D2. Stand tall (identity): every bone at its GLB rest rotation — the rest offsets are kept.
        checkQuat("rest: hips",   restB.hips,   kHipsRest,   1e-5f);
        checkQuat("rest: spine",  restB.spine,  kSpineRest.normalized(),  1e-5f);
        checkQuat("rest: spine1", restB.spine1, kSpine1Rest, 1e-5f);
        checkQuat("rest: spine2", restB.spine2, kSpine2Rest.normalized(), 1e-5f);
        checkQuat("rest: leftUpLeg",  restB.leftUpLeg,  kLeftUpLegRest,  1e-4f);
        checkQuat("rest: rightUpLeg", restB.rightUpLeg, kRightUpLegRest, 1e-4f);
        checkQuat("rest: leftLeg",    restB.leftLeg,    I, 1e-4f);
        std::printf("  [DIAG] rest head (%.4f,%.4f,%.4f)\n", rest.head.p.x(), rest.head.p.y(), rest.head.p.z());
        checkVec("rest: head (BodyVizView)", rest.head.p, QVector3D(0.0f, 1.6009f, -0.0041f), 2e-3f);

        // D3. A right-hander turned AWAY: hips 45°, chest 90° (the guide's top of the turn).
        {
            const QQuaternion qp = trunkGolferTurn(45, true), qt = trunkGolferTurn(90, true);
            checkQuat("RH away 45° = Rz(−45°)", qp, QQuaternion::fromAxisAndAngle(0, 0, 1, -45), 1e-5f);
            const TrunkAvatarBones b = trunkAvatarBones(qp, qt);
            const L l = landmarks(b);
            checkQuat("RH away: hips = Ry(−45°) in scene", b.hips, QQuaternion::fromAxisAndAngle(0, 1, 0, -45), 1e-4f);
            // Spine2 carries the thorax exactly, on top of its rest world orientation.
            checkQuat("RH away: spine2 world = D·rest", l.spine2.r, (D(qt) * rest.spine2.r).normalized(), 1e-4f);
            // The landmarks: the lead (left) shoulder comes FORWARD and across — toward the camera, +Z —
            // and the shoulder line turns 90°; the hip line 45°. The avatar turns away.
            checkVec("RH away: shoulder line (L−R)", (l.lSh - l.rSh).normalized(), QVector3D(0, 0, 1), 2e-2f);
            checkVec("RH away: hip line (L−R)", (l.lHip - l.rHip).normalized(),
                     QVector3D(float(M_SQRT1_2), 0, float(M_SQRT1_2)), 2e-2f);
            // The spine twists along its length: Spine is turned less than Spine1, Spine1 less than Spine2.
            const double a0 = deg(rest.spine.r,  l.spine.r)  - 45, a1 = deg(rest.spine1.r, l.spine1.r) - 45;
            std::printf("  [DIAG] spine twist beyond the hips: spine %.1f°, spine1 %.1f°, spine2 45°\n", a0, a1);
            checkNear("RH away: spine twist share", a0, 45.0 * 0.1173 / (0.1173 + 0.1346 + 0.15056), 0.3);
            checkNear("RH away: spine1 twist share", a1, 45.0 * (0.1173 + 0.1346) / (0.1173 + 0.1346 + 0.15056), 0.3);
            // The feet stay planted.
            checkVec("RH away: left ankle planted",  l.lAnkle, rest.lAnkle, 1.5e-2f);
            checkVec("RH away: right ankle planted", l.rAnkle, rest.rAnkle, 1.5e-2f);
        }
        // D4. A left-hander turned away: the mirror — the lead (RIGHT) shoulder comes forward.
        {
            const L l = landmarks(trunkAvatarBones(trunkGolferTurn(45, false), trunkGolferTurn(90, false)));
            checkVec("LH away: shoulder line (L−R)", (l.lSh - l.rSh).normalized(), QVector3D(0, 0, -1), 2e-2f);
        }
        // D5. THE TRAP: a 30° forward bend (rotation about the golfer's right, −30°). The head goes
        // FORWARD, toward the camera (+Z), and down. The basis change alone sends it BACKWARD.
        {
            const QQuaternion bend = QQuaternion::fromAxisAndAngle(1, 0, 0, -30);
            const L l = landmarks(trunkAvatarBones(bend, bend));
            std::printf("  [DIAG] bend: head (%.3f,%.3f,%.3f) rest (%.3f,%.3f,%.3f)\n",
                        l.head.p.x(), l.head.p.y(), l.head.p.z(), rest.head.p.x(), rest.head.p.y(), rest.head.p.z());
            checkNear("bend 30°: head forward (Δz)", l.head.p.z() - rest.head.p.z(), 0.30, 0.03);
            checkNear("bend 30°: head down (Δy)",    l.head.p.y() - rest.head.p.y(), -0.08, 0.03);
            checkNear("bend 30°: head centred (x)",  l.head.p.x(), 0.0, 1e-3);
            // The basis change alone (Rx(−90°) conjugation, no Ry(180°), no rest offset) on the hips:
            const P trapHips{ worldOrientationToScene(bend), kHipsPos };
            const P trapHead = child(child(child(child(child(trapHips, kSpinePos, kSpineRest), kSpine1Pos, kSpine1Rest),
                                                 kSpine2Pos, kSpine2Rest), kNeckPos, I), kHeadPos, I);
            std::printf("  [DIAG] basis-change-only bend: head z %.3f (rest %.3f) — backward\n",
                        trapHead.p.z(), rest.head.p.z());
            checkNear("trap: basis-only bends BACKWARD (Δz)", trapHead.p.z() - rest.head.p.z(), -0.30, 0.03);
            checkVec("bend: ankles planted", l.lAnkle, rest.lAnkle, 1.5e-2f);
        }
        // D6. A sensor twisted in its pocket reads a SIDE bend while the golfer bends straight forward
        // (§6.7, "take your address"): the thorax rolled 20° toward the golfer's right (about +Y,
        // anterior) leans the avatar to ITS right — scene −X — with the hips square.
        {
            const L l = landmarks(trunkAvatarBones(I, QQuaternion::fromAxisAndAngle(0, 1, 0, 20)));
            checkNear("side bend: head to the avatar's right (Δx)", l.head.p.x() - rest.head.p.x(), -0.13, 0.03);
            checkVec("side bend: hips square", (l.lHip - l.rHip).normalized(), QVector3D(1, 0, 0), 1e-3f);
        }
        // D7. GOLDEN (frozen): address bend 30° then turned away, right-hander — pelvis Rz(−45°)·Rx(−30°),
        // thorax Rz(−90°)·Rx(−30°). The bones' parent-local rotations and the head as drawn.
        {
            const QQuaternion bend = QQuaternion::fromAxisAndAngle(1, 0, 0, -30);
            const QQuaternion qp = (trunkGolferTurn(45, true) * bend).normalized();
            const QQuaternion qt = (trunkGolferTurn(90, true) * bend).normalized();
            const TrunkAvatarBones b = trunkAvatarBones(qp, qt);
            const L l = landmarks(b);
            auto pq = [](const char *n, const QQuaternion &q) {
                std::printf("  [DIAG] %-10s (%.5f,%.5f,%.5f,%.5f)\n", n, q.scalar(), q.x(), q.y(), q.z());
            };
            pq("hips", b.hips); pq("spine", b.spine); pq("spine1", b.spine1); pq("spine2", b.spine2);
            pq("leftUpLeg", b.leftUpLeg); pq("leftLeg", b.leftLeg);
            std::printf("  [DIAG] head (%.4f,%.4f,%.4f)\n", l.head.p.x(), l.head.p.y(), l.head.p.z());
            checkQuat("golden: hips world = D(pelvis)",   l.hips.r,   D(qp), 1e-4f);
            checkQuat("golden: spine2 world = D(thorax)·rest", l.spine2.r, (D(qt) * rest.spine2.r).normalized(), 1e-4f);
            checkQuat("golden: hips",   b.hips,   GOLD_HIPS,   1e-3f);
            checkQuat("golden: spine",  b.spine,  GOLD_SPINE,  1e-3f);
            checkQuat("golden: spine1", b.spine1, GOLD_SPINE1, 1e-3f);
            checkQuat("golden: spine2", b.spine2, GOLD_SPINE2, 1e-3f);
            checkVec("golden: head",    l.head.p, GOLD_HEAD,   2e-3f);
        }
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
