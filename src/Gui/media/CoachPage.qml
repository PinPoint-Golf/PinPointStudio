/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {

    // ── Download / loading overlay ───────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.colorBg
        visible: app.llmController.downloading || (!app.llmController.llmReady && !app.llmController.downloading)
        z: 10

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Theme.gap(16)
            width: Math.min(parent.width - Theme.gap(64), Theme.sp(400))

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: app.llmController.downloading ? qsTr("Downloading AI Coach") : qsTr("Loading AI Coach…")
                color: Theme.colorText
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzHeading
                font.weight: Font.Normal
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: app.llmController.downloadStatus
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzBody
                visible: app.llmController.downloadStatus !== ""
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                Layout.fillWidth: true
                height: Theme.sp(4)
                radius: Theme.sp(2)
                color: Theme.colorBg3
                visible: app.llmController.downloading

                Rectangle {
                    width: parent.width * app.llmController.downloadProgress
                    height: parent.height
                    radius: parent.radius
                    color: Theme.colorAccent
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: app.llmController.downloading
                text: Math.round(app.llmController.downloadProgress * 100) + "%"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzBody
            }
        }
    }

    // ── Main chat layout ──────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap(16)
        spacing: Theme.gap(8)
        visible: app.llmController.llmReady

        // ── Header row: backend badge + latency ──────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(8)

            Item { Layout.fillWidth: true }

            Label {
                visible: app.llmController.lastLlmLatencyMs > 0
                text: app.llmController.lastLlmLatencyMs < 1000
                      ? app.llmController.lastLlmLatencyMs + "ms"
                      : (app.llmController.lastLlmLatencyMs / 1000).toFixed(1) + "s"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
            }

            // Backend badge — same three-state colour scheme as AudioPage STT/TTS badges.
            Rectangle {
                radius: Theme.radius
                color: app.llmController.llmBackend === "Cloud"
                       ? Theme.colorAccentLight
                       : app.llmController.llmBackend !== "" && app.llmController.llmBackend !== "CPU"
                         ? Theme.colorGoodLight
                         : Theme.colorBg3
                border.width: 1
                border.color: app.llmController.llmBackend === "Cloud"
                              ? Qt.alpha(Theme.colorAccent, 0.25)
                              : app.llmController.llmBackend !== "" && app.llmController.llmBackend !== "CPU"
                                ? Qt.alpha(Theme.colorGood, 0.25)
                                : Theme.colorBorderMid
                implicitWidth: llmBadgeLabel.implicitWidth + Theme.sp(10)
                implicitHeight: llmBadgeLabel.implicitHeight + Theme.sp(4)
                ToolTip.visible: llmBadgePress.containsMouse
                ToolTip.text: app.llmController.cloudLlmFallbackAvailable
                    ? (app.llmController.llmBackend === "Cloud"
                       ? qsTr("Cloud AI (Gemini) — click to switch to local")
                       : qsTr("Local AI — click to switch to Cloud"))
                    : (app.llmController.llmBackend === "Cloud"
                       ? qsTr("Cloud AI via Gemini")
                       : app.llmController.llmBackend !== ""
                         ? qsTr("Local AI — %1").arg(app.llmController.llmBackend)
                         : qsTr("Local AI — CPU"))

                // Hovers for its tooltip whatever the backend; moves and toggles only when
                // there is a fallback to switch to.
                PpPressable {
                    id: llmBadgePress
                    moves:       app.llmController.cloudLlmFallbackAvailable
                    cursorShape: app.llmController.cloudLlmFallbackAvailable ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked:   if (app.llmController.cloudLlmFallbackAvailable) app.llmController.toggleLlmBackend()
                }

                Label {
                    id: llmBadgeLabel
                    anchors.centerIn: parent
                    text: app.llmController.llmBackend !== "" ? app.llmController.llmBackend : qsTr("CPU")
                    color: app.llmController.llmBackend === "Cloud"
                           ? Theme.colorAccent
                           : app.llmController.llmBackend !== "" && app.llmController.llmBackend !== "CPU"
                             ? Theme.colorGood
                             : Theme.colorText3
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.weight: Font.Normal
                }
            }
        }

        // ── Conversation list ────────────────────────────────────────────────
        ScrollView {
            id: chatScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ListView {
                id: chatList
                model: app.llmController.conversation
                spacing: Theme.gap(8)
                bottomMargin: Theme.gap(4)

                // Auto-scroll to bottom as new tokens arrive.
                onCountChanged:     Qt.callLater(() => chatList.positionViewAtEnd())
                onContentYChanged:  {}

                delegate: Item {
                    width: chatList.width
                    height: bubble.height + Theme.sp(4)

                    readonly property bool isCoach: modelData.role === "model"
                    readonly property bool isPartial: modelData.partial === true

                    // Coach messages: left-aligned. User messages: right-aligned.
                    Rectangle {
                        id: bubble
                        width: Math.min(bubbleText.implicitWidth + Theme.gap(20),
                                        chatList.width * 0.82)
                        height: bubbleText.implicitHeight + Theme.gap(16)
                        anchors.left:  isCoach ? parent.left  : undefined
                        anchors.right: isCoach ? undefined     : parent.right
                        radius: Theme.radius
                        color: isCoach ? Theme.colorSurface : Theme.colorAccentLight
                        border.width: 1
                        border.color: isCoach ? Theme.colorBorderMid
                                              : Qt.alpha(Theme.colorAccent, 0.25)

                        Text {
                            id: bubbleText
                            anchors {
                                left: parent.left;  leftMargin:  Theme.gap(10)
                                right: parent.right; rightMargin: Theme.gap(10)
                                top: parent.top;    topMargin:   Theme.gap(8)
                            }
                            text: modelData.text + (isPartial && isCoach ? "▌" : "")
                            wrapMode: Text.WordWrap
                            color: Theme.colorText
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontSzBody

                            // Blink the cursor while the model is streaming.
                            SequentialAnimation on opacity {
                                running: isPartial && isCoach
                                loops:   Animation.Infinite
                                NumberAnimation { to: 0.4; duration: 500 }
                                NumberAnimation { to: 1.0; duration: 500 }
                            }
                        }
                    }

                    // Role label (small, muted) above each bubble.
                    Label {
                        anchors.bottom:  bubble.top
                        anchors.bottomMargin: Theme.gap(2)
                        anchors.left:  isCoach ? bubble.left  : undefined
                        anchors.right: isCoach ? undefined     : bubble.right
                        text: isCoach ? qsTr("Coach") : qsTr("You")
                        color: Theme.colorText3
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                    }
                }
            }
        }

        // ── Voice toggles + input row ────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(8)

            // Voice input chip
            Rectangle {
                id: voiceInChip
                width: voiceInLabel.implicitWidth + Theme.sp(16)
                height: voiceInLabel.implicitHeight + Theme.sp(8)
                radius: height / 2
                color: app.llmController.voiceInputEnabled ? Theme.colorGoodLight  : Theme.colorBg3
                border.width: 1
                border.color: app.llmController.voiceInputEnabled
                              ? Qt.alpha(Theme.colorGood, 0.35)
                              : Theme.colorBorderMid
                ToolTip.visible: voiceInPress.containsMouse
                ToolTip.text: app.llmController.voiceInputEnabled
                              ? qsTr("Voice input ON — STT sends to coach")
                              : qsTr("Voice input OFF — tap to enable")
                PpPressable { id: voiceInPress; onClicked: app.llmController.setVoiceInput(!app.llmController.voiceInputEnabled) }
                Label {
                    id: voiceInLabel
                    anchors.centerIn: parent
                    text: qsTr("Voice in")
                    color: app.llmController.voiceInputEnabled ? Theme.colorGood : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                }
            }

            // Voice output chip
            Rectangle {
                id: voiceOutChip
                width: voiceOutLabel.implicitWidth + Theme.sp(16)
                height: voiceOutLabel.implicitHeight + Theme.sp(8)
                radius: height / 2
                color: app.llmController.voiceOutputEnabled ? Theme.colorGoodLight : Theme.colorBg3
                border.width: 1
                border.color: app.llmController.voiceOutputEnabled
                              ? Qt.alpha(Theme.colorGood, 0.35)
                              : Theme.colorBorderMid
                ToolTip.visible: voiceOutPress.containsMouse
                ToolTip.text: app.llmController.voiceOutputEnabled
                              ? qsTr("Voice output ON — coach responses are spoken")
                              : qsTr("Voice output OFF — tap to enable")
                PpPressable { id: voiceOutPress; onClicked: app.llmController.setVoiceOutput(!app.llmController.voiceOutputEnabled) }
                Label {
                    id: voiceOutLabel
                    anchors.centerIn: parent
                    text: qsTr("Voice out")
                    color: app.llmController.voiceOutputEnabled ? Theme.colorGood : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                }
            }

            Item { Layout.fillWidth: true }

            // Clear history button
            PpButton {
                style: "labText"; size: "lab"
                label: qsTr("Clear")
                enabled: app.llmController.conversation.length > 0 && !app.llmController.llmActive
                onClicked: app.llmController.clearHistory()
            }
        }

        // ── Text input + Send / Stop ─────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(8)

            PpTextArea {
                id: messageInput
                style: "lab"
                Layout.fillWidth: true
                placeholderText: qsTr("Ask your coach…")
                implicitHeight: Math.max(Theme.sp(40), contentHeight + Theme.sp(20))

                // Ctrl+Return sends the message.
                Keys.onPressed: function(event) {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                            && (event.modifiers & Qt.ControlModifier)) {
                        sendAndClear()
                        event.accepted = true
                    }
                }
            }

            // Send / Stop button
            PpButton {
                id: sendButton
                style: app.llmController.llmActive ? "labStop" : "labPrimary"; size: "lab"
                label: app.llmController.llmActive ? qsTr("Stop ■") : qsTr("Send ▶")
                enabled: app.llmController.llmActive
                         || (app.llmController.llmReady && messageInput.text.trim().length > 0)
                onClicked: {
                    if (app.llmController.llmActive) {
                        app.llmController.stopGeneration()
                    } else {
                        sendAndClear()
                    }
                }
            }
        }
    }

    function sendAndClear() {
        const text = messageInput.text.trim()
        if (text.length === 0)
            return
        app.llmController.sendMessage(text)
        messageInput.text = ""
    }
}
