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

import QtQuick
import QtQuick3D
import PinPointStudio

// VizOrientationMarker — the sensor-mount orientation marker on an arm segment, shared by
// ArmVizView and BodyVizView: a bright unlit ball offset from the bone toward one local face
// (default -Z, the back/outer face the IMU straps on) near the segment's distal end. Because it
// sits OFF the long axis it orbits visibly as the bone rolls, exposing roll on the otherwise
// rotationally-symmetric segments.
//
// A 3-D Node: place it as a child of the segment's Node. The caller sets `visible` (each view has
// its own rule) and `tabScale` (BodyVizView draws it at half ArmVizView's size).
Node {
    id: tabRoot
    property real  along:    0.20            // distance up the bone (local +Y)
    property color tabColor: Theme.colorImuA
    property real  tabScale: 0.0006
    // Which local face the marker sits on. Empirically: +Z = palm (medial),
    // +X = posterior; the IMU straps on the back/outer face = opposite the palm
    // = local -Z (watch-like, away from the thigh). Tunable while we confirm.
    property vector3d tabDir: Qt.vector3d(0, 0, -1)
    y: along
    Model {
        source:    "#Sphere"
        scale:     Qt.vector3d(tabRoot.tabScale, tabRoot.tabScale, tabRoot.tabScale)
        position:  Qt.vector3d(tabRoot.tabDir.x * 0.05, tabRoot.tabDir.y * 0.05, tabRoot.tabDir.z * 0.05)
        materials: PrincipledMaterial {
            baseColor: tabRoot.tabColor       // id-qualified — `parent.tabColor` silently failed → white
            lighting:  PrincipledMaterial.NoLighting
        }
    }
}
