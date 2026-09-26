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

// SwingViz3DHost — keeps ONE SwingViz3DView alive for a screen and lends it to whichever panel
// slot is showing (docs/design/swing_3d_viz_design.md §6).
//
// ⚠ WHY A HOST. PpModeStage rebuilds its delegates on every mode, tab and arrangement switch,
// and destroying Quick 3D View3D instances has corrupted the render thread's heap in this app
// before (CapturePage.qml keeps its three views alive for exactly that reason). So the view is
// created the first time a slot asks for it — never before, so a user who never opens the panel
// pays nothing — and is then only REPARENTED: into the slot on attach, back here on detach.
// The Loader is the view's QObject owner, so a slot's destruction never takes the view with it.

import QtQuick
import PinPointStudio

Item {
    id: host
    visible: false
    width: 0
    height: 0

    property string swingDir: ""
    property real   positionUs: 0
    property Item   slot: null
    readonly property Item view: loader.item

    function attach(s) {
        loader.active = true           // latched: never set false again
        slot = s
    }
    function detach(s) {
        if (slot === s) slot = null
    }

    Loader {
        id: loader
        active: false
        parent: host.slot ? host.slot : host
        anchors.fill: parent
        visible: host.slot !== null
        sourceComponent: SwingViz3DView {
            swingDir: host.swingDir
            positionUs: host.positionUs
        }
    }
}
