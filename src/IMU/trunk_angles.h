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
#include <algorithm>
#include <cmath>
#include <limits>

// Trunk readings from a trunk q_anat (trunk design D13): PROJECTIONS OF SEGMENT AXES, not Cardan
// angles, so there is no sequence dependence at large turn and they match the coaches' vocabulary.
//
// The frame is imu_frame_contract.md §5 "Trunk segments": q_anat from solveTrunkSegment, a
// gravity-aligned, Z-up, stance-headed world; segment +X the golfer's right, +Y anterior,
// +Z superior. The signs are pinpoint_sign_conventions.md "Trunk IMU readings". Everything here is
// RAW — the frame is the same for both hands, so the lead-side mirroring a left-hander needs is the
// caller's (design D15: the producer mirrors at ingest), and so is the reference to address.
//
// Pure and header-only (QtGui types only).
namespace trunk_angles {

// Wrap an angle into (−π, π].
inline double wrapPi(double a)
{
    constexpr double kPi = 3.14159265358979323846;
    while (a >  kPi) a -= 2.0 * kPi;
    while (a <= -kPi) a += 2.0 * kPi;
    return a;
}

// Heading (rad, atan2 in the horizontal plane, from stance +X toward +Y) of the medio-lateral axis
// q·x̂. Turn is this relative to the swing's own address heading — turnFromRad below — which is
// also what makes a fusion-yaw drift between stand tall and the swing drop out.
inline double turnHeadingRad(const QQuaternion &qAnat)
{
    const QVector3D ml = qAnat.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
    return std::atan2(double(ml.y()), double(ml.x()));
}

// Signed heading change from a reference orientation (rad, (−π, π]); positive = turned about +Z
// (opening for a right-hander).
inline double turnFromRad(const QQuaternion &qAnat, const QQuaternion &qRef)
{
    return wrapPi(turnHeadingRad(qAnat) - turnHeadingRad(qRef));
}

// The LIVE heading (°, (−180, 180], turnHeadingRad in degrees) of one trunk q_anat, for the
// calibration routine's turn detector (TrunkRoutine.qml via ImuInstance::trunkHeadingDeg; design
// §16.2, 26 Oct). NaN — "no reading", never a number to act on — when the quaternion is not finite
// (a filter fault can leave one), has no length, or when the medio-lateral axis stands within about
// 3° of vertical, where its horizontal heading is noise. The caller differences two readings and
// wraps the difference (turnFromRad's rule).
inline double liveHeadingDeg(const QQuaternion &qAnat)
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    const double w = qAnat.scalar(), x = qAnat.x(), y = qAnat.y(), z = qAnat.z();
    if (!std::isfinite(w) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return kNaN;
    const double n = std::sqrt(w * w + x * x + y * y + z * z);
    if (!(n > 1e-6)) return kNaN;
    const QVector3D ml = qAnat.normalized().rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
    const double h = std::hypot(double(ml.x()), double(ml.y()));
    if (!std::isfinite(h) || h < 0.05) return kNaN;          // sin 3° ≈ 0.05
    return std::atan2(double(ml.y()), double(ml.x())) * 57.29577951308232;
}

// Obliquity / side bend (rad): elevation of the medio-lateral axis q·x̂ above horizontal, RAW sign
// — positive when the golfer's RIGHT (+X) side is higher. "Lead side higher" is the reported sign,
// so a right-hander's reading is this negated and a left-hander's is this as it stands; that flip
// is the caller's. First-order sensitive to the mount yaw δ through a forward bend (design §6.2).
inline double obliquityRad(const QQuaternion &qAnat)
{
    const QVector3D ml = qAnat.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
    return std::asin(std::clamp(double(ml.z()), -1.0, 1.0));
}

// Forward bend / tilt (rad): the inclination of the long axis q·ẑ from vertical, signed ANTERIOR
// positive. The sign is the sagittal sense, from the anterior axis q·ŷ: bent toward the ball points
// it below horizontal. The magnitude is the whole inclination, so a side bend contributes to it.
// Independent of the mount yaw δ (q·ẑ does not move under a rotation about ẑ).
inline double forwardBendRad(const QQuaternion &qAnat)
{
    const QVector3D si  = qAnat.rotatedVector(QVector3D(0.0f, 0.0f, 1.0f));
    const QVector3D ant = qAnat.rotatedVector(QVector3D(0.0f, 1.0f, 0.0f));
    const double incl = std::acos(std::clamp(double(si.z()), -1.0, 1.0));
    return ant.z() <= 0.0f ? incl : -incl;
}

// The segment's own superior–inferior rate (°/s), design D3: gyro_segment · ẑ_segment. The gyro is
// the SEGMENT-frame one (SegmentStream::gyroDps, M⁻¹·v_sensor). Positive = about +Z, opening for a
// right-hander. Independent of δ and of the fusion world's heading.
inline double siRateDps(const QVector3D &gyroSegmentDps)
{
    return double(gyroSegmentDps.z());
}

} // namespace trunk_angles
