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

// Explicit basis change from the IMU world frame to the Qt Quick3D scene frame
// (docs/design/imu_frame_contract.md §6, imu_rearchitecture.md §3.4c).
//
//   IMU world : right-handed, +Z up (gravity reaction), +X right, +Y forward.
//   Qt scene  : right-handed, +Y up, -Z forward, +X right.
//
// The change of basis is a fixed Rx(-90 deg) (det +1, NO handedness flip):
//   world +Z -> scene +Y,   world +Y -> scene -Z,   world +X -> scene +X.
//
// Header-only and Qt-Gui-only so it is unit-testable and shared by both viz renderers.
namespace pinpoint::viz {

// The world->scene basis-change quaternion, Rx(-90 deg) = (cos-45, sin-45, 0, 0).
inline QQuaternion worldToScene()
{
    return QQuaternion(0.70710678f, -0.70710678f, 0.0f, 0.0f);
}

// Re-express an orientation given in IMU-world coordinates in scene coordinates
// (similarity transform). NOTE: this is the basis change ONLY — a rendered bone also
// needs its per-GLB rest offset, so the full rest factor R0 in a renderer is
// worldToScene() composed with that rest offset, NOT this similarity alone (§3.4c).
inline QQuaternion worldOrientationToScene(const QQuaternion &qWorld)
{
    const QQuaternion C = worldToScene();
    return (C * qWorld * C.conjugated()).normalized();
}

// ── The trunk on BodyVizView (trunk_imu_sequence_grf_design.md §6.7; imu_frame_contract.md §5, §6) ──
//
// The pelvis and thorax q_anat live in the TRUNK world (imu_frame_contract.md §5): +X the golfer's
// right, +Y anterior (the way the golfer faced at stand tall), +Z up; identity at stand tall. The
// avatar stands in the scene FACING THE CAMERA (+Z scene), its left arm at scene +X, so the golfer's
// right is scene −X. Two factors, then, not one:
//   * worldToScene() (Rx(−90°)), the basis change every viz shares, and
//   * Ry(180°), because the avatar faces the viewer, not away from it;
// and on top of both, each bone keeps its own GLB rest rotation (the spine's ±6° tilts, the thighs'
// 180° about Z). ⚠ A trunk bone bound with the basis change alone turns the right way — a turn is
// about the vertical, which Ry(180°) leaves alone — and so LOOKS right, but it bends the avatar
// BACKWARD when the golfer bends forward and drops the spine's rest tilts. viz_frame_test pins it.
//
// A segment's motion on the avatar is the trunk-world rotation re-expressed in the avatar's scene,
//     D = T · q_anat · T⁻¹,  T = trunkWorldToAvatar(),
// applied ON TOP of the bone's rest world orientation: boneWorld = D · boneRestWorld.

// The trunk world → the avatar's scene: right → −X, anterior → +Z, up → +Y.
inline QQuaternion trunkWorldToAvatar()
{
    return (QQuaternion(0.0f, 0.0f, 1.0f, 0.0f) * worldToScene()).normalized();   // Ry(180°) · Rx(−90°)
}

// A golfer-relative turn about the vertical (degrees, + = AWAY from the target) as a trunk-world
// q_anat. A right-hander turns away about −Z, a left-hander about +Z (pinpoint_sign_conventions.md,
// "Trunk IMU readings"): the guide's handedness lives here, the live sensors carry their own.
inline QQuaternion trunkGolferTurn(double awayDeg, bool rightHanded)
{
    return QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, float(rightHanded ? -awayDeg : awayDeg));
}

// BodyVizView.qml's rest pose, verbatim — the node rotations and offsets this binding builds on.
// (body_pose_adapter.cpp carries the same constants for its own chain.)
namespace bodyviz {
inline const QQuaternion kHipsRest      (1.0f, 0.0f, 0.0f, 0.0f);
inline const QQuaternion kSpineRest     (0.9982f, -0.0607f, 0.0f, 0.0f);
inline const QQuaternion kSpine1Rest    (1.0f, 0.0f, 0.0f, 0.0f);
inline const QQuaternion kSpine2Rest    (0.9983f,  0.0577f, 0.0f, 0.0f);
inline const QQuaternion kLeftUpLegRest (-0.0030f, 0.0f, -0.0064f, 1.0000f);   // ≈ 180° about Z
inline const QQuaternion kRightUpLegRest( 0.0031f, 0.0f, -0.0063f, 1.0000f);
// Node offsets (parent-local).
inline const QVector3D kHipsPos     (0.0f, 0.9979f, 0.0f);
inline const QVector3D kSpinePos    (0.0f, 0.0992f, -0.0123f);
inline const QVector3D kSpine1Pos   (0.0f, 0.1173f, 0.0f);
inline const QVector3D kSpine2Pos   (0.0f, 0.1346f, 0.0f);
inline const QVector3D kNeckPos     (0.0f, 0.1503f, 0.0088f);
inline const QVector3D kHeadPos     (0.0f, 0.1032f, 0.0314f);
inline const QVector3D kLeftUpLegPos ( 0.0912f, -0.0666f, -0.0006f);
inline const QVector3D kRightUpLegPos(-0.0913f, -0.0666f, -0.0006f);
inline const QVector3D kLegPos      (0.0f, 0.4060f, 0.0f);     // knee, in the thigh's frame
} // namespace bodyviz

