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
    property string iconText:  ""
    property string titleText: ""

    Column {
        anchors.centerIn: parent
        spacing: Theme.sp(12)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text:           iconText
            font.pixelSize: Theme.sp(32)
            color:          Theme.colorText3
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text:           titleText
            font.family:    Theme.fontDisplay
            font.italic:    Theme.fontDisplayItalic
            font.weight: Theme.fontDisplayWeight
            font.pixelSize: Theme.fontSzDisplay
            color:          Theme.colorText
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text:               qsTr("COMING SOON")
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color:              Theme.colorText3
        }
    }
}
