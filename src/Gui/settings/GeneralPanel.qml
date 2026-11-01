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

// Settings → General (panelIndex 0): language and units, session behaviour, motion-capture quality
// (with the High model's one-time download), the application rows (updates, diagnostics, the
// version and updater status, GPU acceleration) and the cloud fallbacks with their API keys. The
// updater's words and colours are update_states.js's, shared with the About dialog and the banner.

import QtQuick
import QtQuick.Layouts
import PinPointStudio
import "../shell/update_states.js" as UpdateStates

SettingsPanelBase {
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

    // ── Cloud Fallback reusable rows ──────────────────────────────────────────

    // A force-cloud toggle row. `locked` shows ON and is non-interactive (used by
    // the AI Coach when no local GPU exists); `canToggle` gates interaction on the
    // provider API key being configured.
    component CloudFallbackRow: SettingsRow {
        id: cfr
        property string title:     ""
        property string subtitle:  ""
        property bool   checked:   false
        property bool   locked:    false
        property bool   canToggle: true
        signal toggled()

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            // The title column fills, so the switch sits at the row's far edge.
            SettingsRowTitle { title: cfr.title; subtitle: cfr.subtitle }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: cfr.locked || cfr.checked
                enabled: cfr.canToggle && !cfr.locked
                opacity: enabled ? 1.0 : 0.45
                onToggled: cfr.toggled()
            }
        }
    }

    // A masked API-key field (with a Show/Hide reveal) bound to a SecretsManager
    // key via the `secrets` bridge. Commits on focus loss / Enter (PpTextField).
    component CloudKeyField: SettingsRow {
        id: ckf
        property string title:       ""
        property string secretName:  ""
        property string placeholder: ""

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

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

            PpButton {
                id: revealBtn
                property bool revealed: false
                Layout.alignment: Qt.AlignVCenter
                style: "quiet"; size: "micro"; mono: true
                implicitHeight: Theme.sp(34)
                label: revealBtn.revealed ? qsTr("Hide") : qsTr("Show")
                onClicked: revealBtn.revealed = !revealBtn.revealed
            }
        }
    }

    // ── Page header ───────────────────────────────────────────────────────

    PpPageHeader {
        Layout.fillWidth: true
        spacing:     root.contentSpacing
        eyebrow:     Theme.caps(qsTr("Configuration"))
        title:       qsTr("General")
        description: qsTr("Language, measurement units, and application behaviour.")
    }

    // ── Group 1 — Localisation ────────────────────────────────────────────

    PpMicro { kind: "body"; text: Theme.caps(qsTr("Localisation")) }

    // Language row
    SettingsRow {
        objectName: "setting_language"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Language"); subtitle: qsTr("Restart required to apply") }

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
    }

    // Units row
    SettingsRow {
        objectName: "setting_units"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Units"); subtitle: qsTr("Speed and distance displayed throughout") }

            PpSegmentedControl {
                style:    "unit"
                options:  ["mph", "km/h"]
                selected: appSettings.units === "mph" ? "mph" : "km/h"
                onActivated: (unit) => appSettings.units = (unit === "mph" ? "mph" : "kmh")
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Group 2 — Session behaviour ───────────────────────────────────────

    PpMicro { kind: "body"; text: Theme.caps(qsTr("Session behaviour")) }

    // Auto-detect swing start row
    SettingsRow {
        objectName: "setting_autoDetect"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Auto-detect swing start"); subtitle: qsTr("Begins capture when motion exceeds threshold") }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: appSettings.autoDetectSwing
                onToggled: (v) => appSettings.autoDetectSwing = v
            }
        }
    }

    // Swing detection sensitivity row
    SettingsRow {
        objectName: "setting_swingSensitivity"
        opacity: appSettings.autoDetectSwing ? 1.0 : 0.4
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Swing detection sensitivity"); subtitle: qsTr("Lower values trigger on slower movements") }

            PpChipGroup {
                options:  [qsTr("Low"), qsTr("Medium"), qsTr("High")]
                selected: appSettings.swingDetectionSensitivity
                onSelectionChanged: (value) => appSettings.swingDetectionSensitivity = value
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // AI coaching on session end row
    SettingsRow {
        objectName: "setting_aiCoaching"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("AI coaching on session end"); subtitle: qsTr("Automatically generate a Claude observation") }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: appSettings.aiCoachingOnSessionEnd
                onToggled: (v) => appSettings.aiCoachingOnSessionEnd = v
            }
        }
    }

    // Speak calibration instructions (EXPERIMENTAL, 27 Oct 2026) — the trunk routine says when to
    // start each turn and when to turn back to address, through ttsController. Instructions
    // only: nothing else is ever spoken (Mark's rule; TrunkRoutine.qml).
    SettingsRow {
        objectName: "setting_spokenInstructions"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle {
                title:    qsTr("Speak calibration instructions (experimental)")
                subtitle: qsTr("Says when to start each turn and when to turn back to address, so you needn't look at the screen.")
            }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: appSettings.experimentalSpokenInstructions
                onToggled: (v) => appSettings.experimentalSpokenInstructions = v
            }
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Group 3 — Motion capture ──────────────────────────────────────────

    PpMicro { kind: "body"; text: Theme.caps(qsTr("Motion capture")) }

    // Motion capture quality row + on-demand High-tier model download.
    ColumnLayout {
        id: mcSection
        Layout.fillWidth: true
        spacing: Theme.gap(10)

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

        SettingsRow {
            objectName: "setting_motionCapture"
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

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap(16)

                SettingsRowTitle {
                    title:    qsTr("Motion capture quality")
                    subtitle: qsTr("Higher settings track your swing in more detail but take longer to analyse")

                    // Status line (a third line under the title and subtitle), in precedence
                    // order: measuring → (High selected on an unsupported machine) block reason →
                    // measured estimate → not yet measured. secondsPerSwing[tier] is -1 until
                    // measured, which falls through to the last case.
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
                    options:  [qsTr("Medium"), qsTr("High")]
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
            implicitHeight: dlCol.implicitHeight + Theme.gap(20)
            radius: Theme.radius
            color:  Theme.colorBg2
            border.width: 1
            border.color: Theme.colorBorder

            readonly property real gbSize: Math.round(motionCaptureProbe.highModelSizeBytes / 1e9 * 10) / 10
            readonly property int  pct:    Math.max(0, Math.round(motionCaptureProbe.highModelDownloadProgress * 100))

            ColumnLayout {
                id: dlCol
                anchors.fill: parent
                anchors.margins: Theme.gap(10)
                spacing: Theme.gap(8)

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
                    spacing: Theme.gap(8)
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

    // ── Group 4 — Application ────────────────────────────────────────────

    PpMicro { kind: "body"; text: Theme.caps(qsTr("Application")) }

    // Check for updates row
    SettingsRow {
        objectName: "setting_checkUpdates"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Check for updates automatically"); subtitle: qsTr("Checks on launch — never downloads without confirmation") }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: appSettings.checkForUpdates
                onToggled: (v) => appSettings.checkForUpdates = v
            }
        }
    }

    // Send anonymous diagnostics row
    SettingsRow {
        objectName: "setting_diagnostics"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle { title: qsTr("Send anonymous diagnostics"); subtitle: qsTr("Crash reports and performance data only — no session content") }

            PpSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: appSettings.sendDiagnostics
                onToggled: (v) => appSettings.sendDiagnostics = v
            }
        }
    }

    // Version row
    SettingsRow {
        objectName: "setting_version"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            Text {
                text:           qsTr("Version")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                color:          Theme.colorText
                Layout.fillWidth: true
            }

            Row {
                id: verRow
                spacing: Theme.gap(10)
                Layout.alignment: Qt.AlignVCenter

                // Live updater state (Linux AppImage). Off-Linux / dev builds
                // report "unsupported"/"devbuild" and the action is hidden.
                readonly property string uState: updateController.state

                // The status badge {text, fg, bg}, in the words update_states.js gives every
                // updater surface ("short": "Checking…" fits the row of chips).
                readonly property var pal: UpdateStates.badge(uState, "short", updateController.progress, Theme)

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
                PpStatusPill {
                    visible: verRow.uState !== "idle"
                    text: verRow.pal.text
                    fg:   verRow.pal.fg
                    bg:   verRow.pal.bg
                    edge: verRow.pal.fg
                }

                // Contextual action: Check now / Download & install / Restart now.
                // Hidden while a check/download/verify is in flight and on
                // platforms whose updates are native (Sparkle/WinSparkle) or dev.
                PpButton {
                    id: updAction
                    readonly property string mode:
                        !updateController.supported ? "" : UpdateStates.actionMode(verRow.uState)
                    visible: mode !== ""
                    style: "tone"; size: "micro"; mono: true
                    tone: (mode === "download" || mode === "restart") ? "accent" : ""
                    label: UpdateStates.actionLabel(updAction.mode, "short")
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
    SettingsRow {
        objectName: "setting_gpuAccel"
        visible: cudaRuntime.supported
        Component.onCompleted: cudaRuntime.refresh()
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle {
                title: qsTr("GPU acceleration")
                subtitle: cudaRuntime.runtimeInstalled
                              ? qsTr("NVIDIA GPU runtime installed — speech and pose run on the GPU")
                          : cudaRuntime.gpuPresent
                              ? qsTr("NVIDIA GPU detected — install the runtime to accelerate speech and pose")
                              : qsTr("No NVIDIA GPU detected — running on CPU / cloud")
            }

            // "Enabled" badge when installed.
            PpStatusPill {
                visible: cudaRuntime.runtimeInstalled
                Layout.alignment: Qt.AlignVCenter
                tone: "good"
                text: qsTr("✓  Enabled")
            }

            // Install action when a GPU is present but the runtime isn't (opens the
            // official release page so the -cuda installer is fetched over HTTPS).
            PpButton {
                id: gpuInstall
                visible: cudaRuntime.shouldOffer
                Layout.alignment: Qt.AlignVCenter
                style: "tone"; tone: "accent"; size: "micro"; mono: true
                label: qsTr("Install GPU acceleration")
                onClicked: cudaRuntime.openDownloadPage()
            }
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Group 4 — Cloud fallback ──────────────────────────────────────────

    PpMicro { kind: "body"; text: Theme.caps(qsTr("Cloud fallback")) }

    PpCardNote {
        kind: "plain"
        text: qsTr("Run speech and AI in the cloud when no local GPU is available — or always. Each provider needs an API key entered below; that key is the source used for the cloud service.")
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
