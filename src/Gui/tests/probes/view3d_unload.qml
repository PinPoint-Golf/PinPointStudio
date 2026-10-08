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

// END-TO-END: A PAGE WITH A 3-D VIEW CAN BE LEFT WITHOUT CRASHING THE APP.
//
// Loads the calibration skeleton (BodyVizView), the arm view (ArmVizView) and a bare View3D
// in a Loader, lets each render, and unloads it — eight times each, the way leaving the
// calibration page does. On Qt 6.11.0's debug libraries an unloaded View3D was freed under the
// render thread (access violation at 0xfeeefeee… in Qt6Quick3DRuntimeRender); the app's
// View3DTeardownGuard (src/Gui/viz/view3d_teardown_guard.h) defers the delete past one frame.
// Surviving to RESULT PASS is the check: a regression crashes the process instead.
//
//   PinPointStudio --probe-qml <this file>
// Exit 0 = PASS; "PROBE:" lines say what ran.

import QtQuick
import QtQuick3D
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function log(s) { console.log("PROBE: " + s) }

    readonly property var kinds: [bodyComp, armComp, bareComp]
    property int kind: 0
    property int cycle: 0
    readonly property int cycles: 8

    Component { id: bodyComp; BodyVizView { } }
    Component { id: armComp;  ArmVizView { } }
    Component { id: bareComp; View3D { PerspectiveCamera { z: 300 } DirectionalLight { } Model { source: "#Cube" } } }

    Loader { id: ld; anchors.fill: parent; active: false }

    Timer {
        interval: 350; running: true; repeat: true
        onTriggered: {
            if (!ld.active) {
                if (probe.cycle >= probe.cycles) {
                    probe.log(["BodyVizView", "ArmVizView", "View3D"][probe.kind] + ": " + probe.cycles + " unloads survived")
                    probe.cycle = 0
                    probe.kind += 1
                    if (probe.kind >= probe.kinds.length) {
                        running = false
                        probe.log("RESULT PASS")
                        Qt.exit(0)
                        return
                    }
                }
                ld.sourceComponent = probe.kinds[probe.kind]
                ld.active = true
            } else {
                ld.active = false
                probe.cycle += 1
            }
        }
    }
}
