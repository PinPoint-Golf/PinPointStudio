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

    property var  sourceData
    property bool isAlternate: false

    height: Theme.sp(40)
    color: isAlternate ? Theme.colorBg : Theme.colorSurface

    Row {
        anchors { fill: parent; leftMargin: Theme.sp(10); rightMargin: Theme.sp(10) }
        visible: root.sourceData !== null && root.sourceData !== undefined

        // Source name (fills remaining width)
        Item {
            width: parent.width - Theme.sp(80) - Theme.sp(72) - Theme.sp(80) - Theme.sp(90) - Theme.sp(60)
            height: parent.height

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.sp(6)

                Rectangle {
                    width: Theme.sp(5)
                    height: Theme.sp(5)
                    radius: Theme.sp(3)
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.sourceData.stalled ? Theme.colorWarn : Theme.colorGood
                }

                Text {
                    text: root.sourceData.identifier
                          ? root.sourceData.name + " (" + root.sourceData.identifier + ")"
                          : root.sourceData.name
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzDataSm
                    color: root.sourceData.stalled ? Theme.colorWarn : Theme.colorText2
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // Written — 80px
        Item {
            width: Theme.sp(80)
            height: parent.height
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.sourceData.eventsWrittenStr
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: Theme.colorText2
            }
        }

        // Ring wraps — 72px
        Item {
            width: Theme.sp(72)
            height: parent.height
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.sourceData.eventsOverwrittenStr
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: Theme.colorText2
            }
        }

        // Bytes — 80px
        Item {
            width: Theme.sp(80)
            height: parent.height
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.sourceData.bytesWrittenStr
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: Theme.colorText2
            }
        }

        // Max inter-arrival — 90px
        Item {
            width: Theme.sp(90)
            height: parent.height
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.sourceData.maxInterArrivalStr
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: root.sourceData.maxInterArrivalUs > 100000
                       ? Theme.colorWarn : Theme.colorText2
            }
        }

        // Ring fill bar — 60px
        Item {
            width: Theme.sp(60)
            height: parent.height

            Rectangle {
                width: Theme.sp(44)
                height: Theme.sp(4)
                radius: Theme.sp(2)
                color: Theme.colorBg3
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }

                Rectangle {
                    height: Theme.sp(4)
                    radius: Theme.sp(2)
                    width: parent.width * Math.min(1.0, root.sourceData.ringFillFraction)
                    color: root.sourceData.ringFillFraction > 0.85
                           ? Theme.colorWarn : Theme.colorGood
                    Behavior on width {
                        NumberAnimation { duration: 600; easing.type: Easing.OutCubic }
                    }
                }
            }
        }
    }
}
