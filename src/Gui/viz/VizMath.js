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
.pragma library

// VizMath — the quaternion maths the QML 3-D views share (ArmVizView, ImuVizView), so each
// constant and product is written once. Maths only: the scene scaffolding (environment, lights,
// orbit) stays in each view.
//
// QML's quaternion value type names W `.scalar`; `.w` reads undefined.

// The world→scene BASIS CHANGE, a fixed Rx(-90°): the IMU world is Z-up, the Qt Quick3D scene is
// Y-up with -Z forward. Canonical in C++ as pinpoint::viz::worldToScene() (src/Gui/cameras/viz_frame.h,
// pinned by viz_frame_test); docs/design/imu_frame_contract.md §6. A function, not a shared var,
// so every caller gets its own value.
function worldToScene() { return Qt.quaternion(0.70710678, -0.70710678, 0, 0) }

// Hamilton product of two quaternions (a × b).
function quatMul(a, b) {
    return Qt.quaternion(
        a.scalar*b.scalar - a.x*b.x - a.y*b.y - a.z*b.z,
        a.scalar*b.x + a.x*b.scalar + a.y*b.z - a.z*b.y,
        a.scalar*b.y - a.x*b.z + a.y*b.scalar + a.z*b.x,
        a.scalar*b.z + a.x*b.y - a.y*b.x + a.z*b.scalar
    )
}

// Conjugate = inverse for unit quaternions.
function quatInv(q) { return Qt.quaternion(q.scalar, -q.x, -q.y, -q.z) }

// An angle difference (°) wrapped into (−180, 180] — trunk_angles.h wrapPi's rule, in degrees. The
// trunk turns difference two live headings (ImuInstance.trunkHeadingDeg) with it: TrunkRoutine and
// CheckTrunkPage. NaN in, NaN out.
function wrap180(d) {
    var w = ((d + 180) % 360 + 360) % 360 - 180
    return w === -180 ? 180 : w
}
