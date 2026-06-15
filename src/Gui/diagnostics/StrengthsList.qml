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

// The "Working well" dock card — positive findings the golfer should protect while fixing faults.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: root

    property var strengths: []
    signal seek(real us)

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
            text: qsTr("Working well")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
            font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingLabel
            color: Theme.colorText3
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Keep these while you work on the findings — they're correct, and easy to lose by accident when changing something else.")
            wrapMode: Text.WordWrap
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; color: Theme.colorText3
        }

        Repeater {
            model: root.strengths
            delegate: FindingCard {
                required property var modelData
                Layout.fillWidth: true
                finding: modelData
                onSeek: (u) => root.seek(u)
            }
        }
    }
}
