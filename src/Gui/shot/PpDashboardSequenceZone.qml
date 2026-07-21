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

// Sequence zone — one wide PpSequenceStrip. The catalogue's `sequence` metric is
// planned (no producer), so we DON'T fake a curve: the strip is built from the peak
// times we DO have — the available speed series (hand then clubhead today;
// pelvis/thorax/lead-arm join automatically when their series land). Emphasis is
// ORDER and GAPS, not curves. The gap reduction is C++ (ChartMetrics.sequenceNodes →
// kinematic_sequence.h).
//
// Swing-agnostic, like the other zones: the strip renders when ≥2 peaks are
// measurable, else the zone holds its place with an "NA" line rather than
// collapsing. The layout must not move between swings.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: zone

    property var catalog: null
    property var shotCtx: ({})
    property var detail: ({})
    property int sessionType: -1

    property bool interactive: false
    property real playheadUs: -1

    signal seekRequested(real tUs)

    ChartMetrics { id: cm }

    // Canonical proximal→distal chain. Only the series that exist in this shot are
    // returned by sequenceNodes(); the rest are dropped (no fake nodes).
    readonly property var _chainOrder: ["pelvisRotation", "thoraxRotation", "leadArmSpeed",
                                        "handSpeed", "clubheadSpeed"]

    readonly property var _nodes: (detail && detail.series)
                                  ? cm.sequenceNodes(detail.series, _chainOrder) : []
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

    // Strip nodes: the C++ reduction plus this zone's display label and its verdict
    // band. A node that arrived out of the expected chain order is the one carrying
    // the fault, so it — not the whole strip — is tinted.
    readonly property var _stripNodes: {
        var out = []
        for (var i = 0; i < _nodes.length; ++i) {
            var early = i > 0 && _expectedIndex(_nodes[i].key) < _expectedIndex(_nodes[i - 1].key)
            out.push({ label: _label(_nodes[i].key),
                       tUs: _nodes[i].tPeakUs,
                       gapMs: _nodes[i].gapMs,
                       band: early ? "yellow" : "green" })
        }
        return out
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
                      leftMargin: Theme.sp(11); rightMargin: Theme.sp(11); topMargin: Theme.sp(9) }
            spacing: Theme.sp(6)

            Text {
                text: qsTr("SEQUENCE")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }

            PpSequenceStrip {
                visible: zone._hasNodes
                Layout.fillWidth: true
                nodes:       zone._stripNodes
                verdict:     zone._intact ? qsTr("INTACT") : qsTr("OUT OF SEQUENCE")
                verdictGood: zone._intact
                playheadUs:  zone.playheadUs
                interactive: zone.interactive
                onSeekRequested: (tUs) => zone.seekRequested(tUs)
            }

            // Placeholder — the zone holds its place on a swing with too few
            // measurable peaks rather than collapsing the layout under it.
            Text {
                visible: !zone._hasNodes
                Layout.fillWidth: true
                text: qsTr("NA — no kinematic-sequence data for this swing.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                color: Theme.colorText3
            }
        }
    }
}
