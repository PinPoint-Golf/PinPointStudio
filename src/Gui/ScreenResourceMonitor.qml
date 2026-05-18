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
import QtQuick.Controls
import PinPoint

Item {
    id: root

    property string snapshotAgeText: "refreshed just now"

    // Refresh data every 500ms, but only while this screen is visible.
    // StackLayout sets non-active children to visible: false, so running: visible
    // gates the timer automatically without any extra wiring.
    Timer {
        interval: 500
        running: visible
        repeat: true
        onTriggered: {
            resourceMonitor.refresh()
            var age = Math.round(resourceMonitor.snapshotAgeMs / 1000)
            snapshotAgeText = age <= 1 ? "refreshed just now" : "refreshed " + age + " s ago"
        }
    }

    // ── Left column: Devices ──────────────────────────────────────────────────
    ScrollView {
        id: leftScroll
        width: 320
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
            width: 320
            padding: 20
            spacing: 12

            // Section label
            Text {
                text: "DEVICES"
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
                bottomPadding: 8
            }

            Repeater {
                model: resourceMonitor.devices

                RmDeviceCard {
                    deviceData: modelData
                    width: 280
                }
            }

            // Age text at bottom of devices column
            Text {
                text: root.snapshotAgeText
                font.family: Theme.fontData
                font.pixelSize: 9
                color: Theme.colorText3
                topPadding: 4
            }
        }
    }

    // Vertical divider
    Rectangle {
        width: 1
        anchors { top: parent.top; bottom: parent.bottom; left: leftScroll.right }
        color: Theme.colorBorderMid
        opacity: 0.6
    }

    // ── Right column: Event buffer ────────────────────────────────────────────
    ScrollView {
        id: rightScroll
        anchors {
            top: parent.top; bottom: parent.bottom
            left: leftScroll.right; leftMargin: 1
            right: parent.right
        }
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
            width: rightScroll.availableWidth
            padding: 20
            spacing: 16

            // Section label
            Text {
                text: "EVENT BUFFER"
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
                bottomPadding: 8
            }

            // Buffer state bar
            Rectangle {
                width: parent.width - 40
                height: 72
                radius: Theme.radiusLg
                border.width: 1
                border.color: Theme.colorBorderMid
                color: Theme.colorSurface

                Row {
                    anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
                    spacing: 12

                    // Status dot
                    Rectangle {
                        width: 7
                        height: 7
                        radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: {
                            var s = resourceMonitor.bufferState
                            if (s === "capturing") return Theme.colorGood
                            if (s === "paused")    return Theme.colorWarn
                            return Theme.colorBorderStrong
                        }
                    }

                    // State label
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: resourceMonitor.bufferState.toUpperCase()
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzDataSm
                        font.letterSpacing: Theme.trackingMicro
                        color: {
                            var s = resourceMonitor.bufferState
                            if (s === "capturing") return Theme.colorGood
                            if (s === "paused")    return Theme.colorWarn
                            return Theme.colorText3
                        }
                        width: 80
                    }

                    // Divider
                    Rectangle {
                        width: 1
                        height: 40
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.colorBorderMid
                    }

                    // KV pairs
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 28

                        // Total events
                        Column {
                            spacing: 2
                            Text {
                                text: "EVENTS"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                            Text {
                                text: resourceMonitor.totalEventsStr
                                font.family: Theme.fontData
                                font.pixelSize: Theme.fontSzData
                                font.weight: Font.Light
                                color: Theme.colorText
                            }
                            Text {
                                text: "written"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                color: Theme.colorText3
                            }
                        }

                        // Timeline entries
                        Column {
                            spacing: 2
                            Text {
                                text: "TIMELINE"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                            Text {
                                text: resourceMonitor.timelineEntriesStr
                                font.family: Theme.fontData
                                font.pixelSize: Theme.fontSzData
                                font.weight: Font.Light
                                color: Theme.colorText
                            }
                            Text {
                                text: "entries"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                color: Theme.colorText3
                            }
                        }

                        // Source count
                        Column {
                            spacing: 2
                            Text {
                                text: "SOURCES"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                            Text {
                                text: resourceMonitor.sourceCount.toString()
                                font.family: Theme.fontData
                                font.pixelSize: Theme.fontSzData
                                font.weight: Font.Light
                                color: Theme.colorText
                            }
                            Text {
                                text: "active"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                color: Theme.colorText3
                            }
                        }
                    }
                }
            }

            // Warning notices
            Column {
                visible: resourceMonitor.warnings.length > 0
                width: parent.width - 40
                spacing: 8

                Repeater {
                    model: resourceMonitor.warnings

                    RmWarningNotice {
                        message: modelData
                        width: parent.width
                    }
                }
            }

            // Sources section
            Column {
                width: parent.width - 40
                spacing: 0

                // Section label
                Text {
                    text: "SOURCES"
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText3
                    bottomPadding: 8
                }

                // Header row
                Rectangle {
                    width: parent.width
                    height: 30
                    color: Theme.colorBg2
                    radius: Theme.radiusLg

                    // Bottom square to remove bottom radius
                    Rectangle {
                        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                        height: Theme.radiusLg
                        color: Theme.colorBg2
                    }

                    Rectangle {
                        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                        height: 1
                        color: Theme.colorBorderMid
                    }

                    Row {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }

                        property int nameW: parent.width - 10 - 10 - 80 - 72 - 80 - 90 - 60

                        Item {
                            width: parent.nameW
                            height: parent.height
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "SOURCE"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: 80; height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "WRITTEN"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: 72; height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "OVERRUNS"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: 80; height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "BYTES"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: 90; height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "MAX INTER"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: 60; height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "FILL"
                                font.family: Theme.fontData
                                font.pixelSize: 9
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }
                    }
                }

                // Source rows
                Repeater {
                    model: resourceMonitor.sources

                    RmSourceRow {
                        sourceData: modelData
                        isAlternate: index % 2 === 1
                        width: parent.width
                    }
                }

                // Empty state
                Rectangle {
                    visible: resourceMonitor.sources.length === 0
                    width: parent.width
                    height: 40
                    color: Theme.colorSurface

                    Text {
                        anchors.centerIn: parent
                        text: "No sources registered"
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText3
                    }
                }
            }

            // Timeline chart
            RmTimelineChart {
                width: parent.width - 40
                historyData: resourceMonitor.timelineHistory
                bufferState: resourceMonitor.bufferState
            }

            Item { height: 4 }
        }
    }
}
