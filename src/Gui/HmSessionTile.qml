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

    property var sessionData

    width:  148
    height: 88
    radius: Theme.radiusLg
    color:  hoverArea.containsMouse ? Theme.colorAccentLight : Theme.colorBg
    border.width: 1
    border.color: hoverArea.containsMouse ? Theme.colorAccent : Theme.colorBorderMid

    Column {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
        spacing: 0

        Item { width: 1; height: 2 }

        Rectangle {
            height: 18
            width:  badgeLabel.implicitWidth + 16
            radius: 20
            color:  Theme.colorAccentLight
            border.width: 1
            border.color: Theme.colorAccentMid

            Text {
                id: badgeLabel
                anchors.centerIn: parent
                text:           root.sessionData ? (root.sessionData.mode || "Full swing") : ""
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color:          Theme.colorAccent
            }
        }

        Item { width: 1; height: 4 }

        Text {
            text:           root.sessionData ? (root.sessionData.date || "—") : ""
            font.family:    Theme.fontData
            font.pixelSize: 9
            color:          Theme.colorText3
        }

        Text {
            text:           root.sessionData ? (root.sessionData.value || "—") : ""
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzData
            font.weight:    Font.Light
            color:          Theme.colorText
            lineHeight:     1
        }

        Text {
            text:           root.sessionData ? (root.sessionData.metricLabel || "") : ""
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            color:          Theme.colorText3
        }

        Text {
            text:           root.sessionData ? (root.sessionData.captures || "") : ""
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            color:          Theme.colorText3
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape:  Qt.PointingHandCursor
    }
}
