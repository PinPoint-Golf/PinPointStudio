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

// Shot-quality score pill — fill graded by quartile via Theme.qualityColor.
// Small variant sits on the shot card; large variant ("NN /100") heads the
// review panel. Text colour flips with Theme.dark (the toolbar count-badge
// idiom) so it stays legible across all themes.

import QtQuick
import PinPointStudio

Rectangle {
    id: root

    property int  score: 0
    property bool large: false

    implicitHeight: large ? Theme.sp(24) : Theme.sp(17)
    implicitWidth:  pillRow.implicitWidth + (large ? Theme.sp(22) : Theme.sp(14))
    radius:         height / 2
    // Half-alpha fill keeps the quartile hue without overpowering the card
    // imagery; the text stays at full opacity for legibility.
    color:          Qt.alpha(Theme.qualityColor(root.score), 0.5)

    Row {
        id: pillRow
        anchors.centerIn: parent
        spacing: Theme.sp(2)

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text:           root.score
            font.family:    Theme.fontData
            font.pixelSize: root.large ? Theme.fontSzHeading : Theme.fontSzMicro
            color:          Theme.dark ? Theme.colorBg : "#FFFFFF"
        }
        Text {
            visible:        root.large
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: Theme.sp(2)
            text:           "/100"
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            opacity:        0.8
            color:          Theme.dark ? Theme.colorBg : "#FFFFFF"
        }
    }
}
