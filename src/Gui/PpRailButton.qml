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
import PinPointStudio

Item {
    id: root

    property string iconText:  ""
    property string labelText: ""
    property bool   isActive:  false
    property bool   isMuted:   false

    signal clicked()

    implicitWidth:  Theme.sp(60)
    implicitHeight: buttonCol.implicitHeight

    Column {
        id: buttonCol
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.sp(3)

        Rectangle {
            id: bg
            width:  Theme.sp(60)
            height: Theme.sp(60)
            radius: Theme.radius

            color: {
                if (isActive && (Theme.aesthetic === "editorial" || Theme.aesthetic === "vector")) return Theme.colorAccentLight
                if (isActive) return Theme.colorSurface
                if (!isMuted && mouseArea.containsMouse) return Theme.colorBg3
                return "transparent"
            }

            border.width: (isActive && Theme.aesthetic !== "editorial" && Theme.aesthetic !== "vector") ? 1 : 0
            border.color: Theme.colorBorderMid

            // Editorial / Vector active: 2px left-edge accent bar
            Rectangle {
                visible: root.isActive && (Theme.aesthetic === "editorial" || Theme.aesthetic === "vector")
                x: 0
                y: 0
                width:  2
                height: parent.height
                color:  Theme.colorAccent
            }

            Text {
                anchors.centerIn: parent
                text:            root.iconText
                // Per-glyph compensation so all rail icons share a common
                // visual size despite differing ink heights (platform-aware,
                // see Theme.symbolScale).
                font.pixelSize:  Math.round(Theme.sp(21) * Theme.symbolScale(root.iconText))
                font.family:     Theme.fontSymbol
                color: {
                    if (root.isActive) return Theme.colorAccent
                    if (!root.isMuted && mouseArea.containsMouse) return Theme.colorText2
                    return Theme.colorText3
                }
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationFast }
                }
            }
        }

        Text {
            id: labelItem
            width:                  Theme.sp(60)
            horizontalAlignment:    Text.AlignHCenter
            text:                   root.labelText.toUpperCase()
            font.family:            Theme.fontBody
            font.pixelSize:         Theme.sp(8)
            font.letterSpacing:     Theme.trackingMicro
            color: {
                if (root.isActive) return Theme.colorAccent
                if (!root.isMuted && mouseArea.containsMouse) return Theme.colorText2
                return Theme.colorText3
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: !root.isMuted
        enabled:      !root.isMuted
        cursorShape:  root.isMuted ? Qt.ArrowCursor : Qt.PointingHandCursor
        onClicked:    root.clicked()
    }
}
