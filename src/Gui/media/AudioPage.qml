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
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPointStudio

Item {

    SplitView {
        id: splitView
        anchors.fill: parent
        orientation: Qt.Vertical

        handle: Rectangle {
            implicitHeight: Theme.sp(8)
            color: SplitHandle.hovered || SplitHandle.pressed ? Theme.colorBg3 : Theme.colorBg2

            Rectangle {
                width: Theme.sp(40); height: Theme.sp(2)
                anchors.centerIn: parent
                radius: Theme.sp(1)
                color: Theme.colorText3
            }
        }

        // ── Live transcript pane ─────────────────────────────────────────────
        Item {
            SplitView.preferredHeight: splitView.height / 2
            SplitView.minimumHeight: 80

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.gap(16)
                anchors.bottomMargin: Theme.gap(8)
                spacing: Theme.gap(12)

                Label {
                    text: qsTr("Live Transcript")
                    color: Theme.colorText
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzHeading
                    font.weight: Font.Normal
                }

                ScrollView {
                    id: scrollView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        id: transcriptArea
                        readOnly: true
                        text: app.transcriptionController.transcript
                        wrapMode: TextArea.Wrap
                        color: Theme.colorText
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzBody
                        padding: Theme.gap(12)
                        background: Rectangle {
                            color: Theme.colorSurface
                            radius: Theme.radius
                            border.width: 1
                            border.color: Theme.colorBorderMid
                        }
                        onTextChanged: scrollView.ScrollBar.vertical.position =
                            Math.max(0, 1.0 - scrollView.ScrollBar.vertical.size)
                    }
                }

                RowLayout {
                    spacing: Theme.gap(8)

                    PpButton {
                        id: listenButton
                        style: "labPrimary"; size: "lab"
                        label: qsTr("Listen ●")
                        enabled: !app.transcriptionController.isListening
                        onClicked: app.transcriptionController.startListening()
                    }

                    PpButton {
                        id: stopListenButton
                        style: "labStop"; size: "lab"
                        label: qsTr("Stop ■")
                        enabled: app.transcriptionController.isListening
                        onClicked: app.transcriptionController.stopListening()
                    }

                    Rectangle {
                        visible: app.transcriptionController.isListening
                        width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
                        color: Theme.colorGood
                        SequentialAnimation on opacity {
                            running: app.transcriptionController.isListening
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.2; duration: 900; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                        }
                    }

                    Label {
                        visible: app.transcriptionController.isListening
                        text: qsTr("Listening…")
                        color: Theme.colorGood
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        visible: app.transcriptionController.lastSttLatencyMs > 0
                        text: app.transcriptionController.lastSttLatencyMs < 1000
                              ? app.transcriptionController.lastSttLatencyMs + "ms"
                              : (app.transcriptionController.lastSttLatencyMs / 1000).toFixed(1) + "s"
                        color: Theme.colorText3
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                    }

                    // STT backend badge
                    Rectangle {
                        radius: Theme.radius
                        color: app.transcriptionController.sttBackend === "Cloud"
                               ? Theme.colorAccentLight
                               : app.transcriptionController.sttBackend !== "" && app.transcriptionController.sttBackend !== "CPU"
                                 ? Theme.colorGoodLight
                                 : Theme.colorBg3
                        border.width: 1
                        border.color: app.transcriptionController.sttBackend === "Cloud"
                                      ? Qt.alpha(Theme.colorAccent, 0.25)
                                      : app.transcriptionController.sttBackend !== "" && app.transcriptionController.sttBackend !== "CPU"
                                        ? Qt.alpha(Theme.colorGood, 0.25)
                                        : Theme.colorBorderMid
                        implicitWidth: sttBackendBadge.implicitWidth + Theme.sp(10)
                        implicitHeight: sttBackendBadge.implicitHeight + Theme.sp(4)
                        ToolTip.visible: sttBackendPress.containsMouse
                        ToolTip.text: app.transcriptionController.cloudSttFallbackAvailable
                            ? (app.transcriptionController.sttBackend === "Cloud"
                               ? qsTr("Cloud STT (Azure) — click to switch to CPU")
                               : qsTr("CPU transcription — click to switch to Cloud"))
                            : (app.transcriptionController.sttBackend === "CPU"   ? qsTr("CPU transcription")
                             : app.transcriptionController.sttBackend === "Cloud" ? qsTr("Cloud transcription via Azure")
                             : qsTr("GPU transcription via %1").arg(app.transcriptionController.sttBackend))
                        // Hovers for its tooltip whatever the backend; moves and toggles only when
                        // there is a fallback to switch to.
                        PpPressable {
                            id: sttBackendPress
                            moves:       app.transcriptionController.cloudSttFallbackAvailable
                            cursorShape: app.transcriptionController.cloudSttFallbackAvailable ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked:   if (app.transcriptionController.cloudSttFallbackAvailable) app.transcriptionController.toggleSttBackend()
                        }

                        Label {
                            id: sttBackendBadge
                            anchors.centerIn: parent
                            text: app.transcriptionController.sttBackend !== "" ? app.transcriptionController.sttBackend : qsTr("CPU")
                            color: app.transcriptionController.sttBackend === "Cloud"
                                   ? Theme.colorAccent
                                   : app.transcriptionController.sttBackend !== "" && app.transcriptionController.sttBackend !== "CPU"
                                     ? Theme.colorGood
                                     : Theme.colorText3
                            font.family: Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            font.weight: Font.Normal
                        }
                    }
                }
            }
        }

        // ── Text-to-speech pane ──────────────────────────────────────────────
        Item {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 80

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.gap(16)
                anchors.topMargin: Theme.gap(8)
                spacing: Theme.gap(12)

                Label {
                    text: qsTr("Text to Speech")
                    color: Theme.colorText
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzHeading
                    font.weight: Font.Normal
                }

                PpTextArea {
                    id: ttsInput
                    style: "lab"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    placeholderText: qsTr("Type text to speak…")
                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                && (event.modifiers & Qt.ControlModifier)) {
                            app.ttsController.speak(ttsInput.text)
                            event.accepted = true
                        }
                    }
                }

                RowLayout {
                    spacing: Theme.gap(8)

                    PpButton {
                        id: speakButton
                        style: "labPrimary"; size: "lab"
                        label: app.ttsController.ttsActive ? qsTr("Speaking…") : qsTr("Speak ▶")
                        enabled: app.ttsController.ttsReady && !app.ttsController.ttsActive
                                 && ttsInput.text.trim().length > 0
                        onClicked: app.ttsController.speak(ttsInput.text)
                    }

                    PpButton {
                        id: stopButton
                        style: "labStop"; size: "lab"
                        label: qsTr("Stop ■")
                        enabled: app.ttsController.ttsActive
                        onClicked: app.ttsController.stopSpeaking()
                    }

                    PpButton {
                        id: replayButton
                        style: "labOutline"; size: "lab"
                        label: qsTr("Replay ↺")
                        enabled: app.ttsController.canReplay && !app.ttsController.ttsActive
                        onClicked: app.ttsController.replayLastAudio()
                    }

                    Label {
                        visible: app.ttsController.ttsBackend !== "Cloud"
                        text: qsTr("Voice:")
                        color: Theme.colorText2
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                    }

                    PpComboBox {
                        id: voiceSelector
                        visible: app.ttsController.ttsBackend !== "Cloud"
                        model: app.ttsController.voices
                        currentIndex: app.ttsController.voices.indexOf(app.ttsController.voice)
                        onActivated: app.ttsController.voice = currentText
                        implicitWidth: Theme.sp(130)
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        visible: app.ttsController.lastTtsLatencyMs > 0
                        text: app.ttsController.lastTtsLatencyMs < 1000
                              ? app.ttsController.lastTtsLatencyMs + "ms"
                              : (app.ttsController.lastTtsLatencyMs / 1000).toFixed(1) + "s"
                        color: Theme.colorText3
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                    }

                    Rectangle {
                        width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
                        color: app.ttsController.ttsReady ? Theme.colorGood : Theme.colorWarn
                        ToolTip.visible: ttsStatusHover.hovered
                        ToolTip.text: app.ttsController.ttsReady ? qsTr("TTS model ready")
                                                              : qsTr("TTS model not loaded")
                        HoverHandler { id: ttsStatusHover }
                    }

                    Label {
                        text: app.ttsController.ttsReady    ? qsTr("Ready")
                            : app.ttsController.downloading ? qsTr("Downloading…")
                            :                             qsTr("Loading…")
                        color: app.ttsController.ttsReady ? Theme.colorGood : Theme.colorWarn
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                    }

                    // TTS backend badge
                    Rectangle {
                        visible: app.ttsController.ttsReady
                        radius: Theme.radius
                        color: app.ttsController.ttsBackend === "Cloud"
                               ? Theme.colorAccentLight
                               : app.ttsController.ttsBackend !== ""
                                 ? Theme.colorGoodLight
                                 : Theme.colorBg3
                        border.width: 1
                        border.color: app.ttsController.ttsBackend === "Cloud"
                                      ? Qt.alpha(Theme.colorAccent, 0.25)
                                      : app.ttsController.ttsBackend !== ""
                                        ? Qt.alpha(Theme.colorGood, 0.25)
                                        : Theme.colorBorderMid
                        implicitWidth: backendBadge.implicitWidth + Theme.sp(10)
                        implicitHeight: backendBadge.implicitHeight + Theme.sp(4)
                        ToolTip.visible: backendPress.containsMouse
                        ToolTip.text: app.ttsController.cloudTtsFallbackAvailable
                            ? (app.ttsController.ttsBackend === "Cloud"
                               ? qsTr("Cloud TTS (Azure) — click to switch to CPU")
                               : qsTr("CPU inference — click to switch to Cloud"))
                            : (app.ttsController.ttsBackend === "Cloud"
                               ? qsTr("Cloud synthesis via Azure Speech")
                               : app.ttsController.ttsBackend !== ""
                               ? qsTr("GPU inference via %1").arg(app.ttsController.ttsBackend)
                               : qsTr("CPU inference"))
                        // Hovers for its tooltip whatever the backend; moves and toggles only when
                        // there is a fallback to switch to.
                        PpPressable {
                            id: backendPress
                            moves:       app.ttsController.cloudTtsFallbackAvailable
                            cursorShape: app.ttsController.cloudTtsFallbackAvailable ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked:   if (app.ttsController.cloudTtsFallbackAvailable) app.ttsController.toggleTtsBackend()
                        }

                        Label {
                            id: backendBadge
                            anchors.centerIn: parent
                            text: app.ttsController.ttsBackend !== "" ? app.ttsController.ttsBackend : qsTr("CPU")
                            color: app.ttsController.ttsBackend === "Cloud"
                                   ? Theme.colorAccent
                                   : app.ttsController.ttsBackend !== ""
                                     ? Theme.colorGood
                                     : Theme.colorText3
                            font.family: Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            font.weight: Font.Normal
                        }
                    }
                }

                // Download progress — only visible while fetching model files
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap(4)
                    visible: app.ttsController.downloading

                    ProgressBar {
                        Layout.fillWidth: true
                        value: app.ttsController.downloadProgress
                        background: Rectangle {
                            color: Theme.colorBg3
                            radius: Theme.radius - 2
                            implicitHeight: 6
                        }
                        contentItem: Item {
                            Rectangle {
                                width: parent.width * app.ttsController.downloadProgress
                                height: parent.height
                                radius: Theme.radius - 2
                                color: Theme.colorAccent
                                Behavior on width { NumberAnimation { duration: 150 } }
                            }
                        }
                    }

                    Label {
                        text: app.ttsController.downloadStatus
                        color: Theme.colorText3
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzLabel
                    }
                }
            }
        }
    }
}
