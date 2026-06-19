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

// The Findings dock card — prioritised fault findings, with the low-confidence ones behind a toggle.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: root

    property var findings: []
    signal seek(real us)

    readonly property var _main: (findings || []).filter(function (f) { return !f.lowConfidence })
    readonly property var _low:  (findings || []).filter(function (f) { return f.lowConfidence })
    property bool _showLow: false

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + Theme.sp(24)
    radius: Theme.radiusLg
    color: Theme.colorSurface
    border.width: Theme.borderWidth
    border.color: Theme.colorBorder

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.sp(12) }
        spacing: Theme.sp(8)

        Text {
            text: qsTr("Findings")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
            font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingLabel
            color: Theme.colorText3
        }

        Text {
            visible: root._main.length === 0 && root._low.length === 0
            Layout.fillWidth: true
            text: qsTr("No faults found in this swing.")
            wrapMode: Text.WordWrap
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; color: Theme.colorText3
        }

        Repeater {
            model: root._main
            delegate: FindingCard {
                required property var modelData
                Layout.fillWidth: true
                finding: modelData
                onSeek: (u) => root.seek(u)
            }
        }

        // low-confidence toggle + cards
        Rectangle {
            Layout.fillWidth: true
            visible: root._low.length > 0
            implicitHeight: lowRow.implicitHeight + Theme.sp(4)
            radius: Theme.radius
            color: lowMa.containsMouse
                   ? Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 1.0)
                   : Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            Row {
                id: lowRow
                spacing: Theme.sp(2)
                Text { text: root._showLow ? "▴" : "▾"
                       font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText2 }
                Text {
                    text: qsTr("Low-confidence findings (%1)").arg(root._low.length)
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText2
                }
            }
            PpPressable { id: lowMa; hoverScale: 1.0; onClicked: root._showLow = !root._showLow }
        }
        Repeater {
            model: root._showLow ? root._low : []
            delegate: FindingCard {
                required property var modelData
                Layout.fillWidth: true
                finding: modelData
                onSeek: (u) => root.seek(u)
            }
        }
    }
}
