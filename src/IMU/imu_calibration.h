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
#include <QVector3D>
#include <cmath>

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
// Derived from the desk-characterisation sensor->case map (docs/reference/IMU_AXIS_REFERENCE.md:
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
    return QQuaternion(0.5f, -0.5f, -0.5f, -0.5f);
}

// Hand sensor (back of hand): solved numerically from a characterization capture
// (gravity-down + wrist-flexion axis → anatomical frame, same method as the
// forearm). The hand's dorsal mounting is NOT a simple long-axis rotation of the
// arm nominal, which is why eyeballed 90-deg flips couldn't land it. Flex axis came
// out 89.3 deg from the long axis (clean). Sign of the flexion axis (the one DOF a
// back-and-forth swing can't resolve) verified by a static flexed-pose log.
inline QQuaternion nominalHandMount()
{
    return QQuaternion(0.4388f, 0.6054f, 0.4965f, -0.4409f);
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

    out.valid = (out.axisAngleDeg >= 60.0f && out.axisAngleDeg <= 120.0f);
    return out;
}

// Compose the anatomical orientation from the world->anatomical map A, the fused raw
// sensor orientation q_raw, and the anatomical->sensor mount M:  q_anat = A * q_raw * M.
// SINGLE SOURCE OF TRUTH for the A*q*M composition, shared by the live write path
// (imu_instance.cpp) and the offline scored path (imu_vision_fuser.cpp) so a
// frame-contract change cannot make them diverge (docs/design/IMU_FRAME_CONTRACT.md).
// Normalised — the operands are unit quaternions, so this only absorbs float drift.
inline QQuaternion toAnatomical(const QQuaternion &A, const QQuaternion &qRaw, const QQuaternion &M)
{
    return (A * qRaw * M).normalized();
}

} // namespace imu_calibration