// The parent-local rotations BodyVizView's trunk nodes take under useTrunkOverride.
struct TrunkAvatarBones {
    QQuaternion hips, spine, spine1, spine2;
    QQuaternion leftUpLeg, leftLeg, rightUpLeg, rightLeg;
};

// The fraction f of a rotation, about its own axis (the short way round).
inline QQuaternion rotationFraction(const QQuaternion &r, float f)
{
    const QQuaternion q = r.scalar() < 0.0f ? -r : r;
    QVector3D axis;
    float angleDeg = 0.0f;
    q.normalized().getAxisAndAngle(&axis, &angleDeg);
    if (!(angleDeg > 1e-5f) || axis.lengthSquared() < 1e-12f) return QQuaternion();
    return QQuaternion::fromAxisAndAngle(axis, angleDeg * f);
}

// The pelvis q_anat drives the hips. The thorax q_anat, RELATIVE TO THE PELVIS, is spread over
// Spine / Spine1 / Spine2 by the length of each bone (to the next joint), so the spine bends and
// twists along its length and Spine2 — the chest, where the thorax sensor sits — carries the thorax
// exactly: spine2 world = D_thorax · its rest world orientation. The legs are not measured: they
// stay PLANTED, each thigh re-aimed from its moved hip joint to its rest knee and each shin left at
// its rest world orientation, so a hip turn does not swivel the feet.
inline TrunkAvatarBones trunkAvatarBones(const QQuaternion &pelvisAnat, const QQuaternion &thoraxAnat)
{
    using namespace bodyviz;
    const QQuaternion T  = trunkWorldToAvatar();
    const QQuaternion Dp = (T * pelvisAnat.normalized() * T.conjugated()).normalized();
    const QQuaternion Dt = (T * thoraxAnat.normalized() * T.conjugated()).normalized();

    TrunkAvatarBones b;
    b.hips = (Dp * kHipsRest).normalized();

    // The thorax relative to the pelvis, in the hips' own (rest) frame.
    const QQuaternion R = (kHipsRest.conjugated() * Dp.conjugated() * Dt * kHipsRest).normalized();
    const float l0 = kSpine1Pos.length(), l1 = kSpine2Pos.length(), l2 = kNeckPos.length();
    const float L  = l0 + l1 + l2;
    const QQuaternion S01 = kSpineRest * kSpine1Rest;
    b.spine  = (rotationFraction(R, l0 / L) * kSpineRest).normalized();
    b.spine1 = (kSpineRest.conjugated() * rotationFraction(R, l1 / L) * kSpineRest * kSpine1Rest).normalized();
    b.spine2 = (S01.conjugated() * rotationFraction(R, l2 / L) * S01 * kSpine2Rest).normalized();

    // Planted legs (the hips node is the scene root's child, so hips local == hips world).
    auto plant = [&](const QVector3D &hipPos, const QQuaternion &upLegRest, QQuaternion *upLeg, QQuaternion *leg) {
        const QVector3D restKnee = hipPos + upLegRest.rotatedVector(kLegPos);    // hips-rest frame
        const QVector3D newHip   = b.hips.rotatedVector(hipPos);
        const QQuaternion aim    = QQuaternion::rotationTo(restKnee - hipPos, restKnee - newHip);
        const QQuaternion upLegW = (aim * kHipsRest * upLegRest).normalized();
        *upLeg = (b.hips.conjugated() * upLegW).normalized();
        *leg   = (upLegW.conjugated() * kHipsRest * upLegRest).normalized();  // the shin keeps its rest world
    };
    plant(kLeftUpLegPos,  kLeftUpLegRest,  &b.leftUpLeg,  &b.leftLeg);
    plant(kRightUpLegPos, kRightUpLegRest, &b.rightUpLeg, &b.rightLeg);
    return b;
}

} // namespace pinpoint::viz
