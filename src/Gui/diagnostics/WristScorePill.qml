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
// the band label + a shape glyph, and reveals an explainable breakdown on tap (design §7.6, §8.2-F).

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
    spacing: Theme.sp(2)

    Row {
        Layout.alignment: Qt.AlignRight
        spacing: Theme.sp(3)

        scale: pillTap.pressed ? 0.97 : pillHover.hovered ? 1.02 : 1.0
        Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "▲"
            font.family: Theme.fontSymbol
            font.pixelSize: Theme.fontSzLabel
            color: Theme.qualityColor(root.score)
        }
        PpQualityPill {
            anchors.verticalCenter: parent.verticalCenter
            large: true
            score: root.score
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.band
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSzLabel
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.trackingLabel
            color: Theme.qualityColor(root.score)
        }
        TapHandler { id: pillTap; onTapped: root._open = !root._open }
        HoverHandler { id: pillHover; cursorShape: Qt.PointingHandCursor }
    }

    Rectangle {
        Layout.alignment: Qt.AlignRight
        visible: root._open
        implicitWidth:  bd.implicitWidth + Theme.sp(16)
        implicitHeight: bd.implicitHeight + Theme.sp(12)
        color: Theme.colorSurface
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder
        radius: Theme.radius

        Column {
            id: bd
            anchors.centerIn: parent
            spacing: Theme.sp(1)
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
