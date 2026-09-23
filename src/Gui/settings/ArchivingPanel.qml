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

// Settings → Archiving (panelIndex 9). swing_storage_impl.md, Phase 2 stage 6.
//
// Space as HOURS of capture left at the swing size this library actually records; moving whole
// sessions to an archive location and back (ArchiveController → SessionArchiver: a verified copy,
// the library keeps a stub that still lists, trends and grades); the library trash; and the
// startup housekeeping, every automatic part of which is OFF until set here.
//
// NO FILESYSTEM WORK ON COMPLETION — this panel is built at launch (a StackLayout child; see the
// warning in StoragePanel.qml). The controller scans on a worker when the panel is first shown.
//
// The archive location is typed, not picked: no native dialogs in this application.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPointStudio

Item {
    id: root

    readonly property bool hasController: typeof archiveController !== "undefined" && archiveController !== null

    onVisibleChanged: if (visible && hasController) archiveController.refresh()

    // ── Search-scroll contract (ScreenSettings → SettingsIndex) ───────────────

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

    function formatBytes(n) {
        if (!n || n <= 0) return "—"
        if (n >= 1e12) return (n / 1e12).toFixed(1) + " TB"
        if (n >= 1e9)  return (n / 1e9).toFixed(1)  + " GB"
        if (n >= 1e6)  return (n / 1e6).toFixed(0)  + " MB"
        return (n / 1e3).toFixed(0) + " KB"
    }

    function indexOfValue(options, v) {
        for (var i = 0; i < options.length; ++i) if (options[i].value === v) return i
        return 0
    }

    readonly property var ageOptions: [
        { label: qsTr("Never"),          value: 0 },
        { label: qsTr("After 30 days"),  value: 30 },
        { label: qsTr("After 90 days"),  value: 90 },
        { label: qsTr("After 180 days"), value: 180 },
        { label: qsTr("After a year"),   value: 365 }
    ]
    readonly property var floorOptions: [
        { label: qsTr("Never"),           value: 0 },
        { label: qsTr("Below 50 GB"),     value: 50 },
        { label: qsTr("Below 100 GB"),    value: 100 },
        { label: qsTr("Below 250 GB"),    value: 250 },
        { label: qsTr("Below 500 GB"),    value: 500 }
    ]
    readonly property var trashOptions: [
        { label: qsTr("Keep until emptied"), value: 0 },
        { label: qsTr("7 days"),             value: 7 },
        { label: qsTr("30 days"),            value: 30 },
        { label: qsTr("90 days"),            value: 90 }
    ]

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
            width: Theme.sp(12); height: Theme.sp(12); radius: Theme.sp(6); color: "white"
            anchors.verticalCenter: parent.verticalCenter
            x: pill.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
            Behavior on x { NumberAnimation { duration: 120 } }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: pill.toggled(!pill.checked) }
    }

    component SectionHead: Text {
        font.family:         Theme.fontBody
        font.pixelSize:      Theme.fontSzMicro
        font.letterSpacing:  Theme.trackingMicro
        font.capitalization: Font.AllUppercase
        color:               Theme.colorText3
    }

    component RowTitle: ColumnLayout {
        property string title
        property string subtitle
        Layout.fillWidth: true
        spacing: Theme.sp(3)
        Text { text: parent.title; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody; color: Theme.colorText }
        Text {
            text: parent.subtitle; visible: text.length > 0
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
    }

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

            SectionHead { text: qsTr("LIBRARY") }
            PpDisplayText { text: qsTr("Archiving") }
            Text {
                text: qsTr("Move whole sessions off the library to another drive and back. An archived session stays in your list with its scores and metrics; opening it brings the rest back. Every copy is checked byte for byte before anything in the library is removed.")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Theme.fontBodyWeight
                color: Theme.colorText3; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }

            // ── Space ─────────────────────────────────────────────────────────
            SectionHead { text: qsTr("SPACE") }
            RowLayout {
                objectName: "setting_archiveSpace"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                RowTitle {
                    title: !root.hasController || archiveController.hoursLeft < 0
                           ? qsTr("Capture time left: measuring…")
                           : archiveController.hoursLeft >= 2
                             ? qsTr("About %1 hours of capture left").arg(Math.floor(archiveController.hoursLeft))
                             : qsTr("About %1 minutes of capture left").arg(Math.floor(archiveController.hoursLeft * 60))
                    subtitle: !root.hasController ? ""
                              : qsTr("%1 free · a swing here costs %2 (the mean of your last %3) · at one swing a minute")
                                    .arg(root.formatBytes(archiveController.freeBytes))
                                    .arg(root.formatBytes(archiveController.swingBytes))
                                    .arg(archiveController.swingsMeasured)
                }
                PpButton {
                    label: qsTr("Refresh")
                    enabled: root.hasController && !archiveController.busy
                    onClicked: archiveController.refresh()
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Where archives go ─────────────────────────────────────────────
            SectionHead { text: qsTr("ARCHIVE LOCATION") }
            ColumnLayout {
                objectName: "setting_archiveLocation"
                Layout.fillWidth: true
                spacing: Theme.sp(6)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                RowTitle {
                    title: qsTr("Folder")
                    subtitle: qsTr("Type or paste the path of a folder on another drive. Each archived session is copied to <folder>/<athlete>/<session>, as ordinary files.")
                }
                PpTextField {
                    Layout.fillWidth: true
                    text: appSettings.archiveLocation
                    placeholderText: qsTr("e.g. /Volumes/Archive/PinPoint  or  E:\\PinPoint")
                    onEditingFinished: appSettings.archiveLocation = text.trim()
                }
            }
            RowLayout {
                objectName: "setting_archiveKeepRaw"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                RowTitle {
                    title: qsTr("Keep raw sensor frames in the archive")
                    subtitle: qsTr("Off discards them when a session is archived. They cannot be recreated.")
                }
                TogglePill {
                    checked: appSettings.archiveKeepRaw
                    onToggled: (v) => appSettings.archiveKeepRaw = v
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Automatic ─────────────────────────────────────────────────────
            SectionHead { text: qsTr("AUTOMATIC — CHECKED A MINUTE AFTER START-UP") }
            RowLayout {
                objectName: "setting_archiveAge"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                RowTitle { title: qsTr("Archive sessions older than"); subtitle: qsTr("Needs an archive location. Today's sessions are never touched.") }
                PpComboBox {
                    Layout.preferredWidth: Theme.sp(200)
                    model: root.ageOptions.map(function (o) { return o.label })
                    currentIndex: root.indexOfValue(root.ageOptions, appSettings.archiveAfterDays)
                    onActivated: (index) => appSettings.archiveAfterDays = root.ageOptions[index].value
                }
            }
            RowLayout {
                objectName: "setting_archiveFloor"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                RowTitle { title: qsTr("Archive the oldest sessions when free space falls"); subtitle: qsTr("Oldest first, until there is room again.") }
                PpComboBox {
                    Layout.preferredWidth: Theme.sp(200)
                    model: root.floorOptions.map(function (o) { return o.label })
                    currentIndex: root.indexOfValue(root.floorOptions, appSettings.archiveFloorGb)
                    onActivated: (index) => appSettings.archiveFloorGb = root.floorOptions[index].value
                }
            }
            RowLayout {
                objectName: "setting_trashRetention"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                RowTitle {
                    title: qsTr("Empty deleted sessions and swings after")
                    subtitle: !root.hasController ? ""
                              : qsTr("The library's own trash (used where the drive has none): %1 items, %2")
                                    .arg(archiveController.trashEntries).arg(root.formatBytes(archiveController.trashBytes))
                }
                PpComboBox {
                    Layout.preferredWidth: Theme.sp(200)
                    model: root.trashOptions.map(function (o) { return o.label })
                    currentIndex: root.indexOfValue(root.trashOptions, appSettings.trashRetentionDays)
                    onActivated: (index) => appSettings.trashRetentionDays = root.trashOptions[index].value
                }
                PpButton {
                    label: qsTr("Empty now")
                    enabled: root.hasController && !archiveController.busy && archiveController.trashEntries > 0
                    onClicked: archiveController.emptyTrash()
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Status ────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                visible: root.hasController && archiveController.status.length > 0
                spacing: Theme.sp(12)
                Text {
                    text: root.hasController ? archiveController.status : ""
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText2
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                PpButton {
                    label: qsTr("Stop")
                    visible: root.hasController && archiveController.busy
                    onClicked: archiveController.cancel()
                }
            }

            // ── Sessions ──────────────────────────────────────────────────────
            SectionHead { text: qsTr("SESSIONS") }
            Text {
                visible: root.hasController && archiveController.sessions.length === 0
                text: qsTr("No sessions found in the library.")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
            }
            Repeater {
                model: root.hasController ? archiveController.sessions : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.sp(16)
                    RowTitle {
                        title: modelData.name
                        subtitle: qsTr("%1 swings · %2 · %3").arg(modelData.swings)
                                  .arg(root.formatBytes(modelData.bytes))
                                  .arg(modelData.archived ? qsTr("archived — opening it restores it")
                                                          : qsTr("in the library"))
                    }
                    PpButton {
                        label: modelData.archived ? qsTr("Restore") : qsTr("Archive")
                        enabled: root.hasController && !archiveController.busy
                                 && (modelData.archived || appSettings.archiveLocation.length > 0)
                        onClicked: modelData.archived ? archiveController.restoreSession(modelData.dir)
                                                      : archiveController.archiveSession(modelData.dir)
                    }
                }
            }
        }
    }
}
