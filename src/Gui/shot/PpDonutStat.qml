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

// Shape-painted donut stat — a score is never shown as a bare number. The arc
// carries the magnitude pre-attentively, so a glance from across the bay reads
// the shot before the digits resolve; `bandColor` is supplied by the caller
// (Theme.qualityColor / RAG band) because banding is a pipeline judgement, not
// a widget one. All geometry derives from Math.min(width, height) so one
// instance serves the panel, the detached window and the kiosk wall cast at
// wildly different sizes. Fetches nothing — value, interval and the segment
// breakdown all arrive as properties; `interactive` is false on the wall cast,
// where there is no pointer and the hover overlay must never materialise.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    property real   value:       0
    property real   maxValue:    100
    property real   interval:    -1
    property color  bandColor:   Theme.colorRagNone
    property string centerLabel: ""
    property var    segments:    []
    property bool   interactive: false

    signal segmentsRequested()

    implicitWidth:  Theme.sp(120)
    implicitHeight: Theme.sp(120)

    // ---- geometry ---------------------------------------------------------
    // 270° gauge, not a closed 360° ring: the open foot disambiguates "empty"
    // from "full" at a glance (a closed ring at 0% and at 100% differ only by
    // fill, which is exactly the cue that dies at wall-cast distance), and the
    // gap leaves clean room under the centre stack for the ± and caption lines.
    readonly property real _d:      Math.min(width, height)
    readonly property real _stroke: _d * 0.09
    readonly property real _radius: Math.max(0, (_d - _stroke) / 2)
    readonly property real _start:  135          // 0° = 3 o'clock, clockwise
    readonly property real _span:   270

    readonly property real _frac: maxValue > 0 ? Math.max(0, Math.min(1, value / maxValue)) : 0

    // Animated separately from _frac so the arc eases rather than snaps when a
    // new shot lands.
    property real _sweep: _frac * _span
    Behavior on _sweep { NumberAnimation { duration: Theme.durationNormal } }

    function _fmt(v) { return (v % 1 === 0) ? v : v.toFixed(1) }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        // Track.
        ShapePath {
            strokeColor: Theme.colorBg2
            strokeWidth: root._stroke
            fillColor:   "transparent"
            capStyle:    ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: root._radius
                radiusY: root._radius
                startAngle:  root._start
                sweepAngle:  root._span
                moveToStart: true
            }
        }

        // Value.
        ShapePath {
            strokeColor: root.bandColor
            strokeWidth: root._stroke
            fillColor:   "transparent"
            capStyle:    ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: root._radius
                radiusY: root._radius
                startAngle:  root._start
                sweepAngle:  root._sweep
                moveToStart: true
            }
        }
    }

    // ---- centre stack -----------------------------------------------------
    Column {
        anchors.centerIn: parent
        spacing: Theme.sp(1)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root._fmt(root.value)
            font.family:    Theme.fontData
            font.pixelSize: Math.round(root._d * 0.30)
            font.weight:    Font.DemiBold
            color:          root.bandColor
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.interval >= 0
            text: "± " + root._fmt(root.interval)
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzLabel
            color:          Theme.colorText3
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.centerLabel.length > 0
            text: root.centerLabel
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.capitalization: Font.AllUppercase
            font.letterSpacing:  Theme.trackingMicro
            color:              Theme.colorText3
        }
    }

    // ---- hover breakdown --------------------------------------------------
    // Gated on `interactive` so the wall cast never arms the handler at all;
    // at rest the panel sits at opacity 0, making the resting render byte-wise
    // identical either way.
    HoverHandler {
        id: hover
        enabled: root.interactive && root.segments.length > 0
        onHoveredChanged: if (hovered) root.segmentsRequested()
    }

    Rectangle {
        id: breakdown
        anchors.centerIn: parent
        width: Math.min(root.width - Theme.sp(10), Theme.sp(150))
        height: Math.min(root.height - Theme.sp(10), segCol.implicitHeight + Theme.sp(12))
        radius: Theme.radius
        color: Theme.colorSurface
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder
        clip: true

        opacity: hover.hovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

        Column {
            id: segCol
            anchors {
                left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                leftMargin: Theme.sp(8); rightMargin: Theme.sp(8)
            }
            spacing: Theme.sp(2)

            Repeater {
                model: root.segments

                Item {
                    required property var modelData

                    width: segCol.width
                    height: Theme.sp(15)

                    Text {
                        id: segLabel
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        width: Math.max(0, parent.width - segValue.width - Theme.sp(6))
                        elide: Text.ElideRight
                        text: modelData && modelData.label !== undefined ? modelData.label : ""
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzLabel
                        color:          Theme.colorText3
                    }

                    Text {
                        id: segValue
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: modelData && modelData.value !== undefined ? root._fmt(modelData.value) : ""
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzLabel
                        color:          Theme.colorText2
                    }
                }
            }
        }
    }
}
