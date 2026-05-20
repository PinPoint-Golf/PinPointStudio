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

    property string message: ""

    radius: Theme.radius
    color: Theme.colorWarnLight
    border.width: 1
    border.color: Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g, Theme.colorWarn.b, 0.4)
    implicitHeight: body.implicitHeight + Theme.sp(22)
    implicitWidth: body.implicitWidth + Theme.sp(28)

    Row {
        id: body
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.margins: Theme.sp(11)
        anchors.topMargin: Theme.sp(11)
        spacing: Theme.sp(10)

        Text {
            text: "⚠"
            font.pixelSize: Theme.sp(14)
            color: Theme.colorWarn
            anchors.top: parent.top
        }

        Column {
            spacing: Theme.sp(3)
            width: parent.width - Theme.sp(24)

            Text {
                text: qsTr("WARNING")
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color: Theme.colorWarn
            }

            Text {
                text: root.message
                wrapMode: Text.WordWrap
                width: parent.width
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight: Font.Light
                color: Theme.colorText2
            }
        }
    }
}
