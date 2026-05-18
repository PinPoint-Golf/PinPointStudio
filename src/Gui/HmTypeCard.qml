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

    property string iconText:       ""
    property string typeName:       ""
    property string requirement:    ""
    property bool   requirementMet: false
    property bool   isSelected:     false

    signal clicked()

    height: 96
    radius: Theme.radiusLg
    color:  (isSelected || hoverArea.containsMouse) ? Theme.colorAccentLight : Theme.colorSurface
    border.width: 1
    border.color: isSelected            ? Theme.colorAccent
                : hoverArea.containsMouse ? Theme.colorAccentMid
                : Theme.colorBorderMid

    Column {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
        spacing: 0

        Item { width: 1; height: 4 }

        Text {
            text:           root.iconText
            font.pixelSize: 16
            color:          Theme.colorText2
        }

        Item { width: 1; height: 4 }

        Text {
            width:          parent.width
            text:           root.typeName
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            font.weight:    Font.Normal
            color:          Theme.colorText
        }

        Item { width: 1; height: 4 }

        Text {
            text:               root.requirementMet ? "✓ " + root.requirement
                                                    : "⚠ " + root.requirement
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
            color:              root.requirementMet ? Theme.colorGood : Theme.colorWarn
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape:  Qt.PointingHandCursor
        onClicked:    root.clicked()
    }
}
