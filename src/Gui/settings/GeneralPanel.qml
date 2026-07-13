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

    readonly property var languageModel: [
        { label: qsTr("English (UK)"), tag: "en_GB" },
        { label: qsTr("English (US)"), tag: "en_US" },
        { label: qsTr("Français"),     tag: "fr_FR" },
        { label: qsTr("Deutsch"),      tag: "de_DE" },
        { label: qsTr("日本語"),        tag: "ja_JP" },
        { label: qsTr("한국어"),        tag: "ko_KR" },
        { label: qsTr("中文（简体）"),  tag: "zh_CN" }
    ]

    // True while the Settings screen is actually shown (set by ScreenSettings from
    // its own StackLayout visibility). This panel is instantiated eagerly with the
    // screen, so it drives the lazy motion-capture hardware probe on first appear
    // rather than at app launch — see onHostVisibleChanged.
    property bool hostVisible: false
    onHostVisibleChanged: if (hostVisible) motionCaptureProbe.refresh()

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

    // ── Cloud Fallback reusable rows ──────────────────────────────────────────

    // A force-cloud toggle row. `locked` shows ON and is non-interactive (used by
    // the AI Coach when no local GPU exists); `canToggle` gates interaction on the
    // provider API key being configured.
    component CloudFallbackRow: RowLayout {
        id: cfr
        property string title:     ""
        property string subtitle:  ""
        property bool   checked:   false
        property bool   locked:    false
        property bool   canToggle: true
        signal toggled()

        Layout.fillWidth: true
        spacing: Theme.sp(16)
        property bool searchHighlight: false
        Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: cfr.searchHighlight ? 1.0 : 0.0; z: -1 }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(3)

            Text {
                text:           cfr.title
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                color:          Theme.colorText
            }
            Text {
                text:             cfr.subtitle
                visible:          cfr.subtitle.length > 0
                font.family:      Theme.fontData
                font.pixelSize:   Theme.fontSzMicro
                color:            Theme.colorText3
                wrapMode:         Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Rectangle {
            id: cfrSwitch
            Layout.alignment: Qt.AlignVCenter
            width:  Theme.sp(34)
            height: Theme.sp(18)
            radius: Theme.sp(9)
            readonly property bool shownOn: cfr.locked || cfr.checked
            color:   shownOn ? Theme.colorAccent : Theme.colorBg3
            opacity: (cfr.canToggle && !cfr.locked) ? 1.0 : 0.45
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            Rectangle {
                width:  Theme.sp(12)
                height: Theme.sp(12)
                radius: Theme.sp(6)
                color:  "white"
                anchors.verticalCenter: parent.verticalCenter
                x: cfrSwitch.shownOn ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                Behavior on x { NumberAnimation { duration: 120 } }
            }

            MouseArea {
                anchors.fill: parent
                enabled:      cfr.canToggle && !cfr.locked
                cursorShape:  enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked:    cfr.toggled()
            }
        }
    }

    // A masked API-key field (with a Show/Hide reveal) bound to a SecretsManager
    // key via the `secrets` bridge. Commits on focus loss / Enter (PpTextField).
    component CloudKeyField: RowLayout {
        id: ckf
        property string title:       ""
        property string secretName:  ""
        property string placeholder: ""

        Layout.fillWidth: true
        spacing: Theme.sp(16)
        property bool searchHighlight: false
        Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: ckf.searchHighlight ? 1.0 : 0.0; z: -1 }

        Text {
            text:             ckf.title
            font.family:      Theme.fontBody
            font.pixelSize:   Theme.fontSzBody
            color:            Theme.colorText
            Layout.fillWidth: true
        }

        PpTextField {
            id: keyField
            Layout.alignment:      Qt.AlignVCenter
            Layout.preferredWidth: Theme.sp(220)
            placeholderText:       ckf.placeholder
            echoMode:              revealBtn.revealed ? TextInput.Normal : TextInput.Password
            // read() has no change-notify, so this evaluates once on load — typing
            // is never reset. Commit persists via secrets.write() on editingFinished.
            text:                  secrets.read(ckf.secretName)
            onEditingFinished:     secrets.write(ckf.secretName, text)
        }

        Rectangle {
            id: revealBtn
            property bool revealed: false
            Layout.alignment: Qt.AlignVCenter
            implicitWidth:  revealLabel.implicitWidth + Theme.sp(20)
            implicitHeight: Theme.sp(34)
            radius:         Theme.radius
            border.width:   1
            border.color:   revealMa.containsMouse ? Theme.colorAccentMid : Theme.colorBorderStrong
            color:          revealMa.containsMouse ? Theme.colorBg2
                                                   : Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

            Text {
                id: revealLabel
                anchors.centerIn: parent
                text:           revealBtn.revealed ? qsTr("Hide") : qsTr("Show")
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color:          Theme.colorText2
            }

            PpPressable {
                id: revealMa
                onClicked:    revealBtn.revealed = !revealBtn.revealed
            }
        }
    }

    ScrollView {
        id: scrollView
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
                text: qsTr("CONFIGURATION")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            PpDisplayText {
                text: qsTr("General")
            }

            Text {
                text: qsTr("Language, measurement units, and application behaviour.")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Theme.fontBodyWeight
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
                Layout.fillWidth: true
            }

            // ── Group 1 — Localisation ────────────────────────────────────────

            Text {
                text: qsTr("LOCALISATION")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Language row
            RowLayout {
                objectName: "setting_language"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Language")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Restart required to apply")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                PpComboBox {
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
                }
            }

            // Units row
            RowLayout {
                objectName: "setting_units"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Units")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Speed and distance displayed throughout")
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

            // ── Group 2 — Session behaviour ───────────────────────────────────

            Text {
                text: qsTr("SESSION BEHAVIOUR")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Auto-detect swing start row
            RowLayout {
                objectName: "setting_autoDetect"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Auto-detect swing start")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Begins capture when motion exceeds threshold")
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
                objectName: "setting_swingSensitivity"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                opacity: appSettings.autoDetectSwing ? 1.0 : 0.4
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Swing detection sensitivity")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Lower values trigger on slower movements")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                PpChipGroup {
                    options:  [qsTr("Low"), qsTr("Medium"), qsTr("High")]
                    selected: appSettings.swingDetectionSensitivity
                    onSelectionChanged: (value) => appSettings.swingDetectionSensitivity = value
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // AI coaching on session end row
            RowLayout {
                objectName: "setting_aiCoaching"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("AI coaching on session end")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Automatically generate a Claude observation")
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

            // ── Group 3 — Motion capture ──────────────────────────────────────

            Text {
                text: qsTr("MOTION CAPTURE")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Motion capture quality row + on-demand High-tier model download.
            ColumnLayout {
                id: mcSection
                Layout.fillWidth: true
                spacing: Theme.sp(10)

                // True while we're asking the user to fetch the High-tier model (or
                // the fetch is in flight): "High" is committed only once the model is
                // actually present, so quality==High ⟺ the large model is downloaded.
                property bool promptHighDownload: false

                // Commit High the moment its model finishes downloading, then clear
                // the prompt. Keyed off highModelChanged (present/downloading/error).
                Connections {
                    target: motionCaptureProbe
                    function onHighModelChanged() {
                        if (mcSection.promptHighDownload && motionCaptureProbe.highModelPresent) {
                            appSettings.motionCaptureQuality = qsTr("High")
                            mcSection.promptHighDownload = false
                        }
                    }
                }

                RowLayout {
                    objectName: "setting_motionCapture"
                    Layout.fillWidth: true
                    spacing: Theme.sp(16)
                    property bool searchHighlight: false
                    Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                    // The (lazy) hardware probe is kicked from root.onHostVisibleChanged
                    // when the Settings screen first appears — not here, because this
                    // panel is constructed eagerly at app launch.

                    // Graceful fallback: a "High" persisted on a machine that turns out
                    // not to support it drops to "Medium" once the probe completes. Done
                    // in a handler (not a binding) to avoid a binding loop on the value.
                    Connections {
                        target: motionCaptureProbe
                        function onStateChanged() {
                            if (motionCaptureProbe.ready
                                    && !motionCaptureProbe.highTierSupported
                                    && appSettings.motionCaptureQuality === qsTr("High"))
                                appSettings.motionCaptureQuality = qsTr("Medium")
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sp(3)

                        Text {
                            text:           qsTr("Motion capture quality")
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            color:          Theme.colorText
                        }
                        Text {
                            text:             qsTr("Higher settings track your swing in more detail but take longer to analyse")
                            font.family:      Theme.fontData
                            font.pixelSize:   Theme.fontSzMicro
                            color:            Theme.colorText3
                            wrapMode:         Text.WordWrap
                            Layout.fillWidth: true
                        }
                        // Status line, in precedence order: measuring → (High selected on
                        // an unsupported machine) block reason → measured estimate → not
                        // yet measured. secondsPerSwing[tier] is -1 until measured, which
                        // falls through to the last case.
                        Text {
                            Layout.fillWidth: true
                            readonly property int selectedSeconds: {
                                var v = motionCaptureProbe.secondsPerSwing[appSettings.motionCaptureQuality];
                                return (v === undefined || v === null) ? -1 : v;
                            }
                            text: motionCaptureProbe.probing
                                      ? qsTr("Measuring your computer…")
                                  : (appSettings.motionCaptureQuality === qsTr("High")
                                     && !motionCaptureProbe.highTierSupported)
                                      ? motionCaptureProbe.highTierBlockReason
                                  : selectedSeconds > 0
                                      ? qsTr("About %1 seconds to analyse each swing").arg(selectedSeconds)
                                      : qsTr("Measured after your first swing")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorText3
                            wrapMode:       Text.WordWrap
                        }
                    }

                    PpChipGroup {
                        options:  [qsTr("Low"), qsTr("Medium"), qsTr("High")]
                        selected: appSettings.motionCaptureQuality
                        // Grey the High chip until the probe confirms the machine can run it.
                        disabledOptions: motionCaptureProbe.highTierSupported ? [] : [qsTr("High")]
                        // Selecting High needs the (unshipped) large model: prompt to
                        // download it first and commit High only once it lands. Every
                        // other tier commits immediately.
                        onSelectionChanged: (value) => {
                            if (value === qsTr("High") && !motionCaptureProbe.highModelPresent) {
                                motionCaptureProbe.clearHighModelDownloadError()
                                mcSection.promptHighDownload = true
                            } else {
                                appSettings.motionCaptureQuality = value
                            }
                        }
                        onBlockedClicked: (value) => { /* block reason is shown in the status line */ }
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                // Inline High-tier model prompt / progress / error strip. Shown only
                // when the user has just picked High without the model, a download is
                // running, or a download failed.
                Rectangle {
                    id: dlPanel
                    Layout.fillWidth: true
                    visible: mcSection.promptHighDownload
                             || motionCaptureProbe.highModelDownloading
                             || motionCaptureProbe.highModelDownloadError !== ""
                    implicitHeight: dlCol.implicitHeight + Theme.sp(20)
                    radius: Theme.radius
                    color:  Theme.colorBg2
                    border.width: 1
                    border.color: Theme.colorBorder

                    readonly property real gbSize: Math.round(motionCaptureProbe.highModelSizeBytes / 1e9 * 10) / 10
                    readonly property int  pct:    Math.max(0, Math.round(motionCaptureProbe.highModelDownloadProgress * 100))

                    ColumnLayout {
                        id: dlCol
                        anchors.fill: parent
                        anchors.margins: Theme.sp(10)
                        spacing: Theme.sp(8)

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color: motionCaptureProbe.highModelDownloadError !== "" ? Theme.colorWarn : Theme.colorText2
                            text: motionCaptureProbe.highModelDownloadError !== ""
                                      ? qsTr("Download failed: %1").arg(motionCaptureProbe.highModelDownloadError)
                                  : motionCaptureProbe.highModelDownloading
                                      ? (motionCaptureProbe.highModelDownloadProgress >= 0
                                             ? qsTr("Downloading high-quality model… %1%").arg(dlPanel.pct)
                                             : qsTr("Downloading high-quality model…"))
                                      : qsTr("High quality uses a larger motion model — a one-time download of about %1 GB.").arg(dlPanel.gbSize)
                        }

                        // Determinate progress bar (only while downloading with a known size).
                        Rectangle {
                            Layout.fillWidth: true
                            visible: motionCaptureProbe.highModelDownloading
                                     && motionCaptureProbe.highModelDownloadProgress >= 0
                            height: Theme.sp(4)
                            radius: height / 2
                            color:  Theme.colorBorder
                            Rectangle {
                                height: parent.height
                                radius: parent.radius
                                width:  parent.width * Math.max(0, Math.min(1, motionCaptureProbe.highModelDownloadProgress))
                                color:  Theme.colorAccent
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.sp(8)
                            Item { Layout.fillWidth: true }

                            PpButton {
                                visible: motionCaptureProbe.highModelDownloading
                                label:   qsTr("Cancel")
                                onClicked: {
                                    motionCaptureProbe.cancelHighModelDownload()
                                    mcSection.promptHighDownload = false
                                }
                            }
                            PpButton {
                                visible: !motionCaptureProbe.highModelDownloading
                                         && motionCaptureProbe.highModelDownloadError === ""
                                label:   qsTr("Not now")
                                onClicked: mcSection.promptHighDownload = false
                            }
                            PpButton {
                                visible: !motionCaptureProbe.highModelDownloading
                                         && motionCaptureProbe.highModelDownloadError === ""
                                label:   qsTr("Download")
                                primary: true
                                onClicked: motionCaptureProbe.downloadHighModel()
                            }
                            PpButton {
                                visible: motionCaptureProbe.highModelDownloadError !== ""
                                         && !motionCaptureProbe.highModelDownloading
                                label:   qsTr("Dismiss")
                                onClicked: {
                                    motionCaptureProbe.clearHighModelDownloadError()
                                    mcSection.promptHighDownload = false
                                }
                            }
                            PpButton {
                                visible: motionCaptureProbe.highModelDownloadError !== ""
                                         && !motionCaptureProbe.highModelDownloading
                                label:   qsTr("Try again")
                                primary: true
                                onClicked: motionCaptureProbe.downloadHighModel()
                            }
                        }
                    }
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 4 — Application ────────────────────────────────────────

            Text {
                text: qsTr("APPLICATION")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Check for updates row
            RowLayout {
                objectName: "setting_checkUpdates"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Check for updates automatically")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Checks on launch — never downloads without confirmation")
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
                objectName: "setting_diagnostics"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("Send anonymous diagnostics")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Crash reports and performance data only — no session content")
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
                objectName: "setting_version"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                Text {
                    text:           qsTr("Version")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    color:          Theme.colorText
                    Layout.fillWidth: true
                }

                Row {
                    id: verRow
                    spacing: Theme.sp(10)
                    Layout.alignment: Qt.AlignVCenter

                    // Live updater state (Linux AppImage). Off-Linux / dev builds
                    // report "unsupported"/"devbuild" and the action is hidden.
                    readonly property string uState: updateController.state

                    // Status-badge palette per state {text, fg, bg}. Text for the
                    // downloading state is bound separately so the % updates live.
                    readonly property var pal: {
                        switch (uState) {
                        case "checking":    return { text: qsTr("Checking…"),         fg: Theme.colorText2, bg: "transparent" }
                        case "available":   return { text: qsTr("Update available"),  fg: Theme.colorAccent, bg: Theme.colorAccentLight }
                        case "downloading": return { text: "",                         fg: Theme.colorAccent, bg: Theme.colorAccentLight }
                        case "verifying":   return { text: qsTr("Verifying…"),        fg: Theme.colorAccent, bg: Theme.colorAccentLight }
                        case "ready":       return { text: qsTr("Restart to update"), fg: Theme.colorGood,  bg: Theme.colorGoodLight }
                        case "error":       return { text: qsTr("Update check failed"), fg: Theme.colorWarn, bg: Theme.colorWarnLight }
                        case "devbuild":    return { text: qsTr("Development build"), fg: Theme.colorText3, bg: "transparent" }
                        default:            return { text: qsTr("✓  Up to date"),     fg: Theme.colorGood,  bg: Theme.colorGoodLight }  // uptodate / unsupported
                        }
                    }

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

                    // Status badge — hidden in the neutral "idle" state (the action
                    // chip carries "Check now" there).
                    Rectangle {
                        visible: verRow.uState !== "idle"
                        implicitWidth:  badgeText.implicitWidth + Theme.sp(16)
                        implicitHeight: Theme.sp(26)
                        color:          verRow.pal.bg
                        border.width:   1
                        border.color:   verRow.pal.fg
                        radius:         Theme.radius

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: verRow.uState === "downloading"
                                  ? qsTr("Downloading %1%").arg(Math.round(updateController.progress * 100))
                                  : verRow.pal.text
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          verRow.pal.fg
                        }
                    }

                    // Contextual action: Check now / Download & install / Restart now.
                    // Hidden while a check/download/verify is in flight and on
                    // platforms whose updates are native (Sparkle/WinSparkle) or dev.
                    Rectangle {
                        id: updAction
                        readonly property string mode:
                            !updateController.supported ? ""
                            : verRow.uState === "available" ? "download"
                            : verRow.uState === "ready"     ? "restart"
                            : (verRow.uState === "checking" || verRow.uState === "downloading"
                               || verRow.uState === "verifying") ? ""
                            : "check"
                        visible: mode !== ""
                        implicitWidth:  updActionText.implicitWidth + Theme.sp(20)
                        implicitHeight: Theme.sp(26)
                        radius:         Theme.radius
                        color:          updActionMa.containsMouse ? Theme.colorAccentLight : "transparent"
                        border.width:   1
                        border.color:   (mode === "download" || mode === "restart")
                                        ? Theme.colorAccent : Theme.colorBorderStrong
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                        Text {
                            id: updActionText
                            anchors.centerIn: parent
                            text: updAction.mode === "download" ? qsTr("Download & install")
                                : updAction.mode === "restart"  ? qsTr("Restart now")
                                : qsTr("Check now")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color: (updAction.mode === "download" || updAction.mode === "restart")
                                   ? Theme.colorAccent : Theme.colorText2
                        }

                        PpPressable {
                            id: updActionMa
                            onClicked: {
                                if (updAction.mode === "download")      updateController.download()
                                else if (updAction.mode === "restart")  updateController.relaunch()
                                else                                    updateController.checkNow()
                            }
                        }
                    }
                }
            }

            // Updater status / error line (e.g. "End the session to install").
            Text {
                Layout.fillWidth: true
                visible: updateController.supported && text.length > 0
                text: updateController.state === "error" ? updateController.errorString
                      : updateController.statusMessage
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color:          updateController.state === "error" ? Theme.colorWarn : Theme.colorText3
                wrapMode:       Text.WordWrap
            }

            // GPU acceleration row — hardware-adaptive CUDA runtime offer (Windows,
            // CUDA-capable installed build only). Re-probes when the panel appears so a
            // GPU added since launch is reflected without a restart (design §4.4).
            RowLayout {
                objectName: "setting_gpuAccel"
                visible: cudaRuntime.supported
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                Component.onCompleted: cudaRuntime.refresh()

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)

                    Text {
                        text:           qsTr("GPU acceleration")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        Layout.fillWidth: true
                        text: cudaRuntime.runtimeInstalled
                                  ? qsTr("NVIDIA GPU runtime installed — speech and pose run on the GPU")
                              : cudaRuntime.gpuPresent
                                  ? qsTr("NVIDIA GPU detected — install the runtime to accelerate speech and pose")
                                  : qsTr("No NVIDIA GPU detected — running on CPU / cloud")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                        wrapMode:       Text.WordWrap
                    }
                }

                // "Enabled" badge when installed.
                Rectangle {
                    visible: cudaRuntime.runtimeInstalled
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth:  gpuBadge.implicitWidth + Theme.sp(16)
                    implicitHeight: Theme.sp(26)
                    color:          Theme.colorGoodLight
                    border.width:   1
                    border.color:   Theme.colorGood
                    radius:         Theme.radius
                    Text {
                        id: gpuBadge
                        anchors.centerIn: parent
                        text:           qsTr("✓  Enabled")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorGood
                    }
                }

                // Install action when a GPU is present but the runtime isn't (opens the
                // official release page so the -cuda installer is fetched over HTTPS).
                Rectangle {
                    id: gpuInstall
                    visible: cudaRuntime.shouldOffer
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth:  gpuInstallText.implicitWidth + Theme.sp(20)
                    implicitHeight: Theme.sp(26)
                    radius:         Theme.radius
                    color:          gpuInstallMa.containsMouse ? Theme.colorAccentLight : "transparent"
                    border.width:   1
                    border.color:   Theme.colorAccent
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    Text {
                        id: gpuInstallText
                        anchors.centerIn: parent
                        text:           qsTr("Install GPU acceleration")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorAccent
                    }
                    PpPressable {
                        id: gpuInstallMa
                        onClicked:    cudaRuntime.openDownloadPage()
                    }
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 4 — Cloud fallback ──────────────────────────────────────

            Text {
                text: qsTr("CLOUD FALLBACK")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            Text {
                text: qsTr("Run speech and AI in the cloud when no local GPU is available — or always. Each provider needs an API key entered below; that key is the source used for the cloud service.")
                font.family:      Theme.fontBody
                font.pixelSize:   Theme.fontSzBody2
                font.weight:      Theme.fontBodyWeight
                color:            Theme.colorText3
                wrapMode:         Text.WordWrap
                Layout.fillWidth: true
            }

            CloudFallbackRow {
                objectName: "setting_cloudStt"
                title:      qsTr("Speech-to-text (Azure)")
                subtitle:   secrets.hasAzureKey ? qsTr("Transcribe with Azure cloud speech recognition")
                                                : qsTr("Add the Azure Speech key below to enable")
                checked:    appSettings.cloudFallbackStt
                canToggle:  secrets.hasAzureKey
                onToggled:  appSettings.cloudFallbackStt = !appSettings.cloudFallbackStt
            }

            CloudFallbackRow {
                objectName: "setting_cloudTts"
                title:      qsTr("Text-to-speech (Azure)")
                subtitle:   secrets.hasAzureKey ? qsTr("Synthesise speech with Azure cloud voices")
                                                : qsTr("Add the Azure Speech key below to enable")
                checked:    appSettings.cloudFallbackTts
                canToggle:  secrets.hasAzureKey
                onToggled:  appSettings.cloudFallbackTts = !appSettings.cloudFallbackTts
            }

            CloudFallbackRow {
                objectName: "setting_cloudLlm"
                title:      qsTr("AI Coach (Gemini)")
                subtitle:   !llmController.localGpuAvailable
                              ? (secrets.hasGeminiKey ? qsTr("No GPU detected — the cloud model is required")
                                                      : qsTr("No GPU detected — add the Gemini key below"))
                              : (secrets.hasGeminiKey ? qsTr("Run the Google Gemini cloud model")
                                                      : qsTr("Add the Gemini key below to enable"))
                checked:    appSettings.cloudFallbackLlm
                locked:     !llmController.localGpuAvailable
                canToggle:  secrets.hasGeminiKey
                onToggled:  appSettings.cloudFallbackLlm = !appSettings.cloudFallbackLlm
            }

            CloudKeyField {
                objectName:  "setting_azureKey"
                title:       qsTr("Azure Speech key")
                secretName:  "azureTtsApiKey"
                placeholder: qsTr("Azure Cognitive Services key")
            }

            CloudKeyField {
                objectName:  "setting_geminiKey"
                title:       qsTr("Gemini API key")
                secretName:  "geminiApiKey"
                placeholder: qsTr("Google AI Studio API key")
            }
        }
    }
}
