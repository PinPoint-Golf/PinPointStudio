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
import QtQuick.Shapes

// MARK — the ONE point mark used by every chart (dot plot, spread, run, sparkline, history, key).
// Shape per theme (Folio round, Vector square); fired = warn, filled; clean = ink3, hollow in Folio,
// filled in Vector. The picked shot adds the ink ring; a hovered shot adds a hairline ink3 ring.
// Centred on (cx, cy). Hovering it brushes the shot; clicking it emits activated().
// (Bindings are guarded: a PpMark is created in the hundreds and torn down with its chart.)
Item {
    id: mk
    property QtObject roles: null
    property var brush: null
    property int shotId: -1
    property bool fired: false
    property bool other: false              // C: out on the other side (drawn as clean)
    property real cx: 0
    property real cy: 0
    readonly property bool ok: !!roles
    property real size: !ok ? 0 : fired ? roles.markFired : roles.markClean
    property real ringSize: ok ? roles.ringD : 0     // the picked / hovered ring; smaller in a sparkline
    property bool interactive: false
    property bool ringed: brush ? brush.isPicked(shotId) : false
    readonly property bool hovered: brush ? brush.isHovered(shotId) : false
    readonly property bool square: ok && roles.square
    readonly property bool hollow: ok && roles.r.cleanHollow
    signal activated()

    x: cx; y: cy; width: 0; height: 0
    opacity: ok && brush && shotId >= 0 ? brush.shotOpacity(shotId, roles.dim) : 1
    Behavior on opacity { NumberAnimation { duration: mk.ok ? mk.roles.normal : 0 } }

    // Round marks are drawn as true curves: a small Rectangle with radius = size/2 tessellates into a
    // notched blob at these sizes (Folio, 11 Oct). Square marks stay Rectangles (crisp as they are).
    Shape {
        visible: mk.ok && !mk.square
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: !mk.ok ? "transparent" : mk.fired ? mk.roles.warn : (mk.hollow ? "transparent" : mk.roles.ink3)
            strokeColor: mk.fired || !mk.hollow || !mk.ok ? "transparent" : mk.roles.ink3
            strokeWidth: mk.fired || !mk.hollow ? 0 : 1
            PathAngleArc { centerX: 0; centerY: 0; radiusX: mk.size / 2 - (mk.hollow && !mk.fired ? 0.5 : 0); radiusY: radiusX; startAngle: 0; sweepAngle: 360 }
        }
    }
    Shape {
        visible: mk.ok && !mk.square && (mk.ringed || mk.hovered)
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"
            strokeColor: !mk.ok ? "transparent" : mk.ringed ? mk.roles.ink : mk.roles.ink3
            strokeWidth: mk.ringed ? 1.25 : 1
            PathAngleArc { centerX: 0; centerY: 0; radiusX: mk.ringSize / 2; radiusY: radiusX; startAngle: 0; sweepAngle: 360 }
        }
    }
    Rectangle {
        visible: mk.ok && mk.square
        x: -mk.size / 2; y: -mk.size / 2; width: mk.size; height: mk.size
        radius: mk.square ? 0 : mk.size / 2
        color: !mk.ok ? "transparent" : mk.fired ? mk.roles.warn : (mk.hollow ? "transparent" : mk.roles.ink3)
        border.width: mk.fired || !mk.hollow ? 0 : 1
        border.color: mk.ok ? mk.roles.ink3 : "transparent"
        antialiasing: true
    }
    Rectangle {
        visible: mk.ok && mk.square && (mk.ringed || mk.hovered)
        x: -mk.ringSize / 2; y: -mk.ringSize / 2; width: mk.ringSize; height: mk.ringSize
        radius: mk.square ? 0 : mk.ringSize / 2
        color: "transparent"
        border.width: mk.ringed ? 1.25 : 1
        border.color: !mk.ok ? "transparent" : mk.ringed ? mk.roles.ink : mk.roles.ink3
        antialiasing: true
    }
    MouseArea {
        enabled: mk.interactive && mk.shotId >= 0
        visible: enabled
        readonly property real hit: Math.max(mk.ringSize, 12)
        x: -hit / 2; y: -hit / 2; width: hit; height: hit
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: if (mk.brush) mk.brush.hoverShot(mk.shotId, containsMouse)
        onClicked: mk.activated()
    }
}
