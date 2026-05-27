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

Row {
    id: root

    property var    options:  []
    property string selected: ""

    signal selectionChanged(string value)

    spacing: Theme.sp(6)

    Repeater {
        model: root.options

        Rectangle {
            required property string modelData
            required property int    index

            height:  Theme.sp(28)
            width:   chipLabel.implicitWidth + Theme.sp(24)
            radius:  Theme.radius
            color:   modelData === root.selected ? Theme.colorAccentLight : "transparent"
            border.width: 1
            border.color: modelData === root.selected ? Theme.colorAccent : Theme.colorBorderStrong

            Text {
                id: chipLabel
                anchors.centerIn: parent
                text:           modelData
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                font.weight:    modelData === root.selected ? Font.Normal : Theme.fontBodyWeight
                color:          modelData === root.selected ? Theme.colorAccent : Theme.colorText2
            }

            MouseArea {
                anchors.fill: parent
                cursorShape:  Qt.PointingHandCursor
                onClicked:    root.selectionChanged(modelData)
            }
        }
    }
}
