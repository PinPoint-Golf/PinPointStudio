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
    property string description:    ""
    property int    camerasRequired:  2
    property int    imusRequired:     3
    property int    camerasCount:     0
    property int    imusCount:        0
    property bool   camerasOptional:  false
    property bool   camerasMet:       camerasOptional || camerasCount >= camerasRequired
    property bool   imusMet:          imusCount >= imusRequired
    property bool   isSelected:       false

    signal clicked()

    implicitHeight: contentCol.implicitHeight + Theme.sp(24)
    radius: Theme.radiusLg
    color:  (isSelected || hoverArea.containsMouse) ? Theme.colorAccentLight : Theme.colorSurface
    border.width: 1
    border.color: isSelected            ? Theme.colorAccent
                : hoverArea.containsMouse ? Theme.colorAccentMid
                : Theme.colorBorderMid

    Column {
        id: contentCol
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.sp(12) }
        spacing: 0

        Item { width: 1; height: Theme.sp(4) }

        Text {
            text:           root.iconText
            font.pixelSize: Theme.sp(32)
            color:          Theme.colorText2
        }

        Item { width: 1; height: Theme.sp(4) }

        Text {
            width:          parent.width
            text:           root.typeName
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            font.weight:    Font.Normal
            color:          Theme.colorText
        }

        Item { width: 1; height: Theme.sp(4) }

        Text {
            width:          parent.width
            text:           root.description
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Font.Normal
            color:          Theme.colorText3
            wrapMode:       Text.Wrap
            visible:        root.description !== ""
        }

        Item { width: 1; height: Theme.sp(6) }

        Row {
            spacing: Theme.sp(12)

            Text {
                text:               root.camerasOptional
                                        ? "✓ Optional camera"
                                        : (root.camerasMet ? "✓ " : "⚠ ")
                                          + root.camerasRequired
                                          + (root.camerasRequired === 1 ? " camera" : " cameras")
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingData
                color:              root.camerasOptional
                                        ? (root.camerasCount >= 1 ? Theme.colorGood : Theme.colorWarn)
                                        : (root.camerasMet ? Theme.colorGood : Theme.colorWarn)
            }

            Text {
                text:               root.imusMet
                                        ? "✓ " + root.imusRequired + " IMUs"
                                        : "⚠ " + root.imusRequired + " IMUs"
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingData
                color:              root.imusMet ? Theme.colorGood : Theme.colorWarn
            }
        }

        Item { width: 1; height: Theme.sp(4) }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape:  Qt.PointingHandCursor
        onClicked:    root.clicked()
    }
}
