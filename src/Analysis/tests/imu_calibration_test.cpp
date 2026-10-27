// Standalone characterization + property test for imu_calibration.h (the A·q·M
// anatomical solve) and the end-to-end calibration→wrist-angle math.
//
// Run via CTest (src/Analysis/tests/CMakeLists.txt):
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.1/gcc_64
//   cmake --build build/analyzer-tests && ctest --test-dir build/analyzer-tests --output-on-failure
//
// This is the Track-A keystone golden (docs/implementation/imu_rearchitecture.md §6 Phase 0.1): it
// freezes the calibration solve and the composed q_anat→angle numbers so any later
// frame-contract change that silently perturbs them fails loudly.
//
// SCOPE NOTE: the offline scored path composes q_anat as
//   (alignA * qRaw * mountM).normalized()        — imu_vision_fuser.cpp:72
// We freeze that EXACT expression here (section E), but compiling imu_vision_fuser.cpp
// itself drags in swing_window.h (Buffer). The literal live-vs-offline two-site
// agreement assertion (imu_instance.cpp:213 vs imu_vision_fuser.cpp:72) belongs with
// the Phase 1.3 unification, where both sites are touched.

#include "../../IMU/imu_calibration.h"
#include "../../IMU/trunk_ceremony.h"
#include "../../Wrist/wrist_angles.h"

#include <QQuaternion>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace imu_calibration;
using pinpoint::analysis::WristAngles;
using pinpoint::analysis::wristFlexExtDeviation;
using pinpoint::analysis::wristRel;
using pinpoint::analysis::radToDeg;

static int g_fail = 0;

// --- assert helpers (sign-robust where rotations are compared) ----------------

static float quatDist(const QQuaternion &a, const QQuaternion &b)
{
    return std::max(std::max(std::abs(a.scalar() - b.scalar()), std::abs(a.x() - b.x())),
                    std::max(std::abs(a.y() - b.y()), std::abs(a.z() - b.z())));
}
// q and -q are the same rotation: accept whichever sign is closer.
static bool quatNear(const QQuaternion &g, const QQuaternion &w, float tol)
{
    const QQuaternion negW(-w.scalar(), -w.x(), -w.y(), -w.z());
    return std::min(quatDist(g, w), quatDist(g, negW)) <= tol;
}

static void checkQuat(const char *label, const QQuaternion &got, const QQuaternion &want, float tol)
{
    const bool ok = quatNear(got, want, tol);
    std::printf("  [%s] %-34s got (%.4f,%.4f,%.4f,%.4f)  want (%.4f,%.4f,%.4f,%.4f)\n",
                ok ? "PASS" : "FAIL", label,
                got.scalar(), got.x(), got.y(), got.z(),
                want.scalar(), want.x(), want.y(), want.z());
    if (!ok) ++g_fail;
}

static void checkNear(const char *label, double got, double want, double tol)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  [%s] %-34s got %9.4f  want %9.4f  (tol %.4g)\n",
                ok ? "PASS" : "FAIL", label, got, want, tol);
    if (!ok) ++g_fail;
}

static void checkBool(const char *label, bool got, bool want)
{
    const bool ok = (got == want);
    std::printf("  [%s] %-34s got %s  want %s\n",
                ok ? "PASS" : "FAIL", label, got ? "true" : "false", want ? "true" : "false");
    if (!ok) ++g_fail;
}

