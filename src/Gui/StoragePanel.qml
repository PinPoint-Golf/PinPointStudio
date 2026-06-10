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
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import PinPointStudio

Item {
    id: root

    // Disk info — refreshed when the panel becomes visible and after folder changes
    property var diskInfo: ({})

    function refreshDiskInfo() {
        diskInfo = appSettings.queryStorageInfo()
    }

    function formatBytes(n) {
        if (!n || n <= 0) return "—"
        if (n >= 1e12) return (n / 1e12).toFixed(1) + " TB"
        if (n >= 1e9)  return (n / 1e9).toFixed(1)  + " GB"
        if (n >= 1e6)  return (n / 1e6).toFixed(1)  + " MB"
        return (n / 1e3).toFixed(0) + " KB"
    }

    function formatMb(n) {
        if (n >= 1024) return (n / 1024).toFixed(1) + " GB"
        return n.toFixed(1) + " MB"
    }

    function formatGb(n) {
        if (n >= 1024) return (n / 1024).toFixed(1) + " TB"
        return n.toFixed(1) + " GB"
    }

    function recalcEstimate() {}

    // ── Estimated size helpers ────────────────────────────────────────────────

    readonly property real codecMultiplier: (({
        "h264": 1.0,
        "h265": 0.6
    })[appSettings.videoCodec]) || 1.0

    readonly property real qualityMultiplier: (({
        "low":      0.5,
        "medium":   1.0,
        "high":     2.0,
        "lossless": 4.0
    })[appSettings.videoQuality]) || 1.0

    readonly property real clipMb: 2 * 60 * codecMultiplier * qualityMultiplier
                                       * (appSettings.saveRawFrames ? 4 : 1)
    readonly property real sessionMb: clipMb * 50

    readonly property int remainingSessions: root.diskInfo.freeBytes > 0 && sessionMb > 0
        ? Math.floor(root.diskInfo.freeBytes / (sessionMb * 1024 * 1024))
        : 0

    Component.onCompleted: refreshDiskInfo()

    // ── Folder dialog ─────────────────────────────────────────────────────────

    FolderDialog {
        id: folderDialog
        title: qsTr("Select athlete library location")
        onAccepted: {
            appSettings.athleteLibraryPath = appSettings.urlToLocalFile(selectedFolder)
            root.refreshDiskInfo()
        }
    }

    // ── Inline component — reusable toggle pill ───────────────────────────────

    component TogglePill: Rectangle {
        id: pill
        property bool checked: false
        signal toggled(bool value)

        width:  Theme.sp(34)
        height: Theme.sp(18)
        radius: Theme.sp(9)
        color:  pill.checked ? Theme.colorAccent : Theme.colorBg3
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            width:  Theme.sp(12)
            height: Theme.sp(12)
            radius: Theme.sp(6)
            color:  "white"
            anchors.verticalCenter: parent.verticalCenter
            x: pill.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
            Behavior on x { NumberAnimation { duration: 120 } }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape:  Qt.PointingHandCursor
            onClicked:    pill.toggled(!pill.checked)
        }
    }

    // ── Option models ─────────────────────────────────────────────────────────

    readonly property var namingOptions: [
        { label: qsTr("Date · Athlete · Session type"), value: "date-name-type" },
        { label: qsTr("Date · Session type · Athlete"), value: "date-type-name" },
        { label: qsTr("Athlete · Date · Session type"), value: "name-date-type" },
        { label: qsTr("Date only"),                      value: "date-only"     }
    ]

    readonly property var resolutionOptions: [
        { label: qsTr("4K"),       value: "4k"     },
        { label: qsTr("1080p"),    value: "1080p"  },
        { label: qsTr("Native"),   value: "native" },
        { label: qsTr("½ native"), value: "half"   }
    ]

    readonly property var codecOptions: [
        { label: qsTr("H.264"), value: "h264" },
        { label: qsTr("H.265"), value: "h265" }
    ]

    readonly property var qualityOptions: [
        { label: qsTr("Low"),      value: "low"      },
        { label: qsTr("Medium"),   value: "medium"   },
        { label: qsTr("High"),     value: "high"     },
        { label: qsTr("Lossless"), value: "lossless" }
    ]

    readonly property var containerOptions: [
        { label: "MP4", value: "mp4" },
        { label: "MOV", value: "mov" },
        { label: "MKV", value: "mkv" }
    ]

    readonly property var imuFormatOptions: [
        { label: qsTr("JSON"),   value: "json"   },
        { label: qsTr("CSV"),    value: "csv"    },
        { label: qsTr("Binary"), value: "binary" }
    ]

    readonly property var codecDescriptions: ({
        "h264": qsTr("H.264 offers excellent compatibility and small file sizes. Recommended for most studio environments."),
        "h265": qsTr("H.265 reduces file size by ~40% vs H.264 at equivalent quality. Requires hardware decode for smooth playback on older machines.")
    })

    // ── Search scroll-to support ──────────────────────────────────────────────

    property string lastHighlightId: ""

    function findChild(parent, name) {
        for (var i = 0; i < parent.children.length; i++) {
            var child = parent.children[i]
            if (child.objectName === name) return child
            var found = findChild(child, name)
            if (found) return found
        }
        return null
    }

    function scrollToItem(itemId) {
        if (!itemId) return true
        var target = findChild(contentCol, itemId)
        if (!target) return false
        var mapped = target.mapToItem(contentCol, 0, 0)
        scrollView.contentItem.contentY = Math.max(0, Math.min(
            mapped.y - Theme.sp(24),
            scrollView.contentItem.contentHeight - scrollView.height
        ))
        target.searchHighlight = true
        lastHighlightId = itemId
        highlightTimer.restart()
        return true
    }

    Timer {
        id: highlightTimer
        interval: 1800
        onTriggered: {
            var target = findChild(contentCol, lastHighlightId)
            if (target) target.searchHighlight = false
        }
    }

    // ── Main scroll view ──────────────────────────────────────────────────────

    ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentCol.y + contentCol.implicitHeight + Theme.sp(28)

        ColumnLayout {
            id: contentCol
            x:       Theme.sp(32)
            y:       Theme.sp(28)
            width:   parent.width - Theme.sp(64)
            spacing: Theme.sp(20)

            // ── Page header ───────────────────────────────────────────────────

            Text {
                text:                qsTr("DATA")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color:               Theme.colorText3
            }

            Text {
                text:           qsTr("Storage")
                font.family:    Theme.fontDisplay
                font.italic:    Theme.fontDisplayItalic
                font.weight: Theme.fontDisplayWeight
                font.pixelSize: Theme.fontSzDisplay
                color:          Theme.colorText
            }

            Text {
                text:             qsTr("Athlete library location, video recording format, and sensor data export. Retention and archiving policies are configured separately.")
                font.family:      Theme.fontBody
                font.pixelSize:   Theme.fontSzBody2
                font.weight:      Theme.fontBodyWeight
                color:            Theme.colorText3
                wrapMode:         Text.WordWrap
                Layout.fillWidth: true
            }

            // ── Group 1 — Athlete library ─────────────────────────────────────

            Text {
                text:                qsTr("ATHLETE LIBRARY")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color:               Theme.colorText3
            }

            // Library location
            RowLayout {
                objectName: "setting_libraryPath"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Athlete library location")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Root directory for all athlete profiles and session archives")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight:   Theme.sp(28)
                        color:            Theme.colorBg2
                        border.width:     1
                        border.color:     Theme.colorBorderStrong
                        radius:           Theme.radius
                        clip:             true

                        Text {
                            anchors {
                                left: parent.left; leftMargin: Theme.sp(10)
                                right: parent.right; rightMargin: Theme.sp(10)
                                verticalCenter: parent.verticalCenter
                            }
                            text:           appSettings.athleteLibraryPath.length > 0
                                                ? appSettings.athleteLibraryPath
                                                : qsTr("No location selected")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          appSettings.athleteLibraryPath.length > 0
                                                ? Theme.colorText2 : Theme.colorText3
                            elide:          Text.ElideLeft
                        }
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignBottom
                    spacing: Theme.sp(4)
                    PpButton {
                        label:     qsTr("Change…")
                        onClicked: folderDialog.open()
                    }
                    PpButton {
                        label:     qsTr("Open")
                        enabled:   appSettings.athleteLibraryPath.length > 0
                        onClicked: Qt.openUrlExternally(appSettings.fileUrlFor(appSettings.athleteLibraryPath))
                    }
                }
            }

            // Disk usage bar
            Rectangle {
                Layout.fillWidth:  true
                Layout.leftMargin: Theme.sp(26)
                implicitHeight:   diskContent.implicitHeight + Theme.sp(24)
                color:            Theme.colorBg2
                border.width:     1
                border.color:     Theme.colorBorderMid
                radius:           Theme.radius
                visible:          root.diskInfo.totalBytes > 0

                ColumnLayout {
                    id: diskContent
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.sp(12) }
                    spacing: Theme.sp(6)

                    Text {
                        text:                qsTr("DISK USAGE — ") + (root.diskInfo.volumeName || "—")
                        font.family:         Theme.fontData
                        font.pixelSize:      Theme.fontSzMicro
                        font.letterSpacing:  Theme.trackingMicro
                        font.capitalization: Font.AllUppercase
                        color:               Theme.colorText3
                    }

                    // Sessions bar
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sp(10)

                        Text {
                            text:                  qsTr("Sessions")
                            font.family:           Theme.fontData
                            font.pixelSize:        Theme.fontSzMicro
                            color:                 Theme.colorText3
                            Layout.preferredWidth: Theme.sp(70)
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.sp(6)
                            radius: Theme.sp(3)
                            color:  Theme.colorBg3

                            Rectangle {
                                width:  root.diskInfo.totalBytes > 0
                                            ? parent.width * Math.min(1, root.diskInfo.sessionBytes / root.diskInfo.totalBytes)
                                            : 0
                                height: parent.height
                                radius: parent.radius
                                color:  Theme.colorAccent
                                Behavior on width { NumberAnimation { duration: Theme.durationNormal } }
                            }
                        }

                        Text {
                            text:                  formatBytes(root.diskInfo.sessionBytes)
                            font.family:           Theme.fontData
                            font.pixelSize:        Theme.fontSzMicro
                            color:                 Theme.colorText2
                            Layout.preferredWidth: Theme.sp(60)
                            horizontalAlignment:   Text.AlignRight
                        }
                    }

                    // Available bar
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sp(10)

                        Text {
                            text:                  qsTr("Available")
                            font.family:           Theme.fontData
                            font.pixelSize:        Theme.fontSzMicro
                            color:                 Theme.colorText3
                            Layout.preferredWidth: Theme.sp(70)
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.sp(6)
                            radius: Theme.sp(3)
                            color:  Theme.colorBg3

                            Rectangle {
                                width:  root.diskInfo.totalBytes > 0
                                            ? parent.width * Math.min(1, root.diskInfo.freeBytes / root.diskInfo.totalBytes)
                                            : 0
                                height: parent.height
                                radius: parent.radius
                                color:  root.diskInfo.freeBytes < 10 * 1024 * 1024 * 1024
                                            ? Theme.colorWarn : Theme.colorGood
                                Behavior on width  { NumberAnimation { duration: Theme.durationNormal } }
                                Behavior on color  { ColorAnimation  { duration: Theme.durationFast } }
                            }
                        }

                        Text {
                            text:                  formatBytes(root.diskInfo.freeBytes)
                            font.family:           Theme.fontData
                            font.pixelSize:        Theme.fontSzMicro
                            color:                 root.diskInfo.freeBytes < 10 * 1024 * 1024 * 1024
                                                       ? Theme.colorWarn : Theme.colorGood
                            Layout.preferredWidth: Theme.sp(60)
                            horizontalAlignment:   Text.AlignRight
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        }
                    }
                }
            }

            // Session folder naming row
            RowLayout {
                objectName: "setting_sessionNaming"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Session folder naming")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }

                    // Live preview of folder name pattern
                    Text {
                        text: {
                            var p = appSettings.sessionNamingPattern
                            if (p === "date-name-type") return "2026-05-22_Mark-Liversedge_Driver"
                            if (p === "date-type-name") return "2026-05-22_Driver_Mark-Liversedge"
                            if (p === "name-date-type") return "Mark-Liversedge_2026-05-22_Driver"
                            return "2026-05-22"
                        }
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorAccent
                    }
                }

                ComboBox {
                        id: namingCombo
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: Theme.sp(240)

                        model: root.namingOptions.map(function(e) { return e.label })

                        Component.onCompleted: {
                            var v = appSettings.sessionNamingPattern
                            for (var i = 0; i < root.namingOptions.length; i++) {
                                if (root.namingOptions[i].value === v) {
                                    currentIndex = i
                                    break
                                }
                            }
                        }

                        onActivated: (index) => {
                            appSettings.sessionNamingPattern = root.namingOptions[index].value
                        }

                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight:    Theme.fontBodyWeight

                        contentItem: Text {
                            leftPadding:       Theme.sp(10)
                            text:              namingCombo.displayText
                            font:              namingCombo.font
                            color:             Theme.colorText
                            verticalAlignment: Text.AlignVCenter
                            elide:             Text.ElideRight
                        }

                        background: Rectangle {
                            color:        Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorBorderStrong
                            radius:       Theme.radius
                        }

                        indicator: Text {
                            x: namingCombo.width - width - Theme.sp(10)
                            anchors.verticalCenter: parent.verticalCenter
                            text:           "⌄"
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            color:          Theme.colorText3
                        }

                        popup: Popup {
                            y: namingCombo.height
                            width: namingCombo.width
                            padding: 0

                            background: Rectangle {
                                color:        Theme.colorSurface
                                border.width: 1
                                border.color: Theme.colorBorderStrong
                                radius:       Theme.radius
                            }

                            contentItem: ListView {
                                implicitHeight: contentHeight
                                model: namingCombo.delegateModel
                                clip: true
                            }
                        }

                        delegate: ItemDelegate {
                            required property string modelData
                            required property int    index

                            width:       namingCombo.width
                            highlighted: namingCombo.highlightedIndex === index

                            contentItem: Text {
                                leftPadding:       Theme.sp(10)
                                text:              modelData
                                font.family:       Theme.fontBody
                                font.pixelSize:    Theme.fontSzBody2
                                font.weight:       Theme.fontBodyWeight
                                color:             Theme.colorText
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                color: parent.highlighted ? Theme.colorAccentLight : "transparent"
                            }
                        }
                    }
                }

            // Auto-save session row (moved verbatim from GeneralPanel)
            RowLayout {
                objectName: "setting_autoSave"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Auto-save session on completion")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Writes to archive immediately when session ends")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    id: autoSaveToggle
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  autoSaveToggle.checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.autoSaveSession

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: autoSaveToggle.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    appSettings.autoSaveSession = !appSettings.autoSaveSession
                    }
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 2 — Video recording ─────────────────────────────────────

            Text {
                text:                qsTr("VIDEO RECORDING")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color:               Theme.colorText3
            }

            // Recording resolution
            RowLayout {
                objectName: "setting_videoRes"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Recording resolution")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Applies to all cameras — must be within sensor ROI bounds")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Row {
                    spacing: Theme.sp(4)
                    Layout.alignment: Qt.AlignVCenter

                    Repeater {
                        model: root.resolutionOptions

                        delegate: Rectangle {
                            required property var modelData

                            readonly property bool isSelected: appSettings.videoResolutionMode === modelData.value

                            width:        resLbl.implicitWidth + Theme.sp(20)
                            height:       Theme.sp(24)
                            radius:       Theme.radius
                            color:        isSelected ? Theme.colorAccentLight : "transparent"
                            border.width: 1
                            border.color: isSelected ? Theme.colorAccent : Theme.colorBorderStrong
                            Behavior on color       { ColorAnimation { duration: Theme.durationFast } }
                            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                            Text {
                                id: resLbl
                                anchors.centerIn: parent
                                text:           modelData.label
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          isSelected ? Theme.colorAccent : Theme.colorText2
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape:  Qt.PointingHandCursor
                                onClicked: {
                                    appSettings.videoResolutionMode = modelData.value
                                    recalcEstimate()
                                }
                            }
                        }
                    }
                }
            }

            // Video codec
            RowLayout {
                objectName: "setting_videoCodec"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Video codec")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Encoding applied when saving swing clips to disk")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                    Text {
                        text:           root.codecDescriptions[appSettings.videoCodec] || ""
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText2
                        wrapMode:       Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Row {
                    spacing: Theme.sp(4)
                    Layout.alignment: Qt.AlignVCenter

                    Repeater {
                        model: root.codecOptions

                        delegate: Rectangle {
                            required property var modelData

                            readonly property bool isSelected: appSettings.videoCodec === modelData.value

                            width:        codecLbl.implicitWidth + Theme.sp(20)
                            height:       Theme.sp(24)
                            radius:       Theme.radius
                            color:        isSelected ? Theme.colorAccentLight : "transparent"
                            border.width: 1
                            border.color: isSelected ? Theme.colorAccent : Theme.colorBorderStrong
                            Behavior on color       { ColorAnimation { duration: Theme.durationFast } }
                            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                            Text {
                                id: codecLbl
                                anchors.centerIn: parent
                                text:           modelData.label
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          isSelected ? Theme.colorAccent : Theme.colorText2
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape:  Qt.PointingHandCursor
                                onClicked: {
                                    appSettings.videoCodec = modelData.value
                                    recalcEstimate()
                                }
                            }
                        }
                    }
                }
            }

            // Encoding quality (dimmed when codec is raw)
            RowLayout {
                objectName: "setting_videoQuality"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                opacity: appSettings.videoCodec === "raw" ? 0.4 : 1.0
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                enabled: appSettings.videoCodec !== "raw"
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Encoding quality")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Higher quality produces larger files")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Row {
                    spacing: Theme.sp(4)
                    Layout.alignment: Qt.AlignVCenter

                    Repeater {
                        model: root.qualityOptions

                        delegate: Rectangle {
                            required property var modelData

                            readonly property bool isSelected: appSettings.videoQuality === modelData.value

                            width:        qualLbl.implicitWidth + Theme.sp(20)
                            height:       Theme.sp(24)
                            radius:       Theme.radius
                            color:        isSelected ? Theme.colorAccentLight : "transparent"
                            border.width: 1
                            border.color: isSelected ? Theme.colorAccent : Theme.colorBorderStrong
                            Behavior on color       { ColorAnimation { duration: Theme.durationFast } }
                            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                            Text {
                                id: qualLbl
                                anchors.centerIn: parent
                                text:           modelData.label
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          isSelected ? Theme.colorAccent : Theme.colorText2
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape:  Qt.PointingHandCursor
                                onClicked: {
                                    appSettings.videoQuality = modelData.value
                                    recalcEstimate()
                                }
                            }
                        }
                    }
                }
            }

            // Save raw camera frames
            RowLayout {
                objectName: "setting_saveRaw"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Save raw camera frames")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Stores unprocessed Bayer data alongside encoded clips")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                TogglePill {
                    checked:  appSettings.saveRawFrames
                    onToggled: (v) => appSettings.saveRawFrames = v
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // Raw frames warning strip
            Rectangle {
                Layout.fillWidth: true
                implicitHeight:   rawWarnText.implicitHeight + Theme.sp(20)
                color:            Theme.colorWarnLight
                border.width:     1
                border.color:     Theme.colorWarn
                radius:           Theme.radius
                visible:          appSettings.saveRawFrames

                Text {
                    id: rawWarnText
                    anchors {
                        left: parent.left; right: parent.right
                        top:  parent.top
                        leftMargin: Theme.sp(12); rightMargin: Theme.sp(12); topMargin: Theme.sp(10)
                    }
                    text:           qsTr("Saving raw Bayer frames uses approximately 4× the storage of encoded video. Ensure the library volume has sufficient free space before recording long sessions.")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color:          Theme.colorWarn
                    wrapMode:       Text.WordWrap
                }
            }

            // Container format
            RowLayout {
                objectName: "setting_container"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Container format")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("File format for saved swing clips")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Row {
                    spacing: Theme.sp(4)
                    Layout.alignment: Qt.AlignVCenter

                    Repeater {
                        model: root.containerOptions

                        delegate: Rectangle {
                            required property var modelData

                            readonly property bool isSelected: appSettings.videoContainer === modelData.value

                            width:        contLbl.implicitWidth + Theme.sp(20)
                            height:       Theme.sp(24)
                            radius:       Theme.radius
                            color:        isSelected ? Theme.colorAccentLight : "transparent"
                            border.width: 1
                            border.color: isSelected ? Theme.colorAccent : Theme.colorBorderStrong
                            Behavior on color       { ColorAnimation { duration: Theme.durationFast } }
                            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                            Text {
                                id: contLbl
                                anchors.centerIn: parent
                                text:           modelData.label
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          isSelected ? Theme.colorAccent : Theme.colorText2
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape:  Qt.PointingHandCursor
                                onClicked: {
                                    appSettings.videoContainer = modelData.value
                                    recalcEstimate()
                                }
                            }
                        }
                    }
                }
            }

            // Estimated sizes
            RowLayout {
                id: estimateRow
                spacing: 0

                    // Column 1 — per-swing clip
                    ColumnLayout {
                        spacing: Theme.sp(4)
                        Layout.leftMargin: Theme.sp(26)

                        Text {
                            text:                qsTr("Per-swing clip")
                            font.family:         Theme.fontData
                            font.pixelSize:      Theme.fontSzMicro
                            font.letterSpacing:  Theme.trackingMicro
                            font.capitalization: Font.AllUppercase
                            color:               Theme.colorText3
                        }
                        Text {
                            text:           "~" + formatMb(root.clipMb)
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody
                            color:          Theme.colorText
                        }
                        Text {
                            text: {
                                var codec   = appSettings.videoCodec.toUpperCase()
                                var quality = appSettings.videoQuality
                                var res     = appSettings.videoResolutionMode
                                return qsTr("2 cameras · 3 s · ") + codec + " " + quality + " · " + res
                            }
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorText3
                        }
                    }

                    // Separator
                    Rectangle {
                        width:            1
                        Layout.fillHeight: true
                        color:            Theme.colorBorderMid
                        Layout.leftMargin:  Theme.sp(12)
                        Layout.rightMargin: Theme.sp(12)
                    }

                    // Column 2 — per session
                    ColumnLayout {
                        spacing: Theme.sp(4)

                        Text {
                            text:                qsTr("Per session (50 swings)")
                            font.family:         Theme.fontData
                            font.pixelSize:      Theme.fontSzMicro
                            font.letterSpacing:  Theme.trackingMicro
                            font.capitalization: Font.AllUppercase
                            color:               Theme.colorText3
                        }
                        Text {
                            text:           "~" + formatGb(root.sessionMb / 1024)
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody
                            color:          Theme.colorText
                        }
                    }

                    // Separator
                    Rectangle {
                        width:             1
                        Layout.fillHeight: true
                        color:             Theme.colorBorderMid
                        Layout.leftMargin:  Theme.sp(12)
                        Layout.rightMargin: Theme.sp(12)
                    }

                    // Column 3 — remaining capacity
                    ColumnLayout {
                        spacing: Theme.sp(4)

                        Text {
                            text:                qsTr("Remaining capacity")
                            font.family:         Theme.fontData
                            font.pixelSize:      Theme.fontSzMicro
                            font.letterSpacing:  Theme.trackingMicro
                            font.capitalization: Font.AllUppercase
                            color:               Theme.colorText3
                        }
                        Text {
                            text:  root.diskInfo.freeBytes > 0
                                       ? "~" + root.remainingSessions + qsTr(" sessions")
                                       : "—"
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody
                            color:          root.remainingSessions > 20 ? Theme.colorGood : Theme.colorWarn
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        }
                    }

                }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 3 — Sensor data ─────────────────────────────────────────

            Text {
                text:                qsTr("SENSOR DATA")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color:               Theme.colorText3
            }

            // Save pose keypoints
            RowLayout {
                objectName: "setting_savePose"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Save pose keypoints")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("MoveNet skeleton data stored alongside each swing clip as JSON")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                TogglePill {
                    checked:   appSettings.savePoseKeypoints
                    onToggled: (v) => appSettings.savePoseKeypoints = v
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // Save IMU streams
            RowLayout {
                objectName: "setting_saveImu"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Save IMU streams")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Full quaternion and accelerometer data for all enabled IMUs")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                TogglePill {
                    checked:   appSettings.saveImuStreams
                    onToggled: (v) => appSettings.saveImuStreams = v
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // IMU data format (dimmed when saveImuStreams is off)
            RowLayout {
                objectName: "setting_imuFormat"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                opacity: appSettings.saveImuStreams ? 1.0 : 0.4
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                enabled: appSettings.saveImuStreams
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("IMU data format")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("File format for saved IMU streams")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Row {
                    spacing: Theme.sp(4)
                    Layout.alignment: Qt.AlignVCenter

                    Repeater {
                        model: root.imuFormatOptions

                        delegate: Rectangle {
                            required property var modelData

                            readonly property bool isSelected: appSettings.imuDataFormat === modelData.value

                            width:        imuFmtLbl.implicitWidth + Theme.sp(20)
                            height:       Theme.sp(24)
                            radius:       Theme.radius
                            color:        isSelected ? Theme.colorAccentLight : "transparent"
                            border.width: 1
                            border.color: isSelected ? Theme.colorAccent : Theme.colorBorderStrong
                            Behavior on color       { ColorAnimation { duration: Theme.durationFast } }
                            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                            Text {
                                id: imuFmtLbl
                                anchors.centerIn: parent
                                text:           modelData.label
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          isSelected ? Theme.colorAccent : Theme.colorText2
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape:  Qt.PointingHandCursor
                                onClicked:    appSettings.imuDataFormat = modelData.value
                            }
                        }
                    }
                }
            }

            // Save launch monitor data
            RowLayout {
                objectName: "setting_saveLaunchMon"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Save launch monitor data")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Ball-flight data from connected launch monitor, stored as JSON")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                TogglePill {
                    checked:   appSettings.saveLaunchMonitorData
                    onToggled: (v) => appSettings.saveLaunchMonitorData = v
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }
}
