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
import PinPointStudio

SettingsPanelBase {
    id: root

    readonly property bool hasController: typeof archiveController !== "undefined" && archiveController !== null

    onVisibleChanged: if (visible && hasController) archiveController.refresh()

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

    PpPageHeader {
        Layout.fillWidth: true
        spacing:     root.contentSpacing
        eyebrow:     Theme.caps(qsTr("Library"))
        title:       qsTr("Archiving")
        description: qsTr("Move whole sessions off the library to another drive and back. An archived session stays in your list with its scores and metrics; opening it brings the rest back. Every copy is checked byte for byte before anything in the library is removed.")
    }

    // ── Space ─────────────────────────────────────────────────────────────
    PpMicro { kind: "body"; text: Theme.caps(qsTr("Space")) }
    SettingsRow {
        objectName: "setting_archiveSpace"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title: !root.hasController || archiveController.hoursLeft < 0
                       ? qsTr("Capture time left: measuring…")
                       : archiveController.hoursLeft >= 2
                         ? qsTr("About %1 hours of capture left").arg(Math.floor(archiveController.hoursLeft))
                         : qsTr("About %1 minutes of capture left").arg(Math.floor(archiveController.hoursLeft * 60))
                subtitle: !root.hasController ? ""
                          : qsTr("%1 free · a swing here costs %2 (the mean of your last %3) · at one swing a minute")
                                .arg(Theme.formatBytes(archiveController.freeBytes, "—"))
                                .arg(Theme.formatBytes(archiveController.swingBytes, "—"))
                                .arg(archiveController.swingsMeasured)
            }
            PpButton {
                label: qsTr("Refresh")
                enabled: root.hasController && !archiveController.busy
                onClicked: archiveController.refresh()
            }
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Where archives go ─────────────────────────────────────────────────
    PpMicro { kind: "body"; text: Theme.caps(qsTr("Archive location")) }
    SettingsRow {
        objectName: "setting_archiveLocation"
        SettingsRowTitle {
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
    SettingsRow {
        objectName: "setting_archiveKeepRaw"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title: qsTr("Keep raw sensor frames in the archive")
                subtitle: qsTr("Off discards them when a session is archived. They cannot be recreated.")
            }
            PpSwitch {
                checked: appSettings.archiveKeepRaw
                onToggled: (v) => appSettings.archiveKeepRaw = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Automatic ─────────────────────────────────────────────────────────
    PpMicro { kind: "body"; text: Theme.caps(qsTr("Automatic — checked a minute after start-up")) }
    SettingsRow {
        objectName: "setting_archiveAge"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle { title: qsTr("Archive sessions older than"); subtitle: qsTr("Needs an archive location. Today's sessions are never touched.") }
            PpComboBox {
                Layout.preferredWidth: Theme.sp(200)
                model: root.ageOptions.map(function (o) { return o.label })
                currentIndex: root.indexOfValue(root.ageOptions, appSettings.archiveAfterDays)
                onActivated: (index) => appSettings.archiveAfterDays = root.ageOptions[index].value
            }
        }
    }
    SettingsRow {
        objectName: "setting_archiveFloor"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle { title: qsTr("Archive the oldest sessions when free space falls"); subtitle: qsTr("Oldest first, until there is room again.") }
            PpComboBox {
                Layout.preferredWidth: Theme.sp(200)
                model: root.floorOptions.map(function (o) { return o.label })
                currentIndex: root.indexOfValue(root.floorOptions, appSettings.archiveFloorGb)
                onActivated: (index) => appSettings.archiveFloorGb = root.floorOptions[index].value
            }
        }
    }
    SettingsRow {
        objectName: "setting_trashRetention"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title: qsTr("Empty deleted sessions and swings after")
                subtitle: !root.hasController ? ""
                          : qsTr("The library's own trash (used where the drive has none): %1 items, %2")
                                .arg(archiveController.trashEntries).arg(Theme.formatBytes(archiveController.trashBytes, "—"))
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
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Status ────────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        visible: root.hasController && archiveController.status.length > 0
        spacing: Theme.gap(12)
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

    // ── Sessions ──────────────────────────────────────────────────────────
    PpMicro { kind: "body"; text: Theme.caps(qsTr("Sessions")) }
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
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title: modelData.name
                subtitle: qsTr("%1 · %2 · %3")
                          .arg(Theme.plural(modelData.swings, qsTr("%1 swing"), qsTr("%1 swings")))
                          .arg(Theme.formatBytes(modelData.bytes, "—"))
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
