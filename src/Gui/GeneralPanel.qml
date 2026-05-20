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
import PinPoint

Item {
    id: root

    readonly property var languageModel: [
        { label: "English (UK)", tag: "en_GB" },
        { label: "English (US)", tag: "en_US" },
        { label: "Français",     tag: "fr_FR" },
        { label: "Deutsch",      tag: "de_DE" },
        { label: "日本語",        tag: "ja_JP" },
        { label: "한국어",        tag: "ko_KR" },
        { label: "中文（简体）",  tag: "zh_CN" }
    ]

    FolderDialog {
        id: folderDialog
        title: "Select athlete library location"
        onAccepted: appSettings.athleteLibraryPath = selectedFolder.toString().replace("file://", "")
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentCol.y + contentCol.implicitHeight + Theme.sp(28)

        ColumnLayout {
            id: contentCol
            x: Theme.sp(32)
            y: Theme.sp(28)
            width: parent.width - Theme.sp(64)
            spacing: Theme.sp(20)

            // ── Page header ───────────────────────────────────────────────────

            Text {
                text: "CONFIGURATION"
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            Text {
                text: "General"
                font.family:  Theme.fontDisplay
                font.italic:  Theme.fontDisplayItalic
                font.pixelSize: Theme.fontSzDisplay
                color: Theme.colorText
            }

            Text {
                text: "Language, athlete data, measurement units, and application behaviour."
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Font.Light
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
                Layout.fillWidth: true
            }

            // ── Group 1 — Localisation ────────────────────────────────────────

            Text {
                text: "LOCALISATION"
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Language row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           "Language"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           "Restart required to apply"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                ComboBox {
                    id: languageCombo
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Theme.sp(160)

                    model: root.languageModel.map(function(e) { return e.label })

                    Component.onCompleted: {
                        var tag = appSettings.language
                        for (var i = 0; i < root.languageModel.length; i++) {
                            if (root.languageModel[i].tag === tag) {
                                currentIndex = i
                                break
                            }
                        }
                    }

                    onActivated: (index) => {
                        appSettings.language = root.languageModel[index].tag
                    }

                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Font.Light

                    contentItem: Text {
                        leftPadding: Theme.sp(10)
                        text:           languageCombo.displayText
                        font:           languageCombo.font
                        color:          Theme.colorText
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        color:        Theme.colorSurface
                        border.width: 1
                        border.color: Theme.colorBorderStrong
                        radius:       Theme.radius
                    }

                    indicator: Text {
                        x: languageCombo.width - width - Theme.sp(10)
                        anchors.verticalCenter: parent.verticalCenter
                        text:           "⌄"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText3
                    }

                    popup: Popup {
                        y: languageCombo.height
                        width: languageCombo.width
                        padding: 0

                        background: Rectangle {
                            color:        Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorBorderStrong
                            radius:       Theme.radius
                        }

                        contentItem: ListView {
                            implicitHeight: contentHeight
                            model: languageCombo.delegateModel
                            clip: true
                        }
                    }

                    delegate: ItemDelegate {
                        required property string modelData
                        required property int    index

                        width: languageCombo.width
                        highlighted: languageCombo.highlightedIndex === index

                        contentItem: Text {
                            leftPadding: Theme.sp(10)
                            text:           modelData
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            font.weight:    Font.Light
                            color:          Theme.colorText
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: parent.highlighted ? Theme.colorAccentLight : "transparent"
                        }
                    }
                }
            }

            // Units row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           "Units"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           "Speed and distance displayed throughout"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                PpUnitToggle {
                    units:    ["mph", "km/h"]
                    selected: appSettings.units === "mph" ? "mph" : "km/h"
                    onSelectionChanged: (unit) => appSettings.units = (unit === "mph" ? "mph" : "kmh")
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 2 — Athlete data ────────────────────────────────────────

            Text {
                text: "ATHLETE DATA"
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Athlete library location — stacked layout
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(8)

                ColumnLayout {
                    spacing: Theme.sp(3)

                    Text {
                        text:           "Athlete library location"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           "Root directory for all athlete profiles and session archives"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(6)

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Theme.sp(28)
                        color:        Theme.colorBg2
                        border.width: 1
                        border.color: Theme.colorBorderStrong
                        radius:       Theme.radius
                        clip:         true

                        Text {
                            anchors {
                                left: parent.left; leftMargin: Theme.sp(10)
                                right: parent.right; rightMargin: Theme.sp(10)
                                verticalCenter: parent.verticalCenter
                            }
                            text:           appSettings.athleteLibraryPath.length > 0
                                                ? appSettings.athleteLibraryPath
                                                : "No location selected"
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          appSettings.athleteLibraryPath.length > 0
                                                ? Theme.colorText2
                                                : Theme.colorText3
                            elide:          Text.ElideLeft
                        }
                    }

                    PpButton {
                        label:     "Change…"
                        onClicked: folderDialog.open()
                    }
                }
            }

            // Auto-save session row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           "Auto-save session on completion"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           "Writes to archive immediately when session ends"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  autoSaveToggle.checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.autoSaveSession
                    id: autoSaveToggle

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

            // ── Group 3 — Session behaviour ───────────────────────────────────

            Text {
                text: "SESSION BEHAVIOUR"
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Auto-detect swing start row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           "Auto-detect swing start"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           "Begins capture when motion exceeds threshold"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    id: autoDetectToggle
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.autoDetectSwing

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: autoDetectToggle.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    appSettings.autoDetectSwing = !appSettings.autoDetectSwing
                    }
                }
            }

            // Swing detection sensitivity row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                opacity: appSettings.autoDetectSwing ? 1.0 : 0.4
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           "Swing detection sensitivity"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           "Lower values trigger on slower movements"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                PpChipGroup {
                    options:  ["Low", "Medium", "High"]
                    selected: appSettings.swingDetectionSensitivity
                    onSelectionChanged: (value) => appSettings.swingDetectionSensitivity = value
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // AI coaching on session end row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           "AI coaching on session end"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           "Automatically generate a Claude observation"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    id: aiCoachingToggle
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.aiCoachingOnSessionEnd

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: aiCoachingToggle.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    appSettings.aiCoachingOnSessionEnd = !appSettings.aiCoachingOnSessionEnd
                    }
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 4 — Application ─────────────────────────────────────────

            Text {
                text: "APPLICATION"
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Check for updates row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           "Check for updates automatically"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           "Checks on launch — never downloads without confirmation"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    id: updatesToggle
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.checkForUpdates

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: updatesToggle.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    appSettings.checkForUpdates = !appSettings.checkForUpdates
                    }
                }
            }

            // Send anonymous diagnostics row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           "Send anonymous diagnostics"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           "Crash reports and performance data only — no session content"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    id: diagnosticsToggle
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.sendDiagnostics

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: diagnosticsToggle.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    appSettings.sendDiagnostics = !appSettings.sendDiagnostics
                    }
                }
            }

            // Version row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)

                Text {
                    text:           "Version"
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    color:          Theme.colorText
                    Layout.fillWidth: true
                }

                Row {
                    spacing: Theme.sp(10)
                    Layout.alignment: Qt.AlignVCenter

                    Rectangle {
                        implicitWidth:  verText.implicitWidth + Theme.sp(20)
                        implicitHeight: Theme.sp(26)
                        color:          "transparent"
                        border.width:   1
                        border.color:   Theme.colorBorderStrong
                        radius:         Theme.radius

                        Text {
                            id: verText
                            anchors.centerIn: parent
                            text:           appSettings.appVersion
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText2
                        }
                    }

                    Rectangle {
                        implicitWidth:  badgeText.implicitWidth + Theme.sp(16)
                        implicitHeight: Theme.sp(26)
                        color:          Theme.colorGoodLight
                        border.width:   1
                        border.color:   Theme.colorGood
                        radius:         Theme.radius

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text:           "✓  Up to date"
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorGood
                        }
                    }
                }
            }
        }
    }
}
