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

    property var    units:    []
    property string selected: ""

    signal selectionChanged(string unit)

    spacing: 0

    Repeater {
        model: root.units

        Rectangle {
            required property string modelData
            required property int    index

            readonly property bool isActive: modelData === root.selected
            readonly property bool isFirst:  index === 0
            readonly property bool isLast:   index === root.units.length - 1

            width:  Theme.sp(36)
            height: Theme.sp(34)
            color:  isActive ? Theme.colorAccentLight : Theme.colorBg2

            // Per-corner radius: left button gets left-side radius, right button gets right-side radius
            topLeftRadius:     isFirst ? Theme.radius : 0
            bottomLeftRadius:  isFirst ? Theme.radius : 0
            topRightRadius:    isLast  ? Theme.radius : 0
            bottomRightRadius: isLast  ? Theme.radius : 0

            // Border via thin overlay rectangles (avoids border fighting between adjacent buttons)
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 1
                border.color: parent.isActive ? Theme.colorAccent : Theme.colorBorderStrong
                topLeftRadius:     parent.topLeftRadius
                bottomLeftRadius:  parent.bottomLeftRadius
                topRightRadius:    parent.topRightRadius
                bottomRightRadius: parent.bottomRightRadius
            }

            Text {
                anchors.centerIn: parent
                text:           modelData
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color:          parent.isActive ? Theme.colorAccent : Theme.colorText3
            }

            MouseArea {
                anchors.fill: parent
                cursorShape:  Qt.PointingHandCursor
                onClicked:    root.selectionChanged(modelData)
            }
        }
    }
}