int main()
{
    const QQuaternion I;
    const QVector3D X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
    std::printf("=== imu_calibration.h characterization & keystone golden ===\n\n");

    // -- A. nominal mount constants are frozen (a frame change must not silently move them).
    std::printf("-- A. nominal mount constants --\n");
    checkQuat("nominalArmMount",  nominalArmMount(),  QQuaternion(0.5f, -0.5f, -0.5f, -0.5f), 1e-3f);
    checkQuat("nominalHandMount", nominalHandMount(), QQuaternion(0.4388f, 0.6054f, 0.4965f, -0.4409f), 1e-3f);

    // -- B. solveSegment reproduces nominalArmMount from the canonical arm-down inputs.
    //    Arm hanging: gravity-DOWN is sensor +X, long axis (distal) is sensor +X, flexion
    //    axis is sensor +Z (docs/reference/imu_axis_reference.md + imu_calibration.h:54-57). Hand-
    //    verified: this yields M=(0.5,-0.5,-0.5,-0.5)=nominalArmMount exactly.
    std::printf("\n-- B. solveSegment(arm-down) == nominalArmMount --\n");
    {
        const Alignment a = solveSegment(/*refRaw=*/I, /*down=*/X, /*long=*/X, /*flex=*/Z);
        checkQuat("M == nominalArmMount", a.M, nominalArmMount(), 1e-4f);
        checkNear("axisAngleDeg",         a.axisAngleDeg, 90.0, 0.5);
        checkBool("valid",                a.valid, true);

        // The opposite-sign down vector flips e_y → a DIFFERENT (mirrored) mount. Freeze it
        // too so the sign-handling branch (imu_calibration.h:127) can't silently change.
        const Alignment flip = solveSegment(I, -X, X, Z);
        checkQuat("M(down=-X) flipped",   flip.M, QQuaternion(0.5f, 0.5f, -0.5f, 0.5f), 1e-4f);
        checkBool("valid(flip)",          flip.valid, true);
    }

    // -- C. reference-pose identity: A·refRaw·M == I, by construction A=conj(refRaw·M).
    //    Holds for ANY refRaw, so this is a property test over several fixed orientations.
    std::printf("\n-- C. A·refRaw·M == I (reference-pose identity property) --\n");
    {
        const QQuaternion refs[] = {
            I,
            QQuaternion::fromAxisAndAngle(X, 37),
            QQuaternion::fromAxisAndAngle(Y, 110),
            QQuaternion::fromAxisAndAngle(QVector3D(1, 2, 3), 64),
            (QQuaternion::fromAxisAndAngle(Z, 25) * QQuaternion::fromAxisAndAngle(X, 80)).normalized(),
        };
        int idx = 0;
        for (const QQuaternion &refRaw : refs) {
            const Alignment a = solveSegment(refRaw, X, X, Z);
            const QQuaternion qAnatRef = (a.A * refRaw * a.M).normalized();
            char lbl[40];
            std::snprintf(lbl, sizeof lbl, "A·refRaw·M == I  [#%d]", idx++);
            checkQuat(lbl, qAnatRef, I, 1e-4f);
        }
    }

    // -- D. validity gate: axisAngleDeg must land in [60,120] (imu_calibration.h:138).
    std::printf("\n-- D. validity gate (axis orthogonality) --\n");
    {
        checkBool("parallel long∥flex → invalid", solveSegment(I, X, X, X).valid, false);
        checkBool("orthogonal long⊥flex → valid", solveSegment(I, X, X, Z).valid, true);
        // 45° apart: below the 60° floor → rejected.
        const QVector3D diag = QVector3D(1, 0, 1).normalized();
        const Alignment a45 = solveSegment(I, X, X, diag);
        checkNear("axisAngleDeg(45° case)", a45.axisAngleDeg, 45.0, 0.5);
        checkBool("45° apart → invalid",    a45.valid, false);
    }

    // -- E. KEYSTONE: a fixed (A,M,q_raw) pose → frozen q_anat (via the exact offline
    //    expression) → frozen wrist flexion/deviation. Inputs are deterministic; outputs
    //    are captured from current code. A change here means the calibration→angle math
    //    moved — exactly what Track A exists to catch.
    std::printf("\n-- E. keystone: (A,M,q_raw) → q_anat → wrist angles --\n");
    {
        const QQuaternion mFore = nominalArmMount();
        const QQuaternion mHand = nominalHandMount();
        const QQuaternion aFore = I, aHand = I;                 // world→anat (frame-dependent; fixed here)
        const QQuaternion qRawFore = QQuaternion::fromAxisAndAngle(X, 10);
        const QQuaternion qRawHand =
            (QQuaternion::fromAxisAndAngle(X, 10) * QQuaternion::fromAxisAndAngle(Z, 25)).normalized();

        // Exactly imu_vision_fuser.cpp:72.
        const QQuaternion qAnatFore = (aFore * qRawFore * mFore).normalized();
        const QQuaternion qAnatHand = (aHand * qRawHand * mHand).normalized();
        const QQuaternion qWristRel = wristRel(qAnatFore, qAnatHand, /*addr=*/I);
        const WristAngles wa = wristFlexExtDeviation(qWristRel);

        std::printf("  [DIAG] qAnatFore = (%.5f, %.5f, %.5f, %.5f)\n",
                    qAnatFore.scalar(), qAnatFore.x(), qAnatFore.y(), qAnatFore.z());
        std::printf("  [DIAG] qAnatHand = (%.5f, %.5f, %.5f, %.5f)\n",
                    qAnatHand.scalar(), qAnatHand.x(), qAnatHand.y(), qAnatHand.z());
        std::printf("  [DIAG] qWristRel = (%.5f, %.5f, %.5f, %.5f)\n",
                    qWristRel.scalar(), qWristRel.x(), qWristRel.y(), qWristRel.z());
        std::printf("  [DIAG] fe = %.4f°   rud = %.4f°\n", radToDeg(wa.feRad), radToDeg(wa.rudRad));

        // GOLDEN VALUES — captured from current code (frozen reference). A diff here
        // means the A·q·M composition or the wrist-angle extraction moved.
        checkQuat("qAnatFore frozen", qAnatFore, QQuaternion(0.54168f, -0.45452f, -0.45452f, -0.54168f), 1e-3f);
        checkQuat("qAnatHand frozen", qAnatHand, QQuaternion(0.47970f,  0.52741f,  0.64267f, -0.28054f), 1e-3f);
        checkQuat("qWristRel frozen", qWristRel, QQuaternion(-0.12003f, 0.02809f,  0.97935f,  0.16027f), 1e-3f);
        checkNear("fe frozen (deg)",  radToDeg(wa.feRad),  -5.6377, 0.02);
        checkNear("rud frozen (deg)", radToDeg(wa.rudRad), 17.8888, 0.02);
    }

    // -- F. toAnatomical() — the unified A·q·M helper (Phase 1.3) — matches the manual
    //    expression both call sites used (imu_instance.cpp:213, imu_vision_fuser.cpp:72).
    std::printf("\n-- F. toAnatomical() unified helper agreement --\n");
    {
        const QQuaternion A  = QQuaternion::fromAxisAndAngle(X, 37);
        const QQuaternion M  = nominalArmMount();
        const QQuaternion qs[] = {
            I,
            QQuaternion::fromAxisAndAngle(Z, 25),
            (QQuaternion::fromAxisAndAngle(Y, 70) * QQuaternion::fromAxisAndAngle(X, 12)).normalized(),
        };
        int idx = 0;
        for (const QQuaternion &qRaw : qs) {
            const QQuaternion viaHelper = toAnatomical(A, qRaw, M);
            const QQuaternion viaManual = (A * qRaw * M).normalized();
            char lbl[44];
            std::snprintf(lbl, sizeof lbl, "helper == (A·q·M).norm  [#%d]", idx++);
            checkQuat(lbl, viaHelper, viaManual, 1e-6f);
        }
        // The keystone qAnatFore (section E) is exactly toAnatomical(I, qRawFore, armMount).
        const QQuaternion qRawFore = QQuaternion::fromAxisAndAngle(X, 10);
        checkQuat("keystone via helper", toAnatomical(I, qRawFore, nominalArmMount()),
                  QQuaternion(0.54168f, -0.45452f, -0.45452f, -0.54168f), 1e-3f);
    }

    // -- G. solveTrunkSegment — the trunk's Z-up, stance-headed solve (imu_frame_contract.md §5
    //    "Trunk segments"). The sensor at its nominal mount, "USB end down, label facing out": segment
    //    +Z (superior) = sensor +Y, +Y (anterior) = sensor −Z, +X (right) = sensor +X, so
    //    M = fromAxes(X, −Z, Y) = Rx(−90°). Standing tall the true segment is the identity in the
    //    stance frame, so the fused raw quaternion is W·M⁻¹ for the filter's world W.
    std::printf("\n-- G. solveTrunkSegment (trunk stand tall) --\n");
    {
        const QQuaternion mNom = QQuaternion::fromAxisAndAngle(X, -90);
        const QQuaternion eskf = QQuaternion::fromAxisAndAngle(X, 180);   // §7: ESKF world = Rx(180°)·Madgwick
        const auto window = [&](const QQuaternion &W, const QQuaternion &M) {
            StandTallWindow w;
            w.meanQuat   = (W * M.conjugated()).normalized();
            w.meanAccelG = M.rotatedVector(Z);     // gravity-up in the sensor frame: M·ẑ_anat
            w.samplesReceived = w.samplesExpected = 400;        // 2 s at 200 Hz
            return w;
        };

        // G1. nominal mount, identity Madgwick world → M = Rx(−90°), A = I, q_anat = I.
        {
            const TrunkAlignment t = solveTrunkSegment(window(I, mNom), TrunkSensor::Pelvis);
            checkQuat("nominal: M == Rx(-90)", t.M, QQuaternion(0.70711f, -0.70711f, 0.0f, 0.0f), 1e-4f);
            checkQuat("nominal: A == I", t.A, I, 1e-4f);
            checkBool("nominal: valid", t.valid, true);
            checkBool("nominal: no failure text", t.record.failure.isEmpty(), true);
            checkNear("nominal: long axis from up", t.record.longAxisFromUpDeg, 0.0, 0.01);
            checkNear("nominal: face from horizontal", t.record.faceFromHorizontalDeg, 0.0, 0.01);
            checkBool("nominal: tier δ0", t.record.deltaTier == QStringLiteral("δ0"), true);
            checkNear("nominal: σ_δ", t.record.sigmaDeltaDeg, 10.0, 1e-6);
        }
        // G2. an arbitrary fusion heading is absorbed by A (the stance frame faces where the golfer
        //     did), and an ESKF world (Rx(180°)) solves to the SAME q_anat for every later pose.
        {
            const QQuaternion W = QQuaternion::fromAxisAndAngle(Z, 73);
            const TrunkAlignment m = solveTrunkSegment(window(W, mNom), TrunkSensor::Thorax);
            const TrunkAlignment e = solveTrunkSegment(window(eskf * W, mNom), TrunkSensor::Thorax);
            checkQuat("Madgwick yawed: A == Rz(-73)", m.A, QQuaternion::fromAxisAndAngle(Z, -73), 1e-4f);
            checkQuat("Madgwick yawed: q_anat(stand tall) == I",
                      toAnatomical(m.A, window(W, mNom).meanQuat, m.M), I, 1e-4f);
            checkQuat("ESKF: same M as Madgwick", e.M, m.M, 1e-4f);
            checkQuat("ESKF: q_anat(stand tall) == I",
                      toAnatomical(e.A, window(eskf * W, mNom).meanQuat, e.M), I, 1e-4f);
            // A later pose: bent 30° toward the ball, turned 40°.
            const QQuaternion pose = (QQuaternion::fromAxisAndAngle(Z, 40) * QQuaternion::fromAxisAndAngle(X, -30)).normalized();
            const QQuaternion rawM = (W * pose * mNom.conjugated()).normalized();
            const QQuaternion rawE = (eskf * rawM).normalized();
            checkQuat("Madgwick: q_anat(pose) == the pose", toAnatomical(m.A, rawM, m.M), pose, 1e-4f);
            checkQuat("ESKF: q_anat(pose) == the pose", toAnatomical(e.A, rawE, e.M), pose, 1e-4f);
        }
        // G3. a tilted mount (a 20° kyphotic lean of the pocket, 8° of roll) is absorbed exactly:
        //     the solved frame is the true one, so a bent pose reads back unchanged.
        {
            const QQuaternion mountInv = (QQuaternion::fromAxisAndAngle(X, 20) * QQuaternion::fromAxisAndAngle(Y, 8)
                                          * mNom.conjugated()).normalized();
            const QQuaternion mTrue = mountInv.conjugated();
            const TrunkAlignment t = solveTrunkSegment(window(I, mTrue), TrunkSensor::Thorax);
            checkBool("tilted 20°: valid", t.valid, true);
            checkQuat("tilted 20°: M == the true mount", t.M, mTrue, 1e-4f);
            const QQuaternion pose = QQuaternion::fromAxisAndAngle(X, -40);
            checkQuat("tilted 20°: q_anat(bent 40°) == the pose",
                      toAnatomical(t.A, pose * mTrue.conjugated(), t.M), pose, 1e-4f);
        }
        // G4. gates — each failure names the sensor and the cause.
        {
            // Upside down (USB end up — sensor +Y points away from it): the long axis is 180° from up.
            const QQuaternion upside = (mNom * QQuaternion::fromAxisAndAngle(Y, 180)).normalized();
            const TrunkAlignment u = solveTrunkSegment(window(I, upside), TrunkSensor::Pelvis);
            checkBool("USB up: rejected", u.valid, false);
            checkBool("USB up: long-axis gate fails", u.record.longAxisOk, false);
            checkNear("USB up: long axis from up", u.record.longAxisFromUpDeg, 180.0, 0.01);
            checkBool("USB up: \"Hip sensor: turn it USB-down, label out\"",
                      u.record.failure == QStringLiteral("Hip sensor: turn it USB-down, label out"), true);

            // Lying flat, label to the sky: the face normal is vertical.
            StandTallWindow flat = window(I, mNom);
            flat.meanAccelG = Z;
            const TrunkAlignment f = solveTrunkSegment(flat, TrunkSensor::Thorax);
            checkBool("lying flat: rejected", f.valid, false);
            checkNear("lying flat: face from horizontal", f.record.faceFromHorizontalDeg, 90.0, 0.01);
            checkBool("lying flat: \"Chest sensor is lying flat …\"",
                      f.record.failure == QStringLiteral("Chest sensor is lying flat — wear it upright: USB down, label facing out"), true);

            // Moving: 2 °/s of mean gyro.
            StandTallWindow mv = window(I, mNom);
            mv.meanGyroDps = QVector3D(0.3f, -2.0f, 0.1f);
            const TrunkAlignment m = solveTrunkSegment(mv, TrunkSensor::Pelvis);
            checkBool("moving 2 °/s: rejected", m.valid, false);
            checkNear("moving 2 °/s: max |gyro|", m.record.maxAbsGyroDps, 2.0, 1e-5);
            checkBool("moving: \"Hip sensor moved — hold still and try again\"",
                      m.record.failure == QStringLiteral("Hip sensor moved — hold still and try again"), true);
            // Still, but under 0.5 °/s is a pass.
            mv.meanGyroDps = QVector3D(0.4f, -0.4f, 0.4f);
            checkBool("0.4 °/s bias: valid", solveTrunkSegment(mv, TrunkSensor::Pelvis).valid, true);

            // |f̄| 4 % off 1 g: outside the ±3 % gate seated on the W0 desk session (design §17.4).
            StandTallWindow heavy = window(I, mNom);
            heavy.meanAccelG *= 1.04f;
            const TrunkAlignment h = solveTrunkSegment(heavy, TrunkSensor::Thorax);
            checkBool("|f| = 1.04 g: rejected", h.valid, false);
            checkNear("|f| = 1.04 g: error %", h.record.gravityErrPct, 4.0, 1e-3);
            checkBool("|f|: \"Chest sensor moved …\"",
                      h.record.failure == QStringLiteral("Chest sensor moved — hold still and try again"), true);
            // The W0 unit that reads +1.74 % lying still (f4907bae), and 2.5 %: both pass the seated gate.
            StandTallWindow w0unit = window(I, mNom);
            w0unit.meanAccelG *= 1.0174f;
            checkBool("|f| = 1.0174 g (W0 unit): valid", solveTrunkSegment(w0unit, TrunkSensor::Pelvis).valid, true);
            w0unit = window(I, mNom);
            w0unit.meanAccelG *= 0.975f;
            checkBool("|f| = 0.975 g: valid", solveTrunkSegment(w0unit, TrunkSensor::Pelvis).valid, true);

            // A short window: 80 % of the expected samples.
            StandTallWindow shortW = window(I, mNom);
            shortW.samplesReceived = 320;
            const TrunkAlignment s = solveTrunkSegment(shortW, TrunkSensor::Thorax);
            checkBool("80 % samples: rejected", s.valid, false);
            checkNear("80 % samples: fraction", s.record.sampleFraction, 0.80, 1e-6);
            checkBool("samples: \"Chest sensor dropped samples …\"",
                      s.record.failure == QStringLiteral("Chest sensor dropped samples — move closer or check the battery"), true);

            // FACE-IN (label against the body, USB still down) is INVISIBLE to stand tall: it reads the
            // same gravity, solves, and passes every gate here, with the stance frame 180° about +Z.
            // The turns cannot see it either (§H); the first address does (design §6.3 "Face-in cannot
            // be caught by stand tall or the turns"). Pinned so nobody mistakes the pass for a check.
            const QQuaternion faceIn = (mNom * QQuaternion::fromAxisAndAngle(Z, 180)).normalized();
            const TrunkAlignment fi = solveTrunkSegment(window(I, faceIn), TrunkSensor::Thorax);
            checkBool("face-in: stand tall cannot see it (valid)", fi.valid, true);
            checkQuat("face-in: solved M is 180° about the spine from the truth", fi.M,
                      (faceIn * QQuaternion::fromAxisAndAngle(Z, 180)).normalized(), 1e-4f);
        }
    }

    // -- H. checkTrunkTurn — the ceremony's turn checks (trunk_imu_sequence_grf_design.md §6.3 steps 2
    //    "Turn away" and 3 "Turn through", §16.2 W2.1b). A synthetic upright body, built here from first
    //    principles: standing tall the true pelvis and thorax are the identity in the stance frame, and
    //    a standing turn is the segment rotated about vertical (or a tilted axis) by peak·p(t), with
    //    p(t) = sin²(2πt) over the step — away and back, twice. The hips turn half the chest (§6.7: hips
    //    about 45°, chest about 90°). Each sensor sits on its segment through a mount M_true (anat → sensor);
    //    the fusion reports q_raw = W·q_true·M_true⁻¹ for the filter's world W; the stand-tall solve sees
    //    q_true = I. Right-hander: away is −Z, through +Z; a left-hander the reverse
    //    (pinpoint_sign_conventions.md "Trunk IMU readings").
    std::printf("\n-- H. checkTrunkTurn (trunk turn away / turn through) --\n");
    {
        const QQuaternion mNom = QQuaternion::fromAxisAndAngle(X, -90);   // USB down, label out (§G)
        const QQuaternion eskf = QQuaternion::fromAxisAndAngle(X, 180);
        const QQuaternion W    = QQuaternion::fromAxisAndAngle(Z, 37);    // an arbitrary fusion heading
        // Realistic mounts: the belt pouch leans 12° on the sacrum; the vest pocket 20° on a kyphotic
        // back with 8° of roll. Stand tall absorbs both (§G3).
        const QQuaternion mHip   = (mNom * QQuaternion::fromAxisAndAngle(X, 12)).normalized();
        const QQuaternion mChest = (mNom * QQuaternion::fromAxisAndAngle(X, -20)
                                    * QQuaternion::fromAxisAndAngle(Y, 8)).normalized();
        const QQuaternion flip   = QQuaternion::fromAxisAndAngle(Y, 180);  // USB up (as §G4)
        constexpr int kN = 1600;                                            // 8 s at 200 Hz

        const auto standTall = [&](const QQuaternion &world, const QQuaternion &mStand, TrunkSensor who) {
            StandTallWindow w;
            w.meanQuat   = (world * mStand.conjugated()).normalized();
            w.meanAccelG = mStand.rotatedVector(Z);
            w.samplesReceived = w.samplesExpected = 400;
            return solveTrunkSegment(w, who);
        };
        // One sensor's step: stand tall on mStand, then the turn worn on mTurn (the same mount unless the
        // sensor was re-seated between the steps).
        const auto step = [&](const QQuaternion &world, const QQuaternion &mStand, const QQuaternion &mTurn,
                              TrunkSensor who, const QVector3D &axis, float peakDeg, int sign) {
            const TrunkAlignment t = standTall(world, mStand, who);
            TrunkTurnSeries s;
            s.A = t.A;
            s.M = t.M;
            s.qRaw.reserve(kN);
            for (int k = 0; k < kN; ++k) {
                const double sn = std::sin(2.0 * 3.14159265358979323846 * k / (kN - 1));
                const QQuaternion qTrue = QQuaternion::fromAxisAndAngle(axis, float(sign * peakDeg * sn * sn));
                s.qRaw.push_back((world * qTrue * mTurn.conjugated()).normalized());
            }
            return s;
        };
        const auto sgnOf = [](TrunkTurnDirection d, bool lh) { return (d == TrunkTurnDirection::Away ? -1 : 1) * (lh ? -1 : 1); };
        const auto msgIs = [](const char *label, const QString &got, const char *want) {
            const bool ok = got == QString::fromUtf8(want);
            std::printf("  [%s] %-34s got \"%s\"\n", ok ? "PASS" : "FAIL", label, got.toUtf8().constData());
            if (!ok) { std::printf("         want \"%s\"\n", want); ++g_fail; }
        };

        // H1. Correct mounts pass, away and through, both hands; one in an ESKF world.
        for (const bool lh : { false, true }) {
            for (const TrunkTurnDirection d : { TrunkTurnDirection::Away, TrunkTurnDirection::Through }) {
                const int sg = sgnOf(d, lh);
                const QQuaternion world = (lh && d == TrunkTurnDirection::Through) ? (eskf * W).normalized() : W;
                const TrunkTurnRecord r = checkTrunkTurn(step(world, mHip, mHip, TrunkSensor::Pelvis, Z, 45, sg),
                                                         step(world, mChest, mChest, TrunkSensor::Thorax, Z, 90, sg),
                                                         d, lh);
                char tag[24];
                std::snprintf(tag, sizeof tag, "%s %s", lh ? "LH" : "RH", d == TrunkTurnDirection::Away ? "away" : "through");
                char lbl[64];
                std::snprintf(lbl, sizeof lbl, "%s: valid", tag);                checkBool(lbl, r.valid, true);
                std::snprintf(lbl, sizeof lbl, "%s: no failure text", tag);      checkBool(lbl, r.failure.isEmpty(), true);
                std::snprintf(lbl, sizeof lbl, "%s: expected sign", tag);        checkNear(lbl, r.expectedSign, sg, 0);
                std::snprintf(lbl, sizeof lbl, "%s: chest range", tag);          checkNear(lbl, r.thorax.rangeDeg, 90.0, 0.05);
                std::snprintf(lbl, sizeof lbl, "%s: hip range", tag);            checkNear(lbl, r.pelvis.rangeDeg, 45.0, 0.05);
                std::snprintf(lbl, sizeof lbl, "%s: chest D13 turn signed", tag); checkNear(lbl, r.thorax.turnDeg, 90.0 * sg, 0.05);
                std::snprintf(lbl, sizeof lbl, "%s: chest S-I turn signed", tag); checkNear(lbl, r.thorax.siTurnDeg, 90.0 * sg, 0.05);
                std::snprintf(lbl, sizeof lbl, "%s: hip S-I turn signed", tag);  checkNear(lbl, r.pelvis.siTurnDeg, 45.0 * sg, 0.05);
                std::snprintf(lbl, sizeof lbl, "%s: chest axis from vertical", tag); checkNear(lbl, r.thorax.axisFromVerticalDeg, 0.0, 0.1);
                std::snprintf(lbl, sizeof lbl, "%s: hip axis from vertical", tag);   checkNear(lbl, r.pelvis.axisFromVerticalDeg, 0.0, 0.1);
                std::snprintf(lbl, sizeof lbl, "%s: chest over hips", tag);      checkNear(lbl, r.chestOverHips, 2.0, 0.01);
            }
        }

        // H2. A sensor upside down relative to its stand-tall solve (re-seated USB-up between the steps):
        //     its segment up points down, so the S-I turn — the KS rate's sign (D3) — reads backwards. The
        //     message names that sensor and the cause, as §6.3 words it.
        {
            const TrunkTurnRecord c = checkTrunkTurn(
                step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 45, -1),
                step(W, mChest, (mChest * flip).normalized(), TrunkSensor::Thorax, Z, 90, -1),
                TrunkTurnDirection::Away, false);
            checkBool("chest re-seated USB-up: rejected", c.valid, false);
            checkBool("  chest sense fails", c.thorax.senseOk, false);
            checkBool("  hip sense passes", c.pelvis.senseOk, true);
            checkNear("  chest S-I turn reads +90 (backwards)", c.thorax.siTurnDeg, 90.0, 0.05);
            checkBool("  not the golfer's wrong way", c.wrongWay, false);
            msgIs("  step message", c.failure,
                  "Chest sensor reads the turn backwards — check it sits USB-down, label facing out");
            msgIs("  chest's own message", c.thorax.failure,
                  "Chest sensor reads the turn backwards — check it sits USB-down, label facing out");
            msgIs("  cause: backwards (the routine goes back to stand tall)", c.cause, "backwards");

            const TrunkTurnRecord h = checkTrunkTurn(
                step(W, mHip, (mHip * flip).normalized(), TrunkSensor::Pelvis, Z, 45, -1),
                step(W, mChest, mChest, TrunkSensor::Thorax, Z, 90, -1),
                TrunkTurnDirection::Through, true);                       // LH through = −Z
            checkBool("LH, hip re-seated USB-up: rejected", h.valid, false);
            msgIs("  step message", h.failure,
                  "Hip sensor reads the turn backwards — check it sits USB-down, label facing out");

            const TrunkTurnRecord b = checkTrunkTurn(
                step(W, mHip, (mHip * flip).normalized(), TrunkSensor::Pelvis, Z, 45, -1),
                step(W, mChest, (mChest * flip).normalized(), TrunkSensor::Thorax, Z, 90, -1),
                TrunkTurnDirection::Away, false);
            msgIs("both re-seated USB-up", b.failure,
                  "Hip and chest sensors read the turn backwards — check they sit USB-down, label facing out");
            msgIs("  cause", b.cause, "backwards");

            // Upside down at stand tall AND the turns: stand tall rejects it (the long-axis gate, §G4), and
            // its solve absorbs the flip exactly, so the turns read it right. Pinned: the turn check is not
            // what catches this case.
            const QQuaternion upside = (mChest * flip).normalized();
            checkBool("USB-up throughout: stand tall rejects",
                      standTall(W, upside, TrunkSensor::Thorax).valid, false);
            const TrunkTurnRecord u = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 45, -1),
                                                     step(W, upside, upside, TrunkSensor::Thorax, Z, 90, -1),
                                                     TrunkTurnDirection::Away, false);
            checkBool("  …its solve reads the turn right", u.thorax.senseOk, true);
        }

        // H3. Swapped: the chest sensor in the belt pouch and the hip sensor in the vest.
        {
            const TrunkTurnRecord s = checkTrunkTurn(step(W, mChest, mChest, TrunkSensor::Pelvis, Z, 90, -1),
                                                     step(W, mHip, mHip, TrunkSensor::Thorax, Z, 45, -1),
                                                     TrunkTurnDirection::Away, false);
            checkBool("swapped: rejected", s.valid, false);
            checkBool("  order fails", s.orderOk, false);
            checkNear("  chest over hips", s.chestOverHips, 0.5, 0.01);
            msgIs("  step message", s.failure, "Hip and chest sensors look swapped");
            msgIs("  cause", s.cause, "swapped");
        }

        // H4. Too little turn.
        {
            const TrunkTurnRecord c = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 22, -1),
                                                     step(W, mChest, mChest, TrunkSensor::Thorax, Z, 45, -1),
                                                     TrunkTurnDirection::Away, false);
            checkBool("chest 45°: rejected", c.valid, false);
            checkBool("  chest range fails", c.thorax.rangeOk, false);
            msgIs("  step message", c.failure, "Chest turned too little — make a fuller backswing turn, then turn back to address");
            msgIs("  cause", c.cause, "range");

            const TrunkTurnRecord h = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 20, 1),
                                                     step(W, mChest, mChest, TrunkSensor::Thorax, Z, 90, 1),
                                                     TrunkTurnDirection::Through, false);
            checkBool("hips 20°: rejected", h.valid, false);
            checkBool("  hip range fails", h.pelvis.rangeOk, false);
            checkBool("  chest range passes", h.thorax.rangeOk, true);
            msgIs("  step message", h.failure, "Hips turned too little — let them turn with your chest");

            const TrunkTurnRecord n = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 5, 1),
                                                     step(W, mChest, mChest, TrunkSensor::Thorax, Z, 12, 1),
                                                     TrunkTurnDirection::Through, false);
            msgIs("barely turned (12°/5°)", n.failure,
                  "Hardly any turn — arms crossed, make a fuller follow-through turn, then turn back to address");
        }

        // H5. The turn about a tilted axis: 20° from vertical fails on both sensors; 10° passes.
        {
            const QVector3D tilt20(0.0f, float(std::sin(20.0 * 3.14159265358979323846 / 180.0)),
                                   float(std::cos(20.0 * 3.14159265358979323846 / 180.0)));
            const TrunkTurnRecord t = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, tilt20, 45, -1),
                                                     step(W, mChest, mChest, TrunkSensor::Thorax, tilt20, 90, -1),
                                                     TrunkTurnDirection::Away, false);
            checkBool("axis tilted 20°: rejected", t.valid, false);
            checkNear("  chest axis from vertical", t.thorax.axisFromVerticalDeg, 20.0, 0.1);
            checkNear("  hip axis from vertical", t.pelvis.axisFromVerticalDeg, 20.0, 0.1);
            checkBool("  chest axis fails", t.thorax.axisOk, false);
            msgIs("  step message", t.failure,
                  "Hip and chest sensors turned on a tilt — stand tall and turn level, without leaning");

            const QVector3D tilt10(float(std::sin(10.0 * 3.14159265358979323846 / 180.0)), 0.0f,
                                   float(std::cos(10.0 * 3.14159265358979323846 / 180.0)));
            const TrunkTurnRecord ok = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, tilt10, 45, -1),
                                                      step(W, mChest, mChest, TrunkSensor::Thorax, tilt10, 90, -1),
                                                      TrunkTurnDirection::Away, false);
            checkBool("axis tilted 10°: valid", ok.valid, true);
            checkNear("  chest axis from vertical", ok.thorax.axisFromVerticalDeg, 10.0, 0.1);

            // Only the chest on a tilt (a loose vest pocket): the message names the chest.
            const TrunkTurnRecord one = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 45, -1),
                                                       step(W, mChest, mChest, TrunkSensor::Thorax, tilt20, 90, -1),
                                                       TrunkTurnDirection::Away, false);
            msgIs("chest alone on a tilt", one.failure,
                  "Chest sensor turned on a tilt — stand tall and turn level, without leaning");
        }

        // H6. FACE-IN PASSES, by design (§6.3 "Face-in cannot be caught by stand tall or the turns"): it is
        //     a 180° error about the spine, and an upright turn is about that same axis. The first address
        //     is the check (W2.1d). Asserted so nobody mistakes the pass for a missed check.
        {
            const QQuaternion inHip   = (mHip * QQuaternion::fromAxisAndAngle(Z, 180)).normalized();
            const QQuaternion inChest = (mChest * QQuaternion::fromAxisAndAngle(Z, 180)).normalized();
            for (const TrunkTurnDirection d : { TrunkTurnDirection::Away, TrunkTurnDirection::Through }) {
                const int sg = sgnOf(d, false);
                const TrunkTurnRecord f = checkTrunkTurn(step(W, inHip, inHip, TrunkSensor::Pelvis, Z, 45, sg),
                                                         step(W, inChest, inChest, TrunkSensor::Thorax, Z, 90, sg),
                                                         d, false);
                checkBool(d == TrunkTurnDirection::Away ? "face-in both, away: PASSES (by design)"
                                                        : "face-in both, through: PASSES (by design)", f.valid, true);
                checkNear("  chest D13 turn", f.thorax.turnDeg, 90.0 * sg, 0.05);
                checkNear("  chest S-I turn", f.thorax.siTurnDeg, 90.0 * sg, 0.05);
            }
        }

        // H7. Mount yaw δ: the solve assumes δ = 0 (tier δ0), and the turn is δ-immune (§6.2).
        {
            const QQuaternion dHip   = (mHip * QQuaternion::fromAxisAndAngle(Z, -40)).normalized();
            const QQuaternion dChest = (mChest * QQuaternion::fromAxisAndAngle(Z, 25)).normalized();
            const TrunkTurnRecord r = checkTrunkTurn(step(W, dHip, dHip, TrunkSensor::Pelvis, Z, 45, 1),
                                                     step(W, dChest, dChest, TrunkSensor::Thorax, Z, 90, 1),
                                                     TrunkTurnDirection::Away, true);   // LH away = +Z
            checkBool("δ chest 25°, hips −40°: valid", r.valid, true);
            checkNear("  chest range", r.thorax.rangeDeg, 90.0, 0.05);
            checkNear("  hip range", r.pelvis.rangeDeg, 45.0, 0.05);
            checkBool("  no cause when valid", r.cause.isEmpty(), true);
        }

        // H8. The golfer turned the other way (or the hand is set wrong): both sensors agree, so neither
        //     is blamed.
        {
            const TrunkTurnRecord w = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 45, 1),
                                                     step(W, mChest, mChest, TrunkSensor::Thorax, Z, 90, 1),
                                                     TrunkTurnDirection::Away, false);
            checkBool("RH turned through when asked away: wrong way", w.wrongWay, true);
            checkBool("  rejected", w.valid, false);
            checkBool("  chest not blamed", w.thorax.failure.isEmpty(), true);
            // In golf terms (Mark, 27 Oct): the turn he made, the turn wanted, and the trail shoulder.
            msgIs("  step message", w.failure,
                  "That was a follow-through turn — make your backswing turn: right shoulder back, away from the target.");
            msgIs("  cause", w.cause, "wrongWay");
            const TrunkTurnRecord t = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 45, 1),
                                                     step(W, mChest, mChest, TrunkSensor::Thorax, Z, 90, 1),
                                                     TrunkTurnDirection::Through, true);   // LH through = −Z
            checkBool("LH made a backswing turn when asked follow-through: wrong way", t.wrongWay, true);
            msgIs("  step message", t.failure,
                  "That was a backswing turn — make your follow-through turn: left shoulder through, toward the target.");
            // The other two: a right-hander's backswing turn on the follow-through step, a left-hander's
            // follow-through turn on the backswing step — the trail shoulder named by the hand.
            const TrunkTurnRecord rt = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 45, -1),
                                                      step(W, mChest, mChest, TrunkSensor::Thorax, Z, 90, -1),
                                                      TrunkTurnDirection::Through, false);  // RH through = +Z
            checkBool("RH made a backswing turn when asked follow-through: wrong way", rt.wrongWay, true);
            msgIs("  step message", rt.failure,
                  "That was a backswing turn — make your follow-through turn: right shoulder through, toward the target.");
            const TrunkTurnRecord la = checkTrunkTurn(step(W, mHip, mHip, TrunkSensor::Pelvis, Z, 45, -1),
                                                      step(W, mChest, mChest, TrunkSensor::Thorax, Z, 90, -1),
                                                      TrunkTurnDirection::Away, true);      // LH away = +Z
            checkBool("LH made a follow-through turn when asked backswing: wrong way", la.wrongWay, true);
            msgIs("  step message", la.failure,
                  "That was a follow-through turn — make your backswing turn: left shoulder back, away from the target.");
            msgIs("  cause", la.cause, "wrongWay");
        }

        // H9. No data from a sensor.
        {
            TrunkTurnSeries empty;
            const TrunkTurnRecord e = checkTrunkTurn(empty, step(W, mChest, mChest, TrunkSensor::Thorax, Z, 90, -1),
                                                     TrunkTurnDirection::Away, false);
            checkBool("hip series empty: rejected", e.valid, false);
            msgIs("  step message", e.failure, "Hip sensor sent no data — check it is switched on and connected");
        }
    }

    // §I. The live trunk heading (trunk_angles::liveHeadingDeg — ImuInstance::trunkHeadingDeg, the
    //     trunk routine's turn start/return; design §16.2, 26 Oct). It must be checkTrunkTurn's D13
    //     turn, and "no reading" (NaN) whenever the quaternion cannot give one.
    {
        std::printf("\n§I live trunk heading\n");
        const QVector3D Z(0, 0, 1), X(1, 0, 0);
        checkNear("identity: heading 0", trunk_angles::liveHeadingDeg(QQuaternion()), 0.0, 1e-4);
        checkNear("yaw +30 about +Z", trunk_angles::liveHeadingDeg(QQuaternion::fromAxisAndAngle(Z, 30)), 30.0, 1e-3);
        checkNear("yaw −90 about +Z (RH away)", trunk_angles::liveHeadingDeg(QQuaternion::fromAxisAndAngle(Z, -90)), -90.0, 1e-3);
        // The same as the D13 turn the check reads, through a forward bend (the address) too.
        const QQuaternion bent = QQuaternion::fromAxisAndAngle(X, 35);
        const QQuaternion turned = QQuaternion::fromAxisAndAngle(Z, -70) * bent;
        checkNear("bent 35° then turned −70°: the D13 turn",
                  trunk_angles::liveHeadingDeg(turned) - trunk_angles::liveHeadingDeg(bent),
                  trunk_angles::turnFromRad(turned, bent) * 57.29577951308232, 1e-3);
        // A quaternion off unit length (a cached product) reads the same.
        const QQuaternion q60 = QQuaternion::fromAxisAndAngle(Z, 60);
        checkNear("scaled ×3: unchanged",
                  trunk_angles::liveHeadingDeg(QQuaternion(3 * q60.scalar(), 3 * q60.x(), 3 * q60.y(), 3 * q60.z())),
                  60.0, 1e-3);
        // No reading: a filter fault's NaN / inf, a zero quaternion, the medio-lateral axis vertical.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        checkBool("NaN component: no reading", std::isnan(trunk_angles::liveHeadingDeg(QQuaternion(nan, 0, 0, 0))), true);
        checkBool("inf component: no reading", std::isnan(trunk_angles::liveHeadingDeg(QQuaternion(1, inf, 0, 0))), true);
        checkBool("zero quaternion: no reading", std::isnan(trunk_angles::liveHeadingDeg(QQuaternion(0, 0, 0, 0))), true);
        const QVector3D Y(0, 1, 0);
        checkBool("x̂ vertical (rolled 90°): no reading",
                  std::isnan(trunk_angles::liveHeadingDeg(QQuaternion::fromAxisAndAngle(Y, -90))), true);
        checkBool("x̂ 10° from vertical: a reading",
                  std::isfinite(trunk_angles::liveHeadingDeg(QQuaternion::fromAxisAndAngle(Y, -80))), true);
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
