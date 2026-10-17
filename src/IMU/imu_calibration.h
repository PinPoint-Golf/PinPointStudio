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

#pragma once

#include <QQuaternion>
#include <QString>
#include <QVector3D>
#include <algorithm>
#include <cmath>

#include "../Core/pp_tuned_constants.h"

// Anatomical calibration solve for an IMU strapped to a limb segment.
//
// Given a faithful sensor orientation q_raw (see orientation_filter.h) the goal
// is a per-sensor transform to a segment anatomical frame:
//
//     q_anat = A * q_raw * M
//
//   M (anatomical-segment-body -> sensor-body) — the constant MOUNTING: how the
//       IMU happens to be strapped on. Derived from two functional joint axes
//       measured in the sensor frame (e.g. forearm long axis from a twist swing,
//       elbow flexion axis from an elbow swing), which are anatomically
//       orthogonal so they define the segment frame.
//   A (fusion-world -> anatomical-world) — chosen so that at the captured
//       reference pose q_anat is identity (segment at its rest orientation).
//
// q_anat is what every consumer uses (3D viz, recording metadata, joint-angle
// analysis); joint angles are relative rotations between adjacent segments.
//
// Pure and header-only (QtGui types only, no UI) so it is unit-testable and
// reusable by other IMU drivers.
namespace imu_calibration {

// Mandated sensor mounting for the arm strap convention (watch-like: face away
// from the thigh = lateral, USB connector forward = anterior). The strap enforces
// orientation, so this is a KNOWN FIXED CONSTANT (no per-session strap solve).
//
// Derived from the desk-characterisation sensor->case map (docs/reference/imu_axis_reference.md:
//   sensor +X = device-right, +Y = USB-end, +Z = face-normal) + the strap
// convention, with axis SIGNS pinned by the measured arm-down gravity:
//   e_y (long, distal / DOWN)     = sensor +X   (accel-up at arm-down = sensor -X)
//   e_z (anterior / abduction)    = sensor +Y   (USB-end points anterior)
//   e_x (medio-lateral / flexion) = e_y x e_z   = sensor +Z
// → M (anatomical-body -> sensor-body) = (0.5, -0.5, -0.5, -0.5).
//
// Validated against captured data (both axis AND sign):
//   * M^-1 * (arm-down accel, sensor -X up) = anatomical -Y (up)  → arm hangs the
//     right way (NOT upside-down); this is the gravity check used in the gate.
//   * straight-arm abduction is physically about sensor -Y; M^-1 maps that to
//     anatomical -Z (anterior/abduction axis) → correctly-mounted sensor gives phi~=0.
// A mis-mount fails ONE of these: a long-axis-rotation error shows in phi; a flip /
// upside-down mount shows in the gravity check (phi is blind to it). Both are needed.
//
// HISTORY: Rz(-90)=(0.7071,0,0,-0.7071) [old empirical, anterior/med-lat swapped,
// phi~=90] → (0.5,-0.5,0.5,0.5) [hand-derived, long-axis sign WRONG, arm upside-down,
// undetected by phi] → (0.5,-0.5,-0.5,-0.5) [gravity-pinned signs, 2026-05-31].
// All three arm segments share the strap convention, so one nominal serves all.
inline QQuaternion nominalArmMount()
{
    using pinpoint::tuned::calib::kNominalArmMount;
    return QQuaternion(kNominalArmMount[0], kNominalArmMount[1],
                       kNominalArmMount[2], kNominalArmMount[3]);
}

// Hand sensor (back of hand): solved numerically from a characterization capture
// (gravity-down + wrist-flexion axis → anatomical frame, same method as the
// forearm). The hand's dorsal mounting is NOT a simple long-axis rotation of the
// arm nominal, which is why eyeballed 90-deg flips couldn't land it. Flex axis came
// out 89.3 deg from the long axis (clean). Sign of the flexion axis (the one DOF a
// back-and-forth swing can't resolve) verified by a static flexed-pose log.
inline QQuaternion nominalHandMount()
{
    using pinpoint::tuned::calib::kNominalHandMount;
    return QQuaternion(kNominalHandMount[0], kNominalHandMount[1],
                       kNominalHandMount[2], kNominalHandMount[3]);
}

struct Alignment {
    QQuaternion A;            // fusion-world -> anatomical-world
    QQuaternion M;            // anatomical-segment-body -> sensor-body
    bool        valid = false;
    float       axisAngleDeg = 0.0f;   // measured angle between the two joint axes (want ~90)
};

// Generic per-segment solve from a reference pose plus two functional joint axes.
// Used for every limb segment (forearm, hand, upper arm); only the physical motions
// that produce the two axes differ per segment — the math is identical.
//   refRaw            — fused quaternion at the arm-down reference pose.
//   gravityDownSensor — unit DOWN direction in the sensor body frame at reference
//                       (= proximal->distal while the segment hangs); fixes the long-axis sign.
//   longAxisSensor    — segment long axis (e.g. forearm twist swing), sensor frame, sign free.
//                       Pass gravityDownSensor here for segments where the long axis is
//                       taken from the hanging pose rather than a dedicated twist motion.
//   flexAxisSensor    — a perpendicular functional joint axis (e.g. elbow/wrist/shoulder
//                       flexion swing), sensor frame, sign free.
inline Alignment solveSegment(const QQuaternion &refRaw,
                              const QVector3D   &gravityDownSensor,
                              const QVector3D   &longAxisSensor,
                              const QVector3D   &flexAxisSensor)
{
    Alignment out;

    QVector3D longA = longAxisSensor.normalized();
    QVector3D flexA = flexAxisSensor.normalized();
    const QVector3D down = gravityDownSensor.normalized();

    // Angle between the two functional axes — anatomically ~90 deg. Reported so the
    // caller can warn if the capture was poor (axes far from orthogonal).
    const float c = qBound(-1.0f, QVector3D::dotProduct(longA, flexA), 1.0f);
    out.axisAngleDeg = qRadiansToDegrees(std::acos(std::abs(c)));

    // Segment anatomical basis, expressed in the SENSOR frame:
    //   e_y = long axis, pointing distally (toward the hand) — aligned with "down"
    //         at the reference pose so the model bone hangs correctly.
    //   e_x = flexion axis, Gram-Schmidt-orthogonalised against e_y.
    //   e_z = e_x x e_y  (right-handed).
    QVector3D ey = longA;
    if (QVector3D::dotProduct(ey, down) < 0.0f) ey = -ey;
    QVector3D ex = (flexA - QVector3D::dotProduct(flexA, ey) * ey).normalized();
    QVector3D ez = QVector3D::crossProduct(ex, ey).normalized();

    // M maps anatomical-body axes to their sensor-frame directions, i.e. its
    // columns are (e_x, e_y, e_z). fromAxes() builds exactly that rotation.
    out.M = QQuaternion::fromAxes(ex, ey, ez).normalized();

    // A places the reference pose at identity: A = conj(refRaw * M).
    out.A = (refRaw * out.M).conjugated().normalized();

    out.valid = (out.axisAngleDeg >= pinpoint::tuned::calib::kAxisAngleMinDeg
                 && out.axisAngleDeg <= pinpoint::tuned::calib::kAxisAngleMaxDeg);
    return out;
}

// ── Trunk segments: pelvis (hip sensor) and thorax (chest sensor) ─────────────────────────────
//
// The trunk does NOT use solveSegment (imu_frame_contract.md §5 "Trunk segments"). The limb solve
// makes the reference-pose body frame the world (A = conj(refRaw·M)), so its world is Y-down and
// the reference posture is zeroed. Every trunk consumer reads world Z as vertical, and the trunk's
// posture is itself a measurement, so the trunk keeps a gravity-aligned, Z-up, STANCE-HEADED
// world: +Z up, +Y the horizontal direction the golfer faced at stand tall, +X = Y × Z (the
// golfer's right). The segment frame is +X right, +Y anterior, +Z superior.
//
// The solve, from a stand-tall still window (mean accel f̄ and mean fused q̄, sensor frame):
//   e_z = f̄ normalised (at rest the accelerometer reads +1 g along up);
//   e_y = the INWARD face normal, sensor −Z (label out), made orthogonal to e_z;
//   e_x = e_y × e_z;  M = fromAxes(e_x, e_y, e_z), so M·v_anat = v_sensor;
//   A = Rz(ψ)·L — L the smallest rotation taking the filter's own up q̄·e_z onto +Z (≈ identity
//   for Madgwick, ≈ Rx(180°) for ESKF, §7: solved, never assumed), ψ the yaw about +Z taking
//   L·q̄·e_y (horizontal by construction) onto +Y.
// At stand tall q_anat = A·q̄·M is the identity; after it q_anat is the true segment orientation
// in the stance frame. The mount tilt (kyphosis, sacral slope) is absorbed exactly; the mount YAW
// about the spine (δ) is invisible to standing still and is assumed zero (tier δ0, σ_δ = 10°).
//
// ⚠ WHAT STAND TALL CANNOT CATCH: a sensor turned face-in (label against the body) with the USB
// end still up reads the same gravity as a correct one — the two differ only by a rotation about
// up, which gravity does not see and the fusion yaw is arbitrary about. It solves, passes every
// gate here, and puts the stance frame 180° about +Z. The turn-away step (design §6.3 step 2,
// "away on both") is what rejects it.

enum class TrunkSensor { Pelvis, Thorax };

// The stand-tall still window for ONE trunk sensor, summarised by the caller (≥ 2 s).
struct StandTallWindow {
    QVector3D   meanAccelG;           // f̄, sensor frame, g
    QQuaternion meanQuat;             // q̄, fused sensor→world (Madgwick or ESKF world)
    QVector3D   meanGyroDps;          // sensor frame, °/s (the bias, when the golfer is still)
    int         samplesReceived = 0;
    int         samplesExpected = 0;
};

// What the solve saw and decided — every gate, its residual, and the δ tier the result carries.
struct TrunkSolveRecord {
    bool  samplesOk  = false;         // ≥ kTrunkMinSampleFraction of the expected samples
    bool  stillOk    = false;         // mean gyro < kTrunkStillGyroMaxDps on every axis
    bool  gravityOk  = false;         // |f̄| = 1 g ± kTrunkGravityTolPct %
    bool  longAxisOk = false;         // the USB axis within kTrunkLongAxisMaxFromUpDeg of up
    bool  faceOk     = false;         // the face normal within kTrunkFaceMaxFromHorizontalDeg of horizontal
    float sampleFraction        = 0.0f;   // received / expected
    float maxAbsGyroDps         = 0.0f;   // max over the axes of |mean gyro|
    float gravityErrPct         = 0.0f;   // | |f̄| − 1 | × 100
    float longAxisFromUpDeg     = 0.0f;   // angle between sensor +Y and up
    float faceFromHorizontalDeg = 0.0f;   // elevation of the face normal (±Z) above/below horizontal
    QString deltaTier     = QStringLiteral("δ0");
    float   sigmaDeltaDeg = pinpoint::tuned::calib::kTrunkMountYawSigmaDeg;
    QString failure;                  // golfer-facing, names the sensor and the cause; empty when valid
};

struct TrunkAlignment {
    QQuaternion A;                    // fusion-world -> stance frame (Rz(ψ)·L)
    QQuaternion M;                    // anatomical-segment-body -> sensor-body
    bool        valid = false;        // every gate passed
    TrunkSolveRecord record;
};

namespace detail {
// The smallest rotation taking u onto world +Z, in double precision. Antiparallel (the ESKF world,
// whose up is −Z) has no unique smallest rotation: 180° about +X, any horizontal axis being as
// good — the yaw ψ applied after it fixes the heading either way.
inline QQuaternion rotationOntoUp(const QVector3D &uIn)
{
    const double n = std::sqrt(double(uIn.x()) * uIn.x() + double(uIn.y()) * uIn.y()
                               + double(uIn.z()) * uIn.z());
    if (n < 1e-12) return QQuaternion();
    const double ux = uIn.x() / n, uy = uIn.y() / n, uz = uIn.z() / n;
    if (uz < -1.0 + 1e-9) return QQuaternion(0.0f, 1.0f, 0.0f, 0.0f);
    // normalise(1 + u·ẑ, u × ẑ), u × ẑ = (u_y, −u_x, 0).
    const double w = 1.0 + uz, x = uy, y = -ux;
    const double m = std::sqrt(w * w + x * x + y * y);
    return QQuaternion(float(w / m), float(x / m), float(y / m), 0.0f);
}
} // namespace detail

inline TrunkAlignment solveTrunkSegment(const StandTallWindow &w, TrunkSensor sensor)
{
    namespace tc = pinpoint::tuned::calib;
    constexpr double kRadToDeg = 57.29577951308232;

    TrunkAlignment out;
    TrunkSolveRecord &r = out.record;
    const QString who = sensor == TrunkSensor::Pelvis ? QStringLiteral("Hip sensor")
                                                      : QStringLiteral("Chest sensor");

    // ── Sensor health ──
    r.sampleFraction = w.samplesExpected > 0 ? float(w.samplesReceived) / float(w.samplesExpected) : 0.0f;
    r.samplesOk = r.sampleFraction >= tc::kTrunkMinSampleFraction;
    r.maxAbsGyroDps = std::max({ std::abs(w.meanGyroDps.x()), std::abs(w.meanGyroDps.y()),
                                 std::abs(w.meanGyroDps.z()) });
    r.stillOk = r.maxAbsGyroDps < tc::kTrunkStillGyroMaxDps;
    const float fLen = w.meanAccelG.length();
    r.gravityErrPct = std::abs(fLen - 1.0f) * 100.0f;
    r.gravityOk = r.gravityErrPct <= tc::kTrunkGravityTolPct;

    // ── Mount ──
    if (fLen > 1e-6f) {
        const QVector3D up = w.meanAccelG / fLen;
        r.longAxisFromUpDeg = float(std::acos(std::clamp(double(up.y()), -1.0, 1.0)) * kRadToDeg);
        r.faceFromHorizontalDeg = float(std::asin(std::clamp(std::abs(double(up.z())), 0.0, 1.0)) * kRadToDeg);
    } else {
        r.longAxisFromUpDeg = 180.0f;
        r.faceFromHorizontalDeg = 90.0f;
    }
    r.longAxisOk = r.longAxisFromUpDeg <= tc::kTrunkLongAxisMaxFromUpDeg;
    r.faceOk = r.faceFromHorizontalDeg <= tc::kTrunkFaceMaxFromHorizontalDeg;

    // The first failing gate names the cause. Samples and stillness first: a short or moving window
    // cannot judge the mount, so it is not blamed on the mount. The face before the long axis: the
    // face normal is perpendicular to the long axis, so its elevation never exceeds the long axis's
    // angle from up — a face failure (> 40°) always fails the long axis too (> 35°), and only this
    // order lets "faces the wrong way" (a sensor lying flat) be told apart from "upside down or on
    // its side" (the long axis alone).
    if (!r.samplesOk)
        r.failure = who + QStringLiteral(" dropped samples — move closer or check the battery");
    else if (!r.stillOk || !r.gravityOk)
        r.failure = who + QStringLiteral(" moved — hold still and try again");
    else if (!r.faceOk)
        r.failure = who + QStringLiteral(" faces the wrong way — label out, against your back");
    else if (!r.longAxisOk)
        r.failure = who + QStringLiteral(": turn it USB-up, label out");

    // ── The solve (computed whenever the geometry allows, so a rejected record still says what it
    //    would have been) ──
    if (fLen <= 1e-6f) {
        if (r.failure.isEmpty()) r.failure = who + QStringLiteral(" moved — hold still and try again");
        return out;
    }
    const QVector3D ez = w.meanAccelG / fLen;
    const QVector3D inward(0.0f, 0.0f, -1.0f);
    QVector3D ey = inward - QVector3D::dotProduct(inward, ez) * ez;
    if (ey.length() < 1e-4f) {        // face normal along up: no anterior to take
        if (r.failure.isEmpty())
            r.failure = who + QStringLiteral(" faces the wrong way — label out, against your back");
        return out;
    }
    ey.normalize();
    const QVector3D ex = QVector3D::crossProduct(ey, ez).normalized();
    out.M = QQuaternion::fromAxes(ex, ey, ez).normalized();

    const QQuaternion qBar = w.meanQuat.normalized();
    const QQuaternion L = detail::rotationOntoUp(qBar.rotatedVector(ez));
    const QVector3D fwd = (L * qBar).rotatedVector(ey);
    const double psiRad = 0.5 * 3.14159265358979323846 - std::atan2(double(fwd.y()), double(fwd.x()));
    const QQuaternion Rz = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, float(psiRad * kRadToDeg));
    out.A = (Rz * L).normalized();

    out.valid = r.failure.isEmpty();
    return out;
}

// Compose the anatomical orientation from the world->anatomical map A, the fused raw
// sensor orientation q_raw, and the anatomical->sensor mount M:  q_anat = A * q_raw * M.
// SINGLE SOURCE OF TRUTH for the A*q*M composition, shared by the live write path
// (imu_instance.cpp) and the offline scored path (imu_vision_fuser.cpp) so a
// frame-contract change cannot make them diverge (docs/design/imu_frame_contract.md).
// Normalised — the operands are unit quaternions, so this only absorbs float drift.
inline QQuaternion toAnatomical(const QQuaternion &A, const QQuaternion &qRaw, const QQuaternion &M)
{
    return (A * qRaw * M).normalized();
}

} // namespace imu_calibration
