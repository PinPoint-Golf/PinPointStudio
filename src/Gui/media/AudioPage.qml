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
                        text: controller.transcript
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
                        enabled: !controller.isListening
                        onClicked: controller.startListening()
                    }

                    PpButton {
                        id: stopListenButton
                        style: "labStop"; size: "lab"
                        label: qsTr("Stop ■")
                        enabled: controller.isListening
                        onClicked: controller.stopListening()
                    }

                    Rectangle {
                        visible: controller.isListening
                        width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
                        color: Theme.colorGood
                        SequentialAnimation on opacity {
                            running: controller.isListening
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.2; duration: 900; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                        }
                    }

                    Label {
                        visible: controller.isListening
                        text: qsTr("Listening…")
                        color: Theme.colorGood
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        visible: controller.lastSttLatencyMs > 0
                        text: controller.lastSttLatencyMs < 1000
                              ? controller.lastSttLatencyMs + "ms"
                              : (controller.lastSttLatencyMs / 1000).toFixed(1) + "s"
                        color: Theme.colorText3
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                    }

                    // STT backend badge
                    Rectangle {
                        radius: Theme.radius
                        color: controller.sttBackend === "Cloud"
                               ? Theme.colorAccentLight
                               : controller.sttBackend !== "" && controller.sttBackend !== "CPU"
                                 ? Theme.colorGoodLight
                                 : Theme.colorBg3
                        border.width: 1
                        border.color: controller.sttBackend === "Cloud"
                                      ? Qt.alpha(Theme.colorAccent, 0.25)
                                      : controller.sttBackend !== "" && controller.sttBackend !== "CPU"
                                        ? Qt.alpha(Theme.colorGood, 0.25)
                                        : Theme.colorBorderMid
                        implicitWidth: sttBackendBadge.implicitWidth + Theme.sp(10)
                        implicitHeight: sttBackendBadge.implicitHeight + Theme.sp(4)
                        ToolTip.visible: sttBackendPress.containsMouse
                        ToolTip.text: controller.cloudSttFallbackAvailable
                            ? (controller.sttBackend === "Cloud"
                               ? qsTr("Cloud STT (Azure) — click to switch to CPU")
                               : qsTr("CPU transcription — click to switch to Cloud"))
                            : (controller.sttBackend === "CPU"   ? qsTr("CPU transcription")
                             : controller.sttBackend === "Cloud" ? qsTr("Cloud transcription via Azure")
                             : qsTr("GPU transcription via %1").arg(controller.sttBackend))
                        // Hovers for its tooltip whatever the backend; moves and toggles only when
                        // there is a fallback to switch to.
                        PpPressable {
                            id: sttBackendPress
                            moves:       controller.cloudSttFallbackAvailable
                            cursorShape: controller.cloudSttFallbackAvailable ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked:   if (controller.cloudSttFallbackAvailable) controller.toggleSttBackend()
                        }

                        Label {
                            id: sttBackendBadge
                            anchors.centerIn: parent
                            text: controller.sttBackend !== "" ? controller.sttBackend : qsTr("CPU")
                            color: controller.sttBackend === "Cloud"
                                   ? Theme.colorAccent
                                   : controller.sttBackend !== "" && controller.sttBackend !== "CPU"
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
                            ttsController.speak(ttsInput.text)
                            event.accepted = true
                        }
                    }
                }

                RowLayout {
                    spacing: Theme.gap(8)

                    PpButton {
                        id: speakButton
                        style: "labPrimary"; size: "lab"
                        label: ttsController.ttsActive ? qsTr("Speaking…") : qsTr("Speak ▶")
                        enabled: ttsController.ttsReady && !ttsController.ttsActive
                                 && ttsInput.text.trim().length > 0
                        onClicked: ttsController.speak(ttsInput.text)
                    }

                    PpButton {
                        id: stopButton
                        style: "labStop"; size: "lab"
                        label: qsTr("Stop ■")
                        enabled: ttsController.ttsActive
                        onClicked: ttsController.stopSpeaking()
                    }

                    PpButton {
                        id: replayButton
                        style: "labOutline"; size: "lab"
                        label: qsTr("Replay ↺")
                        enabled: ttsController.canReplay && !ttsController.ttsActive
                        onClicked: ttsController.replayLastAudio()
                    }

                    Label {
                        visible: ttsController.ttsBackend !== "Cloud"
                        text: qsTr("Voice:")
                        color: Theme.colorText2
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                    }

                    PpComboBox {
                        id: voiceSelector
                        visible: ttsController.ttsBackend !== "Cloud"
                        model: ttsController.voices
                        currentIndex: ttsController.voices.indexOf(ttsController.voice)
                        onActivated: ttsController.voice = currentText
                        implicitWidth: Theme.sp(130)
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        visible: ttsController.lastTtsLatencyMs > 0
                        text: ttsController.lastTtsLatencyMs < 1000
                              ? ttsController.lastTtsLatencyMs + "ms"
                              : (ttsController.lastTtsLatencyMs / 1000).toFixed(1) + "s"
                        color: Theme.colorText3
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                    }

                    Rectangle {
                        width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
                        color: ttsController.ttsReady ? Theme.colorGood : Theme.colorWarn
                        ToolTip.visible: ttsStatusHover.hovered
                        ToolTip.text: ttsController.ttsReady ? qsTr("TTS model ready")
                                                              : qsTr("TTS model not loaded")
                        HoverHandler { id: ttsStatusHover }
                    }

                    Label {
                        text: ttsController.ttsReady    ? qsTr("Ready")
                            : ttsController.downloading ? qsTr("Downloading…")
                            :                             qsTr("Loading…")
                        color: ttsController.ttsReady ? Theme.colorGood : Theme.colorWarn
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                    }

                    // TTS backend badge
                    Rectangle {
                        visible: ttsController.ttsReady
                        radius: Theme.radius
                        color: ttsController.ttsBackend === "Cloud"
                               ? Theme.colorAccentLight
                               : ttsController.ttsBackend !== ""
                                 ? Theme.colorGoodLight
                                 : Theme.colorBg3
                        border.width: 1
                        border.color: ttsController.ttsBackend === "Cloud"
                                      ? Qt.alpha(Theme.colorAccent, 0.25)
                                      : ttsController.ttsBackend !== ""
                                        ? Qt.alpha(Theme.colorGood, 0.25)
                                        : Theme.colorBorderMid
                        implicitWidth: backendBadge.implicitWidth + Theme.sp(10)
                        implicitHeight: backendBadge.implicitHeight + Theme.sp(4)
                        ToolTip.visible: backendPress.containsMouse
                        ToolTip.text: ttsController.cloudTtsFallbackAvailable
                            ? (ttsController.ttsBackend === "Cloud"
                               ? qsTr("Cloud TTS (Azure) — click to switch to CPU")
                               : qsTr("CPU inference — click to switch to Cloud"))
                            : (ttsController.ttsBackend === "Cloud"
                               ? qsTr("Cloud synthesis via Azure Speech")
                               : ttsController.ttsBackend !== ""
                               ? qsTr("GPU inference via %1").arg(ttsController.ttsBackend)
                               : qsTr("CPU inference"))
                        // Hovers for its tooltip whatever the backend; moves and toggles only when
                        // there is a fallback to switch to.
                        PpPressable {
                            id: backendPress
                            moves:       ttsController.cloudTtsFallbackAvailable
                            cursorShape: ttsController.cloudTtsFallbackAvailable ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked:   if (ttsController.cloudTtsFallbackAvailable) ttsController.toggleTtsBackend()
                        }

                        Label {
                            id: backendBadge
                            anchors.centerIn: parent
                            text: ttsController.ttsBackend !== "" ? ttsController.ttsBackend : qsTr("CPU")
                            color: ttsController.ttsBackend === "Cloud"
                                   ? Theme.colorAccent
                                   : ttsController.ttsBackend !== ""
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
                    visible: ttsController.downloading

                    ProgressBar {
                        Layout.fillWidth: true
                        value: ttsController.downloadProgress
                        background: Rectangle {
                            color: Theme.colorBg3
                            radius: Theme.radius - 2
                            implicitHeight: 6
                        }
                        contentItem: Item {
                            Rectangle {
                                width: parent.width * ttsController.downloadProgress
                                height: parent.height
                                radius: Theme.radius - 2
                                color: Theme.colorAccent
                                Behavior on width { NumberAnimation { duration: 150 } }
                            }
                        }
                    }

                    Label {
                        text: ttsController.downloadStatus
                        color: Theme.colorText3
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzLabel
                    }
                }
            }
        }
    }
}
