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

// The FINDINGS section of the Wrist motion panel — prioritised fault findings as FAULTS-style rows
// (FindingCard), with the low-confidence ones behind a link. A section, not a card: a Micro heading
// in colorWarn, CONFIDENCE heading the rows' pips, and hairlines between the rows.

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

Column {
    id: root

    property var findings: []
    signal seek(real us)

    readonly property var _main: (findings || []).filter(function (f) { return !f.lowConfidence })
    readonly property var _low:  (findings || []).filter(function (f) { return f.lowConfidence })
    property bool _showLow: false

    spacing: 0

    Item {
        width:  parent.width
        height: heading.implicitHeight + Theme.sp(4)
        PpMicro {
            id: heading
            text:  Theme.caps(qsTr("Findings"))
            color: Theme.colorWarn
        }
        PpMicro {
            visible: root._main.length + root._low.length > 0
            anchors.baseline: heading.baseline
            x: parent.width - Theme.sp(22) - implicitWidth
            text: Theme.caps(qsTr("Confidence"))
            font.letterSpacing: Theme.trackingData
        }
    }

    PpCardNote {
        visible: root._main.length === 0 && root._low.length === 0
        width:   parent.width
        topPadding: Theme.gap(6)
        text: qsTr("No faults found in this swing.")
    }

    Repeater {
        model: root._main
        delegate: FindingCard {
            required property var modelData
            required property int index
            width:   root.width
            divider: index > 0
            finding: modelData
            onSeek: (u) => root.seek(u)
        }
    }

    // The low-confidence findings, behind a link.
    Item {
        visible: root._low.length > 0
        width:   parent.width
        height:  visible ? Theme.sp(36) : 0
        Rectangle { visible: root._main.length > 0; width: parent.width; height: 1; color: Theme.colorBorder }
        PpLink {
            anchors { left: parent.left; leftMargin: Theme.gap(32); verticalCenter: parent.verticalCenter }
            text: root._showLow ? qsTr("Hide the low-confidence findings")
                                : qsTr("Low-confidence findings (%1)").arg(root._low.length)
            hoverTint: false
            onClicked: root._showLow = !root._showLow
        }
    }
    Repeater {
        model: root._showLow ? root._low : []
        delegate: FindingCard {
            required property var modelData
            width:   root.width
            finding: modelData
            onSeek: (u) => root.seek(u)
        }
    }
}
