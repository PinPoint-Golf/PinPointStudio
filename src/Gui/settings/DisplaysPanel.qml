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

// Settings → Displays (panelIndex 2): a diagram of the connected monitors, which one the main window
// opens on and how it remembers its size, where and how the post-shot diagnostics panel is cast
// (display, mode, delay, dwell, mirroring), and the UI's frame-rate cap and hardware acceleration.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

SettingsPanelBase {
    id: root

    // Build a model of connected screens for use in ComboBoxes.
    // Qt.application.screens returns the list of QScreen objects available at runtime.
    // Friendly manufacturer names for the common EDID PNP vendor codes.
    readonly property var _vendorNames: ({
        "AUS": "ASUS", "APP": "Apple", "DEL": "Dell", "SAM": "Samsung", "LGD": "LG",
        "GSM": "LG", "BNQ": "BenQ", "ACR": "Acer", "HWP": "HP", "LEN": "Lenovo",
        "MSI": "MSI", "AOC": "AOC", "VSC": "ViewSonic", "PHL": "Philips", "EIZ": "EIZO"
    })

    readonly property var screenModel: {
        var list = []
        var screens = Qt.application.screens
        for (var i = 0; i < screens.length; i++) {
            var s = screens[i]
            // Human-friendly name: built-in panels (eDP/LVDS/DSI) → "Built-in display";
            // everything else → "<Vendor> <Model>" (e.g. "ASUS PA279CRV"), falling back
            // to the connector name. NOTE: on this setup screen index 0 is the EXTERNAL
            // monitor, so no "primary" label — the resolution + name are what identify it.
            var friendly
            if (/^(eDP|LVDS|DSI)/i.test(s.name || "")) {
                friendly = qsTr("Built-in display")
            } else if (s.model && s.model.length) {
                var v = root._vendorNames[s.manufacturer] || ""
                friendly = (v ? v + " " : "") + s.model
            } else {
                friendly = (s.name && s.name.length) ? s.name : qsTr("Display %1").arg(i + 1)
            }
            list.push({
                label: friendly + "  ·  " + s.width + "×" + s.height,
                value: "screen:" + i
            })
        }
        return list
    }

    // Helper: find the ComboBox index for a stored value string
    function mainDisplayIndexFor(stored) {
        if (stored === "primary") return 0
        if (stored === "cursor")  return 1
        for (var i = 0; i < root.screenModel.length; i++) {
            if (root.screenModel[i].value === stored) return 2 + i
        }
        return 0
    }

    function secondaryDisplayIndexFor(stored) {
        if (stored === "none") return 0
        for (var i = 0; i < root.screenModel.length; i++) {
            if (root.screenModel[i].value === stored) return 1 + i
        }
        return 0
    }

    // ── Page header ───────────────────────────────────────────────────

    SettingsSectionHead { text: Theme.caps(qsTr("Configuration")) }

    PpDisplayText {
        text: qsTr("Displays")
    }

    Text {
        text: qsTr("Configure which monitors Pinpoint uses and how post-shot content is presented.")
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText3
        wrapMode:       Text.WordWrap
        Layout.fillWidth: true
    }

    // ── Monitor diagram ───────────────────────────────────────────────
    Item {
        Layout.fillWidth: true
        implicitHeight: monRow.implicitHeight + legend.implicitHeight + Theme.sp(8)

        Row {
            id: monRow
            spacing: Theme.gap(12)
            anchors.top: parent.top
            anchors.left: parent.left

            Repeater {
                model: Qt.application.screens

                delegate: Column {
                    required property var modelData
                    required property int index

                    readonly property bool isMain:
                        appSettings.mainDisplayMode === "screen:" + index
                        || (appSettings.mainDisplayMode === "primary" && index === 0)
                        || (appSettings.mainDisplayMode === "cursor"  && index === 0)
                    readonly property bool isSecondary: appSettings.secondaryDisplayMode === "screen:" + index

                    spacing: Theme.gap(4)
                    anchors.bottom: parent.bottom

                    Rectangle {
                        width:  Theme.sp(80)
                        height: Theme.sp(50)
                        radius: Theme.radius
                        color:  isMain      ? Theme.colorAccentLight
                              : isSecondary ? Theme.colorGoodLight
                              : Theme.colorBg3
                        border.width: 1
                        border.color: isMain      ? Theme.colorAccent
                                    : isSecondary ? Theme.colorGood
                                    : Theme.colorBorderStrong
                        Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                        Rectangle {
                            visible: isMain || isSecondary
                            anchors.top:     parent.top
                            anchors.right:   parent.right
                            anchors.margins: Theme.gap(4)
                            implicitWidth:  badgeText.implicitWidth + Theme.sp(8)
                            implicitHeight: Theme.sp(14)
                            radius: 1
                            color:  isMain ? Theme.colorAccent : Theme.colorGood

                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text:           isMain ? qsTr("Pinpoint") : qsTr("Post-shot")
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.sp(7)
                                color:          Theme.colorSurface
                            }
                        }

                        Text {
                            anchors.left:    parent.left
                            anchors.bottom:  parent.bottom
                            anchors.margins: Theme.gap(4)
                            text:            (index + 1).toString()
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color: isMain || isSecondary ? Theme.colorAccent : Theme.colorText3
                        }
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width:  Theme.sp(12)
                        height: Theme.sp(5)
                        color:  Theme.colorBorderStrong
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width:  Theme.sp(22)
                        height: Theme.sp(2)
                        radius: 1
                        color:  Theme.colorBorderStrong
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text:           modelData.name + " · " + modelData.width + "×" + modelData.height
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.sp(8)
                        color:          Theme.colorText3
                    }
                }
            }
        }

        Row {
            id: legend
            anchors.top:        monRow.bottom
            anchors.topMargin:  Theme.gap(10)
            anchors.left:       parent.left
            spacing: Theme.gap(16)

            Repeater {
                model: [
                    { color: Theme.colorAccent,       label: qsTr("Pinpoint main window") },
                    { color: Theme.colorGood,         label: qsTr("Post-shot display")    },
                    { color: Theme.colorBorderStrong, label: qsTr("Available")            }
                ]
                delegate: Row {
                    required property var modelData
                    spacing: Theme.gap(5)
                    Rectangle {
                        width:  Theme.sp(8)
                        height: Theme.sp(8)
                        radius: 1
                        color:  modelData.color
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text:           modelData.label
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    // ── Group 1 — Main window ─────────────────────────────────────────

    SettingsSectionHead { text: Theme.caps(qsTr("Main window")) }

    // Launch on row
    SettingsRow {
        objectName: "setting_launchOn"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Launch on"); subtitle: qsTr("Which display opens the main Pinpoint window") }

            PpComboBox {
                id: launchOnCombo
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Theme.sp(220)

                readonly property var mainDisplayLabels: {
                    var fixed = [
                        qsTr("Primary monitor"),
                        qsTr("Monitor with cursor at launch")
                    ]
                    return fixed.concat(root.screenModel.map(function(s) { return s.label }))
                }

                model: mainDisplayLabels

                Component.onCompleted: currentIndex = root.mainDisplayIndexFor(appSettings.mainDisplayMode)

                onActivated: (index) => {
                    if (index === 0)      appSettings.mainDisplayMode = "primary"
                    else if (index === 1) appSettings.mainDisplayMode = "cursor"
                    else                  appSettings.mainDisplayMode = root.screenModel[index - 2].value
                }
            }
        }
    }

    // Remember window geometry row
    SettingsRow {
        objectName: "setting_rememberGeometry"
        opacity: appSettings.windowMaximized ? 0.4 : 1.0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Remember window size and position"); subtitle: qsTr("Restores last session geometry on next launch") }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: appSettings.rememberWindowGeometry
                onToggled: (v) => appSettings.rememberWindowGeometry = v
            }
        }
    }

    // Launch in full screen row
    SettingsRow {
        objectName: "setting_fullScreen"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Launch in full screen"); subtitle: qsTr("Overrides saved window size if enabled") }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: appSettings.windowMaximized
                onToggled: (v) => appSettings.windowMaximized = v
            }
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Group 2 — Post-shot display ───────────────────────────────────

    SettingsSectionHead { text: Theme.caps(qsTr("Post-shot display")) }

    // WHAT the cast shows is no longer a choice, so it is stated once here rather
    // than offered as a control with one working value. The rows below decide WHERE
    // it goes, HOW it is surfaced, and how it is read at distance.
    Text {
        objectName: "setting_postShotContentNote"
        Layout.fillWidth: true
        text: qsTr("The secondary display shows the session diagnostics panel — what keeps "
                   + "happening this session, and what the model thinks is causing it.")
        wrapMode: Text.WordWrap
        font.family:    Theme.fontData
        font.pixelSize: Theme.fontSzMicro
        color:          Theme.colorText3
    }

    // Secondary display row
    SettingsRow {
        objectName: "setting_secondaryDisplay"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Secondary display"); subtitle: qsTr("Golfer-facing screen the session diagnostics panel is cast to") }

            PpComboBox {
                id: secondaryDisplayCombo
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Theme.sp(220)

                readonly property var secondaryDisplayLabels: {
                    var fixed = [ qsTr("None") ]
                    return fixed.concat(root.screenModel.map(function(s) { return s.label }))
                }

                model: secondaryDisplayLabels

                Component.onCompleted: currentIndex = root.secondaryDisplayIndexFor(appSettings.secondaryDisplayMode)

                onActivated: (index) => {
                    if (index === 0) appSettings.secondaryDisplayMode = "none"
                    else             appSettings.secondaryDisplayMode = root.screenModel[index - 1].value
                }
            }
        }
    }

    // Post-shot display mode row — how the diagnostics panel is surfaced on the
    // target display (persistent panel / auto-closing window / full-screen kiosk).
    SettingsRow {
        objectName: "setting_postShotDisplayMode"
        opacity: appSettings.secondaryDisplayMode === "none" ? 0.4 : 1.0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Post-shot display mode"); subtitle: qsTr("A persistent window, one that pops after each swing and closes, or a full-screen kiosk") }

            PpChipGroup {
                options: [qsTr("Panel"), qsTr("Window"), qsTr("Kiosk")]
                selected: {
                    if (appSettings.postShotDisplayMode === "window") return qsTr("Window")
                    if (appSettings.postShotDisplayMode === "kiosk")  return qsTr("Kiosk")
                    return qsTr("Panel")
                }
                onSelectionChanged: (value) => {
                    if (value === qsTr("Window"))     appSettings.postShotDisplayMode = "window"
                    else if (value === qsTr("Kiosk")) appSettings.postShotDisplayMode = "kiosk"
                    else                              appSettings.postShotDisplayMode = "panel"
                }
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Display delay row (dimmed when no secondary display)
    SettingsRow {
        objectName: "setting_postShotDelay"
        opacity: appSettings.secondaryDisplayMode === "none" ? 0.4 : 1.0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Display delay"); subtitle: qsTr("Pause after a swing before the cast window appears") }

            PpChipGroup {
                options: [qsTr("None"), qsTr("0.5 s"), qsTr("1.0 s"), qsTr("2.0 s")]
                selected: {
                    var d = appSettings.postShotDelay
                    if (d < 0.1)  return qsTr("None")
                    if (d < 0.75) return qsTr("0.5 s")
                    if (d < 1.5)  return qsTr("1.0 s")
                    return qsTr("2.0 s")
                }
                onSelectionChanged: (value) => {
                    if (value === qsTr("None"))           appSettings.postShotDelay = 0.0
                    else if (value === qsTr("0.5 s"))     appSettings.postShotDelay = 0.5
                    else if (value === qsTr("1.0 s"))     appSettings.postShotDelay = 1.0
                    else                                  appSettings.postShotDelay = 2.0
                }
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Window dwell row — how long the post-shot WINDOW stays before auto-closing.
    // Inert (dimmed) outside "window" mode, mirroring the no-secondary dimming.
    SettingsRow {
        objectName: "setting_postShotDwell"
        opacity: (appSettings.secondaryDisplayMode === "none"
                  || appSettings.postShotDisplayMode !== "window") ? 0.4 : 1.0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Window dwell"); subtitle: qsTr("How long the cast window stays before closing") }

            PpChipGroup {
                options: [qsTr("5 s"), qsTr("8 s"), qsTr("12 s"), qsTr("20 s")]
                selected: {
                    var d = appSettings.postShotDwell
                    if (d < 6.5)  return qsTr("5 s")
                    if (d < 10)   return qsTr("8 s")
                    if (d < 16)   return qsTr("12 s")
                    return qsTr("20 s")
                }
                onSelectionChanged: (value) => {
                    if (value === qsTr("5 s"))       appSettings.postShotDwell = 5.0
                    else if (value === qsTr("8 s"))  appSettings.postShotDwell = 8.0
                    else if (value === qsTr("12 s")) appSettings.postShotDwell = 12.0
                    else                             appSettings.postShotDwell = 20.0
                }
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Mirror main window row (dimmed when no secondary display)
    SettingsRow {
        objectName: "setting_mirrorMain"
        opacity: appSettings.secondaryDisplayMode === "none" ? 0.4 : 1.0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Mirror main window"); subtitle: qsTr("Flips the cast horizontally, for a coach standing opposite the athlete") }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: appSettings.postShotMirror
                onToggled: (v) => appSettings.postShotMirror = v
            }
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Group 3 — Rendering ───────────────────────────────────────────

    SettingsSectionHead { text: Theme.caps(qsTr("Rendering")) }

    // Frame rate cap row
    SettingsRow {
        objectName: "setting_frameRateCap"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Frame rate cap"); subtitle: qsTr("UI rendering — independent of camera capture rate") }

            PpChipGroup {
                options: [qsTr("30 fps"), qsTr("60 fps"), qsTr("Match display")]
                selected: {
                    if (appSettings.uiFrameRateCap === "30") return qsTr("30 fps")
                    if (appSettings.uiFrameRateCap === "60") return qsTr("60 fps")
                    return qsTr("Match display")
                }
                onSelectionChanged: (value) => {
                    if (value === qsTr("30 fps"))      appSettings.uiFrameRateCap = "30"
                    else if (value === qsTr("60 fps")) appSettings.uiFrameRateCap = "60"
                    else                               appSettings.uiFrameRateCap = "display"
                }
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Hardware acceleration row
    SettingsRow {
        objectName: "setting_hwAccel"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Hardware acceleration"); subtitle: qsTr("Uses GPU for video decode and overlay rendering") }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: appSettings.hardwareAcceleration
                onToggled: (v) => appSettings.hardwareAcceleration = v
            }
        }
    }
}
