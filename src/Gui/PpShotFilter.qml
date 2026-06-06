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

// Filter popover content — traffic-light quality bands (same colours as the
// card score pills), an exact star-rating picker (only shots rated exactly N;
// tap the same star again to clear), and a has-video toggle. Pure bindings to
// the carousel's ShotFilterProxyModel Q_PROPERTYs; the filtering itself lives
// in C++.

import QtQuick
import PinPointStudio

Item {
    id: root

    required property var proxy   // the carousel's ShotFilterProxyModel

    implicitWidth:  Theme.sp(256)
    implicitHeight: content.implicitHeight + Theme.sp(27)

    Column {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: Theme.sp(14); rightMargin: Theme.sp(14); topMargin: Theme.sp(13) }

        Item {   // header: FILTER · Clear all
            width: parent.width; height: clearText.implicitHeight
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text:           qsTr("FILTER")
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:          Theme.colorText3
            }
            Text {
                id: clearText
                anchors.right: parent.right
                text:           qsTr("Clear all")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorAccent
                opacity:        root.proxy.filterActive ? 1.0 : 0.45
                MouseArea {
                    anchors.fill:    parent
                    anchors.margins: -Theme.sp(4)
                    cursorShape:     Qt.PointingHandCursor
                    onClicked:       root.proxy.clearAll()
                }
            }
        }

        Item { width: 1; height: Theme.sp(13) }

        Text {
            text:           qsTr("QUALITY")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel
            color:          Theme.colorText3
        }

        Item { width: 1; height: Theme.sp(7) }

        Row {   // traffic-light band chips — exact band select, tap again to clear
            width: parent.width
            spacing: Theme.sp(6)

            Repeater {
                model: Theme.qualityBands

                Rectangle {
                    readonly property bool bandSelected: root.proxy.qualityLo === modelData.lo

                    width:  (parent.width - Theme.sp(18)) / 4
                    height: Theme.sp(32)
                    radius: Theme.radius
                    color:  bandSelected ? Theme.qualityColor(modelData.lo)
                                         : Theme.qualityColorLight(modelData.lo)
                    border.width: 1
                    border.color: bandSelected ? Theme.colorSurface : Theme.qualityColor(modelData.lo)

                    Rectangle {   // selection ring
                        anchors.fill: parent
                        anchors.margins: -Theme.sp(3)
                        radius:  Theme.radius + Theme.sp(2)
                        visible: bandSelected
                        color:   "transparent"
                        border.width: 1
                        border.color: Theme.qualityColor(modelData.lo)
                    }

                    Text {
                        anchors.centerIn: parent
                        text:           modelData.label
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          bandSelected ? (Theme.dark ? Theme.colorBg : "#FFFFFF")
                                                     : Theme.qualityColor(modelData.lo)
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        // Single atomic call — see ShotFilterProxyModel::setQualityBand.
                        onClicked: root.proxy.setQualityBand(bandSelected ? -1 : modelData.lo,
                                                             bandSelected ? -1 : modelData.hi)
                    }
                }
            }
        }

        Item { width: 1; height: Theme.sp(13) }

        Text {
            text:           qsTr("RATING")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel
            color:          Theme.colorText3
        }

        Item { width: 1; height: Theme.sp(7) }

        Item {   // exact star-rating picker + hint
            width: parent.width; height: ratingStars.implicitHeight
            PpStarRating {
                id: ratingStars
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                interactive: true
                value:       root.proxy.ratingFilter
                starSize:    Theme.sp(19)
                spacing:     Theme.sp(4)
                onRated: (n) => root.proxy.ratingFilter = n
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text:           root.proxy.ratingFilter > 0 ? qsTr("Exactly %1★").arg(root.proxy.ratingFilter)
                                                            : qsTr("Any")
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color:          Theme.colorText3
            }
        }

        Item { width: 1; height: Theme.sp(13) }

        Rectangle {   // hairline before the toggle group
            width: parent.width; height: 1
            color: Theme.colorBorderMid
            opacity: Theme.borderOpacityNormal
        }

        Item { width: 1; height: Theme.sp(13) }

        Item {   // has-video toggle (the CamerasPanel TogglePill idiom)
            width: parent.width; height: toggle.height
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text:           qsTr("Has video only")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText
            }
            Rectangle {
                id: toggle
                anchors.right: parent.right
                width:  Theme.sp(34)
                height: Theme.sp(18)
                radius: Theme.sp(9)
                color:  root.proxy.hasVideoOnly ? Theme.colorAccent : Theme.colorBg3
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                Rectangle {
                    width:  Theme.sp(12)
                    height: Theme.sp(12)
                    radius: Theme.sp(6)
                    color:  "white"
                    anchors.verticalCenter: parent.verticalCenter
                    x: root.proxy.hasVideoOnly ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                    Behavior on x { NumberAnimation { duration: Theme.durationFast } }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    root.proxy.hasVideoOnly = !root.proxy.hasVideoOnly
                }
            }
        }
    }
}
