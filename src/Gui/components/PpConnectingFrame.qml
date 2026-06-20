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

// Traveling-light "connecting" frame. Overlay it on a button with
// `anchors.fill: parent`; drive `running` from a connection-in-flight boolean
// (CameraInstance.isConnecting / ImuInstance.busy / the manager anyConnecting
// aggregates). While running, a dim track outlines the whole rounded-rect
// border and a single bright comet of stroke chases around it. When `running`
// clears (connect succeeded, failed, or timed out) the frame fades away and the
// button returns to normal. Purely decorative — captures no input.
//
// Honours Theme.reduceMotion: the comet stops and a steady accent outline marks
// the in-flight state instead (no motion).

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    // Connection-in-flight flag — the single input that turns the frame on/off.
    property bool  running:   false
    // Match these to the host button so the frame hugs its border.
    property real  radius:    Theme.radius
    property real  lineWidth: Math.max(2, Theme.sp(2))
    // Comet/outline colour. Override for filled buttons where the accent hue
    // would not read against the fill (e.g. the wizard CTA passes a light hue).
    property color color:     Theme.colorAccent

    // Decorative overlay only — never steal hover/clicks from the button below.
    enabled: false
    opacity: running ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

    // Stroke is centred on the path, so inset the rect by half the line width to
    // keep it fully inside the item (and just inside the host's own border).
    readonly property real _inset: lineWidth / 2
    readonly property real _w: Math.max(0, width  - lineWidth)
    readonly property real _h: Math.max(0, height - lineWidth)
    // Rounded-rect perimeter expressed in stroke-width units (dashPattern's unit).
    // The straight-edge approximation slightly overestimates vs. the rounded
    // corners; harmless, since the gap only needs to exceed the perimeter so that
    // exactly one comet is ever on screen.
    readonly property real _periUnits: Math.max(1, (2 * (_w + _h)) / Math.max(1, lineWidth))
    // Lit arc length — a fraction of the perimeter so it scales with button size.
    readonly property real _dashOn: Math.max(3, _periUnits * 0.18)

    readonly property bool _animated: running && !Theme.reduceMotion

    Shape {
        anchors.fill: parent
        antialiasing: true

        // Dim track — the full outline, so the frame reads "active" even at the
        // point of the loop where the comet has just passed.
        ShapePath {
            strokeColor: Qt.rgba(root.color.r, root.color.g, root.color.b, 0.22)
            strokeWidth: root.lineWidth
            fillColor:   "transparent"
            joinStyle:   ShapePath.RoundJoin
            PathRectangle {
                x: root._inset; y: root._inset
                width:  root._w; height: root._h
                radius: root.radius
            }
        }

        // Bright comet chasing the frame — or, under reduceMotion, a steady
        // accent ring (SolidLine, animation halted).
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.lineWidth
            fillColor:   "transparent"
            capStyle:    ShapePath.RoundCap
            joinStyle:   ShapePath.RoundJoin
            strokeStyle: root._animated ? ShapePath.DashLine : ShapePath.SolidLine
            // One lit arc + a gap longer than the whole perimeter ⇒ a single
            // comet travels the frame with nothing trailing it.
            dashPattern: [root._dashOn, root._periUnits]
            dashOffset:  0
            PathRectangle {
                x: root._inset; y: root._inset
                width:  root._w; height: root._h
                radius: root.radius
            }

            // Advance by one full pattern length for a seamless loop; linear
            // easing keeps the comet at constant speed around the border.
            NumberAnimation on dashOffset {
                running:  root._animated
                loops:    Animation.Infinite
                from:     0
                to:       root._dashOn + root._periUnits
                duration: Math.max(900, root._periUnits * 14)
                easing.type: Easing.Linear
            }
        }
    }
}
