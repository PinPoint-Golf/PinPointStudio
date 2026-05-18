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

Rectangle {
    id: root

    property var  athleteData: ({})
    property bool isSelected:  false

    signal clicked()

    height:       116
    radius:       Theme.radiusLg
    color:        isSelected ? Theme.colorAccentLight : Theme.colorSurface
    border.width: 1
    border.color: isSelected ? Theme.colorAccent : Theme.colorBorderMid
    clip:         true

    readonly property var avatarColors: [
        "#A8C4E0", "#7EBFAA", "#E0C4A8", "#C4A8E0", "#A8E0C4", "#E0A8C4"
    ]
    readonly property string avatarColor: {
        const uuid = athleteData.uuid || ""
        return uuid.length > 0
            ? avatarColors[uuid.charCodeAt(0) % avatarColors.length]
            : avatarColors[0]
    }

    Column {
        anchors {
            left:    parent.left
            right:   parent.right
            top:     parent.top
            margins: 14
        }
        spacing: 4

        // Initials circle
        Rectangle {
            width:        28
            height:       28
            radius:       14
            color:        root.avatarColor
            border.width: 1
            border.color: Qt.darker(root.avatarColor, 1.3)

            Text {
                anchors.centerIn: parent
                text:           athleteData.initials || "?"
                font.family:    Theme.fontData
                font.pixelSize: 10
                color:          Qt.darker(root.avatarColor, 2.2)
            }
        }

        // Name
        Text {
            width:          parent.width
            text:           athleteData.name || ""
            font.family:    Theme.fontBody
            font.pixelSize: 13
            font.weight:    Font.Normal
            color:          Theme.colorText
            elide:          Text.ElideRight
        }

        // Meta: handedness · handicap · sessions
        Text {
            width:          parent.width
            font.family:    Theme.fontData
            font.pixelSize: 10
            color:          Theme.colorText3
            elide:          Text.ElideRight
            text: {
                const hand = (athleteData.handedness || "R")[0] + "H"
                const hcp  = (athleteData.handicap >= 0)
                                 ? (Math.round(athleteData.handicap) + " hcp")
                                 : "no hcp"
                const sess = (athleteData.sessionCount || 0) + " sessions"
                return hand + " · " + hcp + " · " + sess
            }
        }

        // Last seen
        Text {
            width:          parent.width
            font.family:    Theme.fontData
            font.pixelSize: 10
            color:          Theme.colorText3
            text: {
                const ts = athleteData.lastSessionAt || 0
                if (ts === 0) return "no sessions yet"
                const days = Math.floor((Date.now() / 1000 - ts) / 86400)
                if (days === 0) return "last seen today"
                if (days === 1) return "last seen yesterday"
                return "last " + days + " days ago"
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape:  Qt.PointingHandCursor
        onClicked:    root.clicked()
    }
}
