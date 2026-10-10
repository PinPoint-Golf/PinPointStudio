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

// Composite Wrist-score headline — reuses PpQualityPill for the 0–100 quartile-graded number, adds
// the band's mark (Theme.qualityMark: a tick in the top band, a target below it) and the band's
// word, so the band reads without its colour, and reveals an explainable breakdown on tap
// (design §7.6, §8.2-F).

import QtQuick
import QtQuick.Layouts
import PinPointStudio

ColumnLayout {
    id: root

    property int    score: 0
    property string band: ""
    property var    breakdown: []
    property string breakdownText: ""

    property bool _open: false
    spacing: Theme.gap(6)

    Row {
        Layout.alignment: Qt.AlignRight
        spacing: Theme.gap(6)

        PpBadge {
            anchors.verticalCenter: parent.verticalCenter
            kind: Theme.qualityMark(root.score)
            tone: Theme.qualityColor(root.score)
        }
        PpQualityPill {
            anchors.verticalCenter: parent.verticalCenter
            large: true
            score: root.score
        }
        PpMicro {
            anchors.verticalCenter: parent.verticalCenter
            text: Theme.caps(root.band)
            color: Theme.qualityColor(root.score)
        }
        TapHandler { onTapped: root._open = !root._open }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    // What the score lost, and to what: set into the panel under the pill.
    PpInset {
        Layout.alignment: Qt.AlignRight
        visible: root._open
        implicitWidth: bd.implicitWidth + 2 * padX
        padY: Theme.sp(10)
        spacing: 0

        Column {
            id: bd
            spacing: Theme.gap(2)
            Text {
                anchors.right: parent.right
                text: root.breakdownText
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                color: Theme.colorText
            }
            Repeater {
                model: root.breakdown
                delegate: Text {
                    required property var modelData
                    anchors.right: parent.right
                    text: "− " + modelData.penalty + "   " + modelData.label
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText2
                }
            }
        }
    }
}
