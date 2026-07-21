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

// Kinematic-sequence strip — the proximal→distal peak ORDER and the GAPS between
// peaks, never a curve and never a bare table. A coach reads a sequence by asking
// "who fired first, and how long until the next?"; a speed-vs-time curve buries
// that answer under shape the eye has to decode, so we draw the answer directly:
// dots on a rail in time order, +Δms on every segment, one verdict pill.
// Nodes are spaced EVENLY (position order, not real time) — the gap numbers carry
// the timing, so even spacing keeps short gaps legible instead of collapsing them.
// Purely presentational: the caller passes already-reduced nodes from C++.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    // [{ label:string, tUs:real, band:string, gapMs:real }] in time order.
    // `gapMs` on node i is the gap from node i-1 (unused on the first node).
    property var    nodes: []
    property string verdict: ""          // empty hides the pill
    property bool   verdictGood: true
    property real   playheadUs: -1       // -1 = idle (all nodes full opacity)
    property bool   interactive: false   // wall cast passes false

    signal seekRequested(real tUs)

    readonly property int _n: nodes ? nodes.length : 0

    visible: _n > 0
    implicitHeight: _n > 0 ? _labelY + _labelH : 0

    // ---- geometry -----------------------------------------------------------
    readonly property real _pillH:  verdict.length > 0 ? Theme.sp(20) : 0
    readonly property real _railY:  _pillH + Theme.sp(20)   // room for the +Δms row above the rail
    readonly property real _dotD:   Theme.sp(12)
    readonly property real _labelY: _railY + _dotD / 2 + Theme.sp(6)
    readonly property real _labelH: Theme.sp(15)
    readonly property real _slotW:  _n > 0 ? width / _n : 0
    readonly property real _hitD:   _dotD + Theme.sp(12)

    // Node centres: one even slot per node, each dot centred in its slot. The
    // half-slot at either end is the padding the outer labels need.
    function _x(i) { return _slotW * (i + 0.5) }

    function _bandColor(band) {
        switch (band) {
        case "green":  return Theme.colorRagGood
        case "yellow": return Theme.colorRagWatch
        case "red":    return Theme.colorRagFault
        }
        return Theme.colorRagNone
    }

    // Idle playhead reveals everything, so the resting render is identical to the
    // non-interactive wall cast.
    function _reached(tUs) { return playheadUs < 0 || tUs <= playheadUs }

    // ---- verdict pill -------------------------------------------------------
    Rectangle {
        id: pill
        visible: root.verdict.length > 0
        anchors { top: parent.top; right: parent.right }
        implicitWidth: pillTxt.implicitWidth + Theme.sp(14)
        implicitHeight: Theme.sp(20)
        radius: Theme.radius
        color: "transparent"
        border.width: Theme.borderWidth
        border.color: root.verdictGood ? Theme.colorRagGood : Theme.colorRagWatch

        Text {
            id: pillTxt
            anchors.centerIn: parent
            text: root.verdict
            font.family: Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.trackingMicro
            color: root.verdictGood ? Theme.colorRagGood : Theme.colorRagWatch
        }
    }

    // ---- rail ---------------------------------------------------------------
    Shape {
        anchors.fill: parent
        visible: root._n > 1
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.colorBorderMid
            strokeWidth: Theme.sp(1)
            fillColor:   "transparent"
            capStyle:    ShapePath.RoundCap

            PathPolyline {
                path: {
                    var pts = []
                    for (var i = 0; i < root._n; ++i)
                        pts.push(Qt.point(root._x(i), root._railY))
                    return pts
                }
            }
        }
    }

    // ---- gap readouts (+N ms), centred on each segment -----------------------
    Repeater {
        model: Math.max(0, root._n - 1)

        delegate: Text {
            id: gapTxt
            required property int index

            // The later node of the pair carries the gap and the reveal state.
            readonly property var _node: root.nodes[index + 1]

            text: "+" + Math.round(_node && _node.gapMs !== undefined ? _node.gapMs : 0) + qsTr(" ms")
            font.family: Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color: Theme.colorText3
            x: (root._x(index) + root._x(index + 1)) / 2 - width / 2
            y: root._railY - Theme.sp(11) - height

            opacity: (_node && root._reached(_node.tUs)) ? 1.0 : 0.35
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
    }

    // ---- nodes --------------------------------------------------------------
    Repeater {
        model: root.nodes

        delegate: Item {
            id: node
            required property var modelData
            required property int index

            readonly property bool _on: root._reached(modelData.tUs)

            x: root._x(index) - width / 2
            y: 0
            width: root._slotW
            height: root._labelY + root._labelH

            opacity: _on ? 1.0 : 0.35
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

            // Hit target around the dot — sized for the pointer, invisible at rest.
            Item {
                id: hit
                width: root._hitD
                height: root._hitD
                x: (node.width - width) / 2
                y: root._railY - height / 2

                // Hover ring: outline only, and only while actually hovered.
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: Theme.borderWidth
                    border.color: Theme.colorBorderMid
                    opacity: (root.interactive && hover.hovered) ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: root._dotD
                    height: root._dotD
                    radius: width / 2
                    color: root._bandColor(node.modelData.band)
                }

                HoverHandler {
                    id: hover
                    enabled: root.interactive
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    enabled: root.interactive
                    onTapped: root.seekRequested(node.modelData.tUs)
                }
            }

            Text {
                text: node.modelData.label !== undefined ? node.modelData.label : ""
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
                color: Theme.colorText2
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                width: Math.max(0, node.width - Theme.sp(6))
                x: Theme.sp(3)
                y: root._labelY
            }
        }
    }
}
