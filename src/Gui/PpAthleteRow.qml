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

import QtQuick
import PinPoint

Item {
    id: root

    property var  athleteData: ({})
    property bool isSelected:  false
    property int  rowIndex:    0

    signal clicked()

    height: Theme.sp(44)

    Rectangle {
        anchors.fill: parent
        color: root.isSelected
                   ? Theme.colorAccentLight
                   : (root.rowIndex % 2 === 0 ? Theme.colorSurface : Theme.colorBg)
    }

    readonly property var avatarColors: [
        "#A8C4E0", "#7EBFAA", "#E0C4A8", "#C4A8E0", "#A8E0C4", "#E0A8C4"
    ]
    readonly property string avatarColor: {
        const uuid = athleteData.uuid || ""
        return uuid.length > 0
            ? avatarColors[uuid.charCodeAt(0) % avatarColors.length]
            : avatarColors[0]
    }

    Row {
        anchors {
            left:           parent.left
            right:          parent.right
            verticalCenter: parent.verticalCenter
            leftMargin:     Theme.sp(12)
            rightMargin:    Theme.sp(12)
        }
        spacing: Theme.sp(12)

        // Avatar circle
        Rectangle {
            width:        Theme.sp(28)
            height:       Theme.sp(28)
            radius:       Theme.sp(14)
            color:        root.avatarColor
            border.width: 1
            border.color: Qt.darker(root.avatarColor, 1.3)
            anchors.verticalCenter: parent.verticalCenter

            Text {
                anchors.centerIn: parent
                text:           athleteData.initials || "?"
                font.family:    Theme.fontData
                font.pixelSize: Theme.sp(10)
                color:          Qt.darker(root.avatarColor, 2.2)
            }
        }

        // Name — fills remaining space
        Text {
            width:          parent.width - Theme.sp(28) - Theme.sp(60) - Theme.sp(60) - Theme.sp(60) - Theme.sp(72) - Theme.sp(16) - parent.spacing * 5
            anchors.verticalCenter: parent.verticalCenter
            text:           athleteData.name || ""
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            color:          Theme.colorText
            elide:          Text.ElideRight
        }

        // Handedness
        Text {
            width:          Theme.sp(60)
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            text:           (athleteData.handedness || "Right")[0] + "H"
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
        }

        // Handicap
        Text {
            width:          Theme.sp(60)
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            text:           (athleteData.handicap >= 0)
                                ? Math.round(athleteData.handicap) + " hcp"
                                : "—"
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
        }

        // Sessions
        Text {
            width:          Theme.sp(60)
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            text:           (athleteData.sessionCount || 0) + " sess"
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
        }

        // Last seen
        Text {
            width:          Theme.sp(72)
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
            text: {
                const ts = athleteData.lastSessionAt || 0
                if (ts === 0) return "never"
                const days = Math.floor((Date.now() / 1000 - ts) / 86400)
                if (days === 0) return "today"
                if (days === 1) return "yesterday"
                return days + "d ago"
            }
        }

        // Chevron
        Text {
            width:          Theme.sp(16)
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            text:           "›"
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            color:          Theme.colorText3
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape:  Qt.PointingHandCursor
        onClicked:    root.clicked()
    }
}
