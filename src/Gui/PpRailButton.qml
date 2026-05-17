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

    property string iconText:  ""
    property string labelText: ""
    property bool   isActive:  false
    property bool   isMuted:   false

    signal clicked()

    implicitWidth:  40
    implicitHeight: buttonCol.implicitHeight

    Column {
        id: buttonCol
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 3

        Rectangle {
            id: bg
            width:  40
            height: 40
            radius: Theme.radius

            color: {
                if (isActive && Theme.aesthetic === "editorial") return Theme.colorAccentLight
                if (isActive) return Theme.colorSurface
                if (!isMuted && mouseArea.containsMouse) return Theme.colorBg3
                return "transparent"
            }

            border.width: (isActive && Theme.aesthetic !== "editorial") ? 1 : 0
            border.color: Theme.colorBorderMid

            // Editorial active: 2px left-edge accent bar
            Rectangle {
                visible: root.isActive && Theme.aesthetic === "editorial"
                x: 0
                y: 0
                width:  2
                height: parent.height
                color:  Theme.colorAccent
            }

            Text {
                anchors.centerIn: parent
                text:            root.iconText
                font.pixelSize:  14
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
            width:                  40
            horizontalAlignment:    Text.AlignHCenter
            text:                   root.labelText.toUpperCase()
            font.family:            Theme.fontBody
            font.pixelSize:         8
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
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
