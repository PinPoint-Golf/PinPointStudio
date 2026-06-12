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

} // namespace pinpoint::viz
