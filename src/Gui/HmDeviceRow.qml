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

    property var deviceData

    height: 44
    color:  "transparent"

    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 1
        color:  Theme.colorBorder
    }

    Row {
        anchors.fill: parent
        spacing:      0

        Item {
            width:  16
            height: parent.height

            Rectangle {
                width:  7
                height: 7
                radius: 3.5
                anchors.centerIn: parent
                color: {
                    if (!root.deviceData) return Theme.colorBorderStrong
                    var d = root.deviceData
                    if (d.status === "streaming" || d.status === "connected") return Theme.colorGood
                    if (d.hasWarning || d.status === "stalled") return Theme.colorWarn
                    return Theme.colorBorderStrong
                }
            }
        }

        Text {
            width:              parent.width - 16 - statusDetail.width
            height:             parent.height
            leftPadding:        9
            text:               root.deviceData ? root.deviceData.name : ""
            font.family:        Theme.fontBody
            font.pixelSize:     Theme.fontSzBody
            color:              Theme.colorText
            elide:              Text.ElideRight
            verticalAlignment:  Text.AlignVCenter
        }

        Text {
            id: statusDetail
            width:               64
            height:              parent.height
            text: {
                if (!root.deviceData) return "—"
                var d = root.deviceData
                if (d.kind === "Camera")
                    return d.status === "streaming" ? d.dataRateHz.toFixed(0) + " fps" : "—"
                return d.status === "connected" ? d.dataRateHz.toFixed(0) + " Hz" : "disconnected"
            }
            font.family:         Theme.fontData
            font.pixelSize:      Theme.fontSzMicro
            font.letterSpacing:  Theme.trackingData
            color:               Theme.colorText3
            horizontalAlignment: Text.AlignRight
            verticalAlignment:   Text.AlignVCenter
        }
    }
}
