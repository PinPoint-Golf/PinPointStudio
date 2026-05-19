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
        width: Theme.sp(320)
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
            width: Theme.sp(320)
            padding: Theme.sp(20)
            spacing: Theme.sp(12)

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
                    width: Theme.sp(280)
                }
            }

            // Age text at bottom of devices column
            Text {
                text: root.snapshotAgeText
                font.family: Theme.fontData
                font.pixelSize: Theme.sp(9)
                color: Theme.colorText3
                topPadding: Theme.sp(4)
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
            padding: Theme.sp(20)
            spacing: Theme.sp(16)

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
                height: Theme.sp(72)
                radius: Theme.radiusLg
                border.width: 1
                border.color: Theme.colorBorderMid
                color: Theme.colorSurface

                Row {
                    anchors { fill: parent; leftMargin: Theme.sp(16); rightMargin: Theme.sp(16) }
                    spacing: Theme.sp(12)

                    // Status dot
                    Rectangle {
                        width: Theme.sp(7)
                        height: Theme.sp(7)
                        radius: Theme.sp(4)
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
                        width: Theme.sp(80)
                    }

                    // Divider
                    Rectangle {
                        width: 1
                        height: Theme.sp(40)
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.colorBorderMid
                    }

                    // KV pairs
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.sp(28)

                        // Total events
                        Column {
                            spacing: Theme.sp(2)
                            Text {
                                text: "EVENTS"
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
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
                                font.pixelSize: Theme.sp(9)
                                color: Theme.colorText3
                            }
                        }

                        // Timeline entries
                        Column {
                            spacing: Theme.sp(2)
                            Text {
                                text: "TIMELINE"
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
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
                                font.pixelSize: Theme.sp(9)
                                color: Theme.colorText3
                            }
                        }

                        // Source count
                        Column {
                            spacing: Theme.sp(2)
                            Text {
                                text: "SOURCES"
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
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
                                font.pixelSize: Theme.sp(9)
                                color: Theme.colorText3
                            }
                        }
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
                    height: Theme.sp(30)
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
                        anchors { fill: parent; leftMargin: Theme.sp(10); rightMargin: Theme.sp(10) }

                        property int nameW: parent.width - Theme.sp(10) - Theme.sp(10) - Theme.sp(80) - Theme.sp(72) - Theme.sp(80) - Theme.sp(90) - Theme.sp(60)

                        Item {
                            width: parent.nameW
                            height: parent.height
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "SOURCE"
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: Theme.sp(80); height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "WRITTEN"
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: Theme.sp(72); height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "WRAPS"
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: Theme.sp(80); height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "BYTES"
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: Theme.sp(90); height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "MAX INTER"
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorText3
                            }
                        }

                        Item {
                            width: Theme.sp(60); height: parent.height
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: "FILL"
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
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
                    height: Theme.sp(40)
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

            // ── Message log (grows downward; lives at bottom) ─────────────────
            Column {
                id: msgLogColumn
                width: parent.width - 40
                spacing: 0

                // Active severity filters — reassign (don't mutate) to trigger reactivity
                property var    activeFilters: ({"INFO": true, "WARN": true, "ERROR": true, "FATAL": true})
                property string textFilter:    ""

                function toggleFilter(sev) {
                    var f = Object.assign({}, activeFilters)
                    f[sev] = !f[sev]
                    activeFilters = f
                }

                function severityColor(sev) {
                    if (sev === "WARN")                    return Theme.colorWarn
                    if (sev === "ERROR" || sev === "FATAL") return Theme.colorError
                    if (sev === "INFO")                    return Theme.colorText2
                    return Theme.colorText3  // DEBUG
                }

                property var filteredLog: {
                    var filters = activeFilters
                    var needle  = textFilter.toLowerCase()
                    var log = resourceMonitor.messageLog
                    var result = []
                    for (var i = 0; i < log.length; i++) {
                        var e = log[i]
                        if (filters[e.severity] === false) continue
                        if (needle !== "" && e.message.toLowerCase().indexOf(needle) === -1) continue
                        result.push(e)
                    }
                    return result
                }

                // Header
                Item {
                    width: parent.width
                    height: Theme.sp(32)

                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        text: "MESSAGE LOG"
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorText3
                    }

                    // Entry count
                    Text {
                        visible: resourceMonitor.messageLog.length > 0
                        anchors { left: parent.left; leftMargin: Theme.sp(100); verticalCenter: parent.verticalCenter }
                        text: msgLogColumn.filteredLog.length + " entries"
                        font.family: Theme.fontData
                        font.pixelSize: Theme.sp(9)
                        color: Theme.colorText3
                    }

                    // Clear button
                    Rectangle {
                        id: clearBtn
                        visible: resourceMonitor.messageLog.length > 0
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        width: clearLbl.implicitWidth + Theme.sp(16)
                        height: Theme.sp(18)
                        radius: Theme.radius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.colorBorderMid

                        Text {
                            id: clearLbl
                            anchors.centerIn: parent
                            text: "Clear"
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.sp(10)
                            color: Theme.colorText3
                        }

                        TapHandler  { onTapped: resourceMonitor.clearLog() }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }

                    // Inline text filter box
                    Rectangle {
                        id: filterBox
                        visible: resourceMonitor.messageLog.length > 0
                        anchors { right: filterChips.left; rightMargin: Theme.sp(8); verticalCenter: parent.verticalCenter }
                        width: Theme.sp(120)
                        height: Theme.sp(18)
                        radius: Theme.radius
                        color: Theme.colorBg2
                        border.width: 1
                        border.color: filterInput.activeFocus ? Theme.colorAccent : Theme.colorBorderMid

                        Text {
                            anchors { left: parent.left; leftMargin: Theme.sp(7); verticalCenter: parent.verticalCenter }
                            visible: filterInput.text.length === 0
                            text: "Filter…"
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.sp(10)
                            color: Theme.colorText3
                        }

                        TextInput {
                            id: filterInput
                            anchors { left: parent.left; right: clearFilter.left; leftMargin: Theme.sp(7); rightMargin: Theme.sp(4); verticalCenter: parent.verticalCenter }
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.sp(10)
                            color: Theme.colorText
                            selectionColor: Theme.colorAccentMid
                            selectedTextColor: Theme.colorText
                            clip: true
                            onTextChanged: msgLogColumn.textFilter = text
                        }

                        Text {
                            id: clearFilter
                            anchors { right: parent.right; rightMargin: Theme.sp(5); verticalCenter: parent.verticalCenter }
                            visible: filterInput.text.length > 0
                            text: "×"
                            font.pixelSize: Theme.sp(12)
                            color: Theme.colorText3
                            TapHandler   { onTapped: { filterInput.text = ""; filterInput.forceActiveFocus() } }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }
                    }

                    // Severity filter chips
                    Row {
                        id: filterChips
                        visible: resourceMonitor.messageLog.length > 0
                        anchors { right: clearBtn.left; rightMargin: Theme.sp(8); verticalCenter: parent.verticalCenter }
                        spacing: Theme.sp(4)

                        Repeater {
                            model: ["INFO", "WARN", "ERROR"]

                            Rectangle {
                                required property string modelData

                                readonly property bool   active:   msgLogColumn.activeFilters[modelData] !== false
                                readonly property color  sevColor: msgLogColumn.severityColor(modelData)

                                width:  chipLbl.implicitWidth + Theme.sp(10)
                                height: Theme.sp(18)
                                radius: Theme.sp(9)
                                color: active
                                    ? Qt.rgba(sevColor.r, sevColor.g, sevColor.b, 0.15)
                                    : "transparent"
                                border.width: 1
                                border.color: active
                                    ? Qt.rgba(sevColor.r, sevColor.g, sevColor.b, 0.4)
                                    : Theme.colorBorderMid

                                Text {
                                    id: chipLbl
                                    anchors.centerIn: parent
                                    text: parent.modelData
                                    font.family: Theme.fontData
                                    font.pixelSize: Theme.sp(9)
                                    font.letterSpacing: Theme.trackingMicro
                                    color: parent.active ? parent.sevColor : Theme.colorText3
                                }

                                TapHandler  { onTapped: msgLogColumn.toggleFilter(parent.modelData) }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                        }
                    }
                }

                // Column header for entries
                Rectangle {
                    visible: resourceMonitor.messageLog.length > 0
                    width: parent.width
                    height: Theme.sp(24)
                    color: Theme.colorBg2
                    radius: Theme.radiusLg
                    // Square bottom corners
                    Rectangle {
                        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                        height: Theme.radiusLg; color: Theme.colorBg2
                    }
                    Rectangle {
                        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                        height: 1; color: Theme.colorBorderMid
                    }

                    Row {
                        anchors { fill: parent; leftMargin: Theme.sp(10); rightMargin: Theme.sp(10) }

                        Item {
                            width: Theme.sp(62); height: parent.height
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "TIME"
                                font.family: Theme.fontData; font.pixelSize: Theme.sp(9)
                                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
                            }
                        }

                        Item {
                            width: Theme.sp(52); height: parent.height
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "SEVERITY"
                                font.family: Theme.fontData; font.pixelSize: Theme.sp(9)
                                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "MESSAGE"
                            font.family: Theme.fontData; font.pixelSize: Theme.sp(9)
                            font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
                        }
                    }
                }

                // Empty state
                Rectangle {
                    visible: msgLogColumn.filteredLog.length === 0
                    width: parent.width
                    height: Theme.sp(40)
                    color: Theme.colorSurface
                    radius: Theme.radius
                    border.width: 1
                    border.color: Theme.colorBorderMid

                    Text {
                        anchors.centerIn: parent
                        text: "No messages logged"
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText3
                    }
                }

                // Log rows — newest first
                Repeater {
                    model: msgLogColumn.filteredLog

                    Rectangle {
                        required property var  modelData
                        required property int  index

                        width: parent.width
                        height: logMsg.implicitHeight + Theme.sp(12)
                        color: index % 2 === 0 ? Theme.colorSurface : Theme.colorBg
                        radius: 0
                        bottomLeftRadius:  index === msgLogColumn.filteredLog.length - 1 ? Theme.radius : 0
                        bottomRightRadius: index === msgLogColumn.filteredLog.length - 1 ? Theme.radius : 0

                        // Helpers: severity → colour and abbreviation
                        readonly property color  sevColor: {
                            var s = modelData.severity
                            if (s === "WARN")            return Theme.colorWarn
                            if (s === "ERROR" || s === "FATAL") return Theme.colorError
                            if (s === "INFO")            return Theme.colorText2
                            return Theme.colorText3   // DEBUG
                        }

                        // Timestamp
                        TextEdit {
                            id: logTs
                            anchors { left: parent.left; top: parent.top
                                      leftMargin: Theme.sp(10); topMargin: Theme.sp(6) }
                            width: Theme.sp(52)
                            text: modelData.timestamp
                            font.family: Theme.fontData
                            font.pixelSize: Theme.sp(10)
                            color: Theme.colorText3
                            readOnly: true
                            selectByMouse: true
                            selectionColor: Theme.colorAccentMid
                            selectedTextColor: Theme.colorText
                        }

                        // Severity badge
                        Rectangle {
                            id: sevBadge
                            anchors { left: logTs.right; top: parent.top
                                      leftMargin: Theme.sp(4); topMargin: Theme.sp(5) }
                            width: Theme.sp(44)
                            height: Theme.sp(16)
                            radius: Theme.sp(3)
                            color: Qt.rgba(parent.sevColor.r, parent.sevColor.g,
                                           parent.sevColor.b, 0.12)
                            border.width: 1
                            border.color: Qt.rgba(parent.sevColor.r, parent.sevColor.g,
                                                  parent.sevColor.b, 0.35)

                            Text {
                                anchors.centerIn: parent
                                text: modelData.severity
                                font.family: Theme.fontData
                                font.pixelSize: Theme.sp(9)
                                font.letterSpacing: Theme.trackingMicro
                                color: parent.parent.sevColor
                            }
                        }

                        // Message text
                        TextEdit {
                            id: logMsg
                            anchors {
                                left: sevBadge.right; right: parent.right; top: parent.top
                                leftMargin: Theme.sp(8); rightMargin: Theme.sp(10)
                                topMargin: Theme.sp(6)
                            }
                            text: modelData.message
                            wrapMode: TextEdit.WordWrap
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            font.weight: Font.Light
                            color: Theme.colorText2
                            readOnly: true
                            selectByMouse: true
                            selectionColor: Theme.colorAccentMid
                            selectedTextColor: Theme.colorText
                        }
                    }
                }
            }

            Item { height: Theme.sp(16) }
        }
    }
}
