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

// Sequence zone — the kinematic sequence, degrading gracefully. The catalogue's
// `sequence` metric is planned (no producer), so we DON'T fake a curve: instead we
// build the strip from the peak times we DO have — the available speed series (hand
// then clubhead today; pelvis/thorax/lead-arm join automatically when their series
// land). Emphasis is ORDER and GAPS, not curves: nodes in time order with +Δms
// between them, a verdict pill, and one plain-language read. The gap reduction is
// C++ (ChartMetrics.sequenceNodes → kinematic_sequence.h). Collapses below 2 nodes.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: zone

    property var catalog: null
    property var shotCtx: ({})
    property var detail: ({})
    property int sessionType: -1

    ChartMetrics { id: cm }

    // Canonical proximal→distal chain. Only the series that exist in this shot are
    // returned by sequenceNodes(); the rest are dropped (no fake nodes).
    readonly property var _chainOrder: ["pelvisRotation", "thoraxRotation", "leadArmSpeed",
                                        "handSpeed", "clubheadSpeed"]

    readonly property var _nodes: (catalog && detail && detail.series)
                                  ? cm.sequenceNodes(detail.series, _chainOrder) : []
    // Swing-agnostic: the zone always shows when enabled (stable layout); it renders the
    // node strip when ≥2 peaks are measurable, else an "NA" line — never collapses.
    readonly property bool _hasNodes: _nodes.length >= 2
    implicitHeight: card.implicitHeight
    visible: true

    function _label(key) {
        switch (key) {
        case "handSpeed":      return qsTr("Hand")
        case "clubheadSpeed":  return qsTr("Club")
        case "pelvisRotation": return qsTr("Pelvis")
        case "thoraxRotation": return qsTr("Thorax")
        case "leadArmSpeed":   return qsTr("Lead arm")
        }
        var sl = cm.shortLabel(key)
        return sl.length ? sl : key
    }
    function _expectedIndex(key) {
        var i = _chainOrder.indexOf(key)
        return i < 0 ? 99 : i
    }
    // INTACT when the time order matches the proximal→distal chain order.
    readonly property bool _intact: {
        for (var i = 1; i < _nodes.length; ++i)
            if (_expectedIndex(_nodes[i].key) < _expectedIndex(_nodes[i - 1].key)) return false
        return true
    }
    function _readLine() {
        if (_nodes.length < 2) return ""
        var parts = []
        for (var i = 1; i < _nodes.length; ++i)
            parts.push(_label(_nodes[i].key) + " +" + Math.round(_nodes[i].gapMs) + qsTr(" ms"))
        return _label(_nodes[0].key) + qsTr(" leads") + ", " + parts.join(", ") + "."
    }

    Rectangle {
        id: card
        width: zone.width
        implicitHeight: col.implicitHeight + Theme.sp(22)
        color: Theme.colorSurface
        radius: Theme.radiusLg
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder

        ColumnLayout {
            id: col
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: Theme.sp(16); rightMargin: Theme.sp(14); topMargin: Theme.sp(11) }
            spacing: Theme.sp(9)

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("SEQUENCE")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText3
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    visible: zone._hasNodes
                    implicitWidth: vTxt.implicitWidth + Theme.sp(14); implicitHeight: Theme.sp(20)
                    radius: Theme.radius
                    color: "transparent"
                    border.width: Theme.borderWidth
                    border.color: zone._intact ? Theme.colorRagGood : Theme.colorRagWatch
                    Text {
                        id: vTxt
                        anchors.centerIn: parent
                        text: zone._intact ? qsTr("INTACT") : qsTr("OUT OF SEQUENCE")
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color: zone._intact ? Theme.colorRagGood : Theme.colorRagWatch
                    }
                }
            }

            // Node strip: chips with +Δms connectors between them.
            RowLayout {
                visible: zone._hasNodes
                Layout.fillWidth: true
                spacing: Theme.sp(6)
                Repeater {
                    model: zone._nodes
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        spacing: Theme.sp(6)
                        // gap connector before every node except the first
                        Text {
                            visible: index > 0
                            text: "+" + Math.round(modelData.gapMs) + qsTr(" ms")
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                            color: Theme.colorText3
                        }
                        Text {
                            visible: index > 0
                            text: "→"; color: Theme.colorText3
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                        }
                        Rectangle {
                            implicitWidth: nTxt.implicitWidth + Theme.sp(16); implicitHeight: Theme.sp(26)
                            radius: Theme.radius
                            color: Theme.colorBg2
                            Text {
                                id: nTxt
                                anchors.centerIn: parent
                                text: zone._label(modelData.key)
                                font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                                color: Theme.colorText
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            Text {
                Layout.fillWidth: true
                text: zone._hasNodes ? zone._readLine()
                                     : qsTr("NA — no kinematic-sequence data for this swing.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                color: Theme.colorText3
            }
        }
    }
}
