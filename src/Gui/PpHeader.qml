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
import QtQuick.Layouts
import PinPoint

Item {
    id: root

    // The screen name shown after the separator. Set by Main.qml.
    property string screenName: ""

    implicitHeight: Theme.headerHeight

    Rectangle { anchors.fill: parent; color: Theme.colorSurface }

    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height:  1
        color:   Theme.colorBorderMid
        opacity: Theme.borderOpacityNormal
    }

    RowLayout {
        anchors.fill:        parent
        anchors.leftMargin:  24
        anchors.rightMargin: 16
        spacing:             16

        Text {
            text:           "Pinpoint"
            font.family:    Theme.fontDisplay
            font.italic:    Theme.fontDisplayItalic
            font.pixelSize: Theme.sp(15)
            color:          Theme.colorText
        }

        Rectangle {
            width:   1
            height:  16
            color:   Theme.colorBorderStrong
            opacity: Theme.borderOpacityNormal
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            text:                root.screenName
            font.family:         Theme.fontBody
            font.pixelSize:      Theme.fontSzLabel
            font.letterSpacing:  Theme.trackingLabel
            font.capitalization: Font.AllUppercase
            color:               Theme.colorText3
        }

        Item { Layout.fillWidth: true }

        // Right content placeholder — populated per-screen in a future task
        Item {
            id: rightSlot
            implicitHeight: parent.height
            implicitWidth:  0
        }
    }
}
