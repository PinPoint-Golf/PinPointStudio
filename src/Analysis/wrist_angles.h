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

#include <QMatrix3x3>
#include <QQuaternion>
#include <QVector3D>
#include <algorithm>
#include <cmath>

// Pure lead-arm joint-angle extraction from per-segment ANATOMICAL quaternions
// (q_anat = A·q_raw·M; design: docs/SHOT_ANALYZER_M1_WRIST.md §4). Header-only and
// Qt-Gui-only so it is unit-testable without the app / SwingWindow.
//
// ANATOMICAL BODY FRAME (from imu_calibration::solveSegment): per segment,
//   +Y = long axis (distal / elbow→wrist),  +X = flexion axis,  +Z = e_x × e_y.
// Wrist DOFs in the forearm→hand relative rotation: flexion about X, radial/ulnar
// deviation about Z, axial (hand-vs-forearm roll) about Y.
//
// ⚠ ABSOLUTE SIGNS NOT YET HARDWARE-VERIFIED. The axis→(FE/RUD) assignment is correct
// for the imu_calibration frame (X=flexion, Z=deviation, Y=axial — verified exactly by
// wrist_angles_test.cpp). What is NOT yet pinned is the SIGN of each channel: which
// physical direction reads as +flexion / +ulnar / +pronation. That is confirmed against
// a real cup/bow pose on the session-start wizard's **"check your sensors"** page (shown
// AFTER calibration) — the live FE/RUD/pronation readouts there let the user verify and,
// if needed, flip the signs. Flip points: the `leftArm` mirror below plus a per-channel
// sign once that page is wired. The decomposition math (cross-talk isolation, magnitude,
// singularity) is correct regardless of the final sign labelling.
//
// CHOSEN CONVENTION (PinPoint = ISB + golf coaching; docs/WRISTMETRICS.md):
//   +flexion  = "bowed"   (lead wrist bowed toward the ground — the impact goal)
//   +deviation= "ulnar"   (wrist "hinge"/cock)
//   +pronation= "roll"    (forearm pronation)
// User-facing names: bow/cup, hinge, roll.

namespace pinpoint::analysis {

// q = swing * twist, where `twist` is the rotation about `axis` and `swing` is the
// remainder. Robust at the 180°-about-⟂-axis singularity (axial part vanishes →
// identity twist, so the caller gets the full rotation as swing).
struct SwingTwist { QQuaternion swing; QQuaternion twist; };

inline SwingTwist swingTwistDecompose(const QQuaternion &q, const QVector3D &axis)
{
    const QVector3D a = axis.normalized();
    const QVector3D v(q.x(), q.y(), q.z());
    const QVector3D proj = QVector3D::dotProduct(v, a) * a;
    QQuaternion twist(q.scalar(), proj.x(), proj.y(), proj.z());
    if (twist.lengthSquared() < 1e-10f)
        twist = QQuaternion();                 // identity (singular: q ≈ 180° ⟂ axis)
    else
        twist.normalize();
    const QQuaternion swing = (q * twist.conjugated()).normalized();
    return { swing, twist };
}

// Signed angle (rad) of a twist quaternion about +axis, in (-π, π].
inline double twistAngleRad(const QQuaternion &twist, const QVector3D &axis)
{
    const QVector3D v(twist.x(), twist.y(), twist.z());
    const double s = QVector3D::dotProduct(v, axis.normalized());
    return 2.0 * std::atan2(std::copysign(static_cast<double>(v.length()), s),
                            static_cast<double>(twist.scalar()));
}

// --- relative-quaternion builders (address-referenced in quaternion space) ----

// Hand relative to forearm (the wrist), referenced to the Address pose.
inline QQuaternion wristRel(const QQuaternion &qForeAnat, const QQuaternion &qHandAnat,
                            const QQuaternion &qWristAddr)
{
    const QQuaternion qWrist = (qForeAnat.conjugated() * qHandAnat).normalized();
    return (qWristAddr.conjugated() * qWrist).normalized();
}

// Forearm relative to upper arm (the elbow), referenced to the Address pose.
inline QQuaternion elbowRel(const QQuaternion &qUpperAnat, const QQuaternion &qForeAnat,
                            const QQuaternion &qElbowAddr)
{
    const QQuaternion qElbow = (qUpperAnat.conjugated() * qForeAnat).normalized();
    return (qElbowAddr.conjugated() * qElbow).normalized();
}

// --- wrist flex/ext + radial/ulnar (cross-talk-safe: swing-twist then Cardan) ---

struct WristAngles {
    double feRad  = 0.0;   // lead-wrist flexion(+) / extension(−)
    double rudRad = 0.0;   // radial(−) / ulnar(+) deviation
};

inline WristAngles wristFlexExtDeviation(const QQuaternion &qWristRel, bool leftArm = false)
{
    // ISB wrist Cardan in the imu_calibration anatomical frame (flexion about X = e_x,
    // deviation about Z, axial about Y): an XZY Tait-Bryan extraction. The third (Y)
    // angle is the hand-vs-forearm roll (forearm pronation, not a wrist DOF) and drops
    // out of the sequence, so axial never leaks into FE/RUD — exactly, with no separate
    // swing-twist. FE uses atan2 (no gimbal across wrist ROM); RUD is the asin middle
    // term but deviation stays far from ±90°, so it never gimbals.
    //
    // The original spec read FE from Z and RUD from X (swapped) via swing-twist; this is
    // the corrected, exactly-cross-talk-free form. Absolute SIGNS still need locking
    // against a real cup/bow pose at bring-up — see the header note.
    const QMatrix3x3 R = qWristRel.toRotationMatrix();
    double feRad  = std::atan2(R(2, 1), R(1, 1));                        // flexion about X
    double rudRad = std::asin(std::clamp(-R(0, 1), -1.0f, 1.0f));        // deviation about Z

    // Left lead arm mirrors the medio-lateral / dorsal axes so flexion & ulnar stay
    // positive, matching the right-arm convention (and BodyPoseAdapter mirroring).
    if (leftArm) { feRad = -feRad; rudRad = -rudRad; }
    return { feRad, rudRad };
}

// --- forearm pronation + elbow flexion (one swing-twist of the elbow rel) -------

struct ForearmElbow {
    double pronRad = 0.0;   // pronation(+) / supination(−), forearm long-axis twist
    double flexRad = 0.0;   // elbow flexion magnitude [0, π]
};

inline ForearmElbow forearmPronElbowFlex(const QQuaternion &qElbowRel, bool leftArm = false)
{
    const QVector3D yLong(0.0f, 1.0f, 0.0f);          // forearm long axis (elbow→wrist)
    const SwingTwist st = swingTwistDecompose(qElbowRel, yLong);

    // pronation = signed twist about the long axis; elbow flexion = swing magnitude.
    double pronRad = twistAngleRad(st.twist, yLong);
    const QVector3D sv(st.swing.x(), st.swing.y(), st.swing.z());
    const double flexRad = 2.0 * std::atan2(static_cast<double>(sv.length()),
                                            static_cast<double>(st.swing.scalar()));

    if (leftArm) pronRad = -pronRad;
    return { pronRad, std::abs(flexRad) };
}

inline double radToDeg(double r) { return r * 180.0 / M_PI; }

} // namespace pinpoint::analysis
