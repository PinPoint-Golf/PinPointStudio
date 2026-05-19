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
    signal addAthleteRequested()

    // Scrollable content centred in the available area
    Flickable {
        anchors.fill:       parent
        contentWidth:       width
        contentHeight:      contentCol.implicitHeight + 80
        clip:               true

        Column {
            id: contentCol
            anchors.horizontalCenter: parent.horizontalCenter
            width:   Math.min(parent.width - 80, Theme.sp(500))
            spacing: 0
            y:       40

            // Eyebrow
            Text {
                width:              parent.width
                text:               "GOLF SWING ANALYSIS"
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:              Theme.colorText3
                bottomPadding:      12
            }

            // Title
            Text {
                width:           parent.width
                text:            "Welcome to Pinpoint"
                font.family:     Theme.fontDisplay
                font.italic:     Theme.fontDisplayItalic
                font.pixelSize:  Theme.fontSzDisplay
                color:           Theme.colorText
                wrapMode:        Text.WordWrap
                lineHeight:      1.1
                bottomPadding:   8
            }

            // Description
            Text {
                width:          parent.width
                text:           "An open-source workshop for understanding the golf swing — cameras, IMUs, and ground forces working together to show you what's actually happening."
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                font.weight:    Font.Light
                color:          Theme.colorText2
                wrapMode:       Text.WordWrap
                lineHeight:     1.7
                bottomPadding:  32
            }

            // Add-athlete heading
            Text {
                width:          parent.width
                text:           "Start by adding an athlete"
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                color:          Theme.colorText
                bottomPadding:  4
            }

            // Sub-heading
            Text {
                width:          parent.width
                text:           "Every session belongs to someone. That's usually you."
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Font.Light
                color:          Theme.colorText3
                bottomPadding:  20
            }

            // CTA button
            Rectangle {
                width:  parent.width
                height: Theme.sp(42)
                radius: Theme.radius
                color:  Theme.colorAccent

                Text {
                    anchors.centerIn: parent
                    text:           "Add your first athlete"
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    color:          Theme.dark ? Theme.colorBg : "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked:   addAthleteRequested()
                }
            }

            // Divider with margins
            Item { width: 1; height: Theme.sp(28) }
            PpDivider { width: parent.width }
            Item { width: 1; height: Theme.sp(28) }

            // Secondary actions row
            Row {
                width:   parent.width
                spacing: Theme.sp(10)

                Repeater {
                    model: [
                        { icon: "⊞", title: "Connect a camera",  desc: "Basler or GenTL over USB3" },
                        { icon: "⌖", title: "Pair wrist IMUs",    desc: "Lead and trail hand sensors" },
                        { icon: "↗", title: "Read the docs",      desc: "Setup guides and hardware" }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        width:  (parent.width - 20) / 3
                        height: secCol.implicitHeight + Theme.sp(28)
                        radius: Theme.radius
                        color:  "transparent"
                        border.width: 1
                        border.color: Theme.colorBorder

                        Column {
                            id: secCol
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.sp(14) }
                            spacing: 0

                            Item { width: 1; height: Theme.sp(14) }

                            Text {
                                text:           modelData.icon
                                font.pixelSize: Theme.sp(16)
                                color:          Theme.colorText2
                                bottomPadding:  7
                            }

                            Text {
                                width:          parent.width
                                text:           modelData.title
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                color:          Theme.colorText
                                bottomPadding:  3
                            }

                            Text {
                                width:          parent.width
                                text:           modelData.desc
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                font.weight:    Font.Light
                                color:          Theme.colorText3
                                wrapMode:       Text.WordWrap
                                lineHeight:     1.4
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape:  Qt.PointingHandCursor
                            onEntered: parent.color = Theme.colorBg2
                            onExited:  parent.color = "transparent"
                        }
                    }
                }
            }

            // Footer
            Item { width: 1; height: Theme.sp(24) }
            Text {
                width:              parent.width
                text:               "Nothing connects to the cloud unless you configure it."
                font.family:        Theme.fontBody
                font.pixelSize:     Theme.fontSzBody2
                font.weight:        Font.Light
                color:              Theme.colorText3
                horizontalAlignment: Text.AlignHCenter
            }
            Item { width: 1; height: Theme.sp(40) }
        }
    }
}
