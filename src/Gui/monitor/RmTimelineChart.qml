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

Rectangle {
    id: root

    property var    historyData: []
    property string bufferState: ""

    radius: Theme.radiusLg
    border.width: 1
    border.color: Theme.colorBorderMid
    color: Theme.colorSurface
    height: Theme.sp(180)
    clip: true

    // Title
    Text {
        id: chartTitle
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: Theme.sp(14) }
        text: qsTr("TIMELINE INDEX — ENTRIES PER 2 S WINDOW")
        font.family: Theme.fontData
        font.pixelSize: Theme.fontSzMicro
        font.letterSpacing: Theme.trackingMicro
        color: Theme.colorText3
    }

    // Footer
    Item {
        id: footer
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right; margins: Theme.sp(14) }
        height: Theme.sp(14)

        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: "−" + (root.historyData.length * 2) + " s"
            font.family: Theme.fontData
            font.pixelSize: Theme.sp(9)
            color: Theme.colorText3
        }

        Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: qsTr("now")
            font.family: Theme.fontData
            font.pixelSize: Theme.sp(9)
            color: Theme.colorText3
        }
    }

    // Bar chart — sits between title and footer
    Item {
        id: chartArea
        anchors {
            top: chartTitle.bottom
            topMargin: 6
            bottom: footer.top
            bottomMargin: 4
            left: parent.left
            right: parent.right
            leftMargin: Theme.sp(14)
            rightMargin: Theme.sp(14)
        }

        property int  barCount: root.historyData.length > 0 ? root.historyData.length : 1
        property real maxVal: {
            var m = 1
            for (var i = 0; i < root.historyData.length; i++)
                m = Math.max(m, root.historyData[i])
            return m
        }
        property real barW: barCount > 1
            ? (width - 3.0 * (barCount - 1)) / barCount
            : width

        Row {
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            spacing: Theme.sp(3)

            Repeater {
                model: root.historyData

                Item {
                    width: chartArea.barW
                    height: chartArea.height

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Math.max(2, modelData / chartArea.maxVal * chartArea.height)
                        radius: Theme.sp(1)
                        color: Theme.colorAccent
                        opacity: index === root.historyData.length - 1 ? 0.8 : 0.5

                        Behavior on height {
                            NumberAnimation { duration: 400; easing.type: Easing.OutCubic }
                        }
                    }
                }
            }
        }
    }
}
