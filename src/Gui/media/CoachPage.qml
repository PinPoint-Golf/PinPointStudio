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
        visible: llmController.downloading || (!llmController.llmReady && !llmController.downloading)
        z: 10

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Theme.sp(16)
            width: Math.min(parent.width - Theme.sp(64), Theme.sp(400))

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: llmController.downloading ? qsTr("Downloading AI Coach") : qsTr("Loading AI Coach…")
                color: Theme.colorText
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzHeading
                font.weight: Font.Normal
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: llmController.downloadStatus
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzBody
                visible: llmController.downloadStatus !== ""
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                Layout.fillWidth: true
                height: Theme.sp(4)
                radius: Theme.sp(2)
                color: Theme.colorBg3
                visible: llmController.downloading

                Rectangle {
                    width: parent.width * llmController.downloadProgress
                    height: parent.height
                    radius: parent.radius
                    color: Theme.colorAccent
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: llmController.downloading
                text: Math.round(llmController.downloadProgress * 100) + "%"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzBody
            }
        }
    }

    // ── Main chat layout ──────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp(16)
        spacing: Theme.sp(8)
        visible: llmController.llmReady

        // ── Header row: backend badge + latency ──────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(8)

            Item { Layout.fillWidth: true }

            Label {
                visible: llmController.lastLlmLatencyMs > 0
                text: llmController.lastLlmLatencyMs < 1000
                      ? llmController.lastLlmLatencyMs + "ms"
                      : (llmController.lastLlmLatencyMs / 1000).toFixed(1) + "s"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
            }

            // Backend badge — same three-state colour scheme as AudioPage STT/TTS badges.
            Rectangle {
                radius: Theme.radius
                color: llmController.llmBackend === "Cloud"
                       ? Theme.colorAccentLight
                       : llmController.llmBackend !== "" && llmController.llmBackend !== "CPU"
                         ? Theme.colorGoodLight
                         : Theme.colorBg3
                scale: llmController.cloudLlmFallbackAvailable
                       ? (llmBadgeTap.pressed ? 0.97 : llmBadgeHover.hovered ? 1.02 : 1.0)
                       : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                border.width: 1
                border.color: llmController.llmBackend === "Cloud"
                              ? Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.25)
                              : llmController.llmBackend !== "" && llmController.llmBackend !== "CPU"
                                ? Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.25)
                                : Theme.colorBorderMid
                implicitWidth: llmBadgeLabel.implicitWidth + Theme.sp(10)
                implicitHeight: llmBadgeLabel.implicitHeight + Theme.sp(4)
                ToolTip.visible: llmBadgeHover.hovered
                ToolTip.text: llmController.cloudLlmFallbackAvailable
                    ? (llmController.llmBackend === "Cloud"
                       ? qsTr("Cloud AI (Gemini) — click to switch to local")
                       : qsTr("Local AI — click to switch to Cloud"))
                    : (llmController.llmBackend === "Cloud"
                       ? qsTr("Cloud AI via Gemini")
                       : llmController.llmBackend !== ""
                         ? qsTr("Local AI — %1").arg(llmController.llmBackend)
                         : qsTr("Local AI — CPU"))

                HoverHandler {
                    id: llmBadgeHover
                    cursorShape: llmController.cloudLlmFallbackAvailable
                                 ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
                TapHandler {
                    id: llmBadgeTap
                    enabled: llmController.cloudLlmFallbackAvailable
                    onTapped: llmController.toggleLlmBackend()
                }

                Label {
                    id: llmBadgeLabel
                    anchors.centerIn: parent
                    text: llmController.llmBackend !== "" ? llmController.llmBackend : qsTr("CPU")
                    color: llmController.llmBackend === "Cloud"
                           ? Theme.colorAccent
                           : llmController.llmBackend !== "" && llmController.llmBackend !== "CPU"
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
                model: llmController.conversation
                spacing: Theme.sp(8)
                bottomMargin: Theme.sp(4)

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
                        width: Math.min(bubbleText.implicitWidth + Theme.sp(20),
                                        chatList.width * 0.82)
                        height: bubbleText.implicitHeight + Theme.sp(16)
                        anchors.left:  isCoach ? parent.left  : undefined
                        anchors.right: isCoach ? undefined     : parent.right
                        radius: Theme.radius
                        color: isCoach ? Theme.colorSurface : Theme.colorAccentLight
                        border.width: 1
                        border.color: isCoach ? Theme.colorBorderMid
                                              : Qt.rgba(Theme.colorAccent.r,
                                                        Theme.colorAccent.g,
                                                        Theme.colorAccent.b, 0.25)

                        Text {
                            id: bubbleText
                            anchors {
                                left: parent.left;  leftMargin:  Theme.sp(10)
                                right: parent.right; rightMargin: Theme.sp(10)
                                top: parent.top;    topMargin:   Theme.sp(8)
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
                        anchors.bottomMargin: Theme.sp(2)
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
            spacing: Theme.sp(8)

            // Voice input chip
            Rectangle {
                id: voiceInChip
                width: voiceInLabel.implicitWidth + Theme.sp(16)
                height: voiceInLabel.implicitHeight + Theme.sp(8)
                radius: height / 2
                color: llmController.voiceInputEnabled ? Theme.colorGoodLight  : Theme.colorBg3
                scale: voiceInTap.pressed ? 0.97 : voiceInHover.hovered ? 1.02 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                border.width: 1
                border.color: llmController.voiceInputEnabled
                              ? Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.35)
                              : Theme.colorBorderMid
                ToolTip.visible: voiceInHover.hovered
                ToolTip.text: llmController.voiceInputEnabled
                              ? qsTr("Voice input ON — STT sends to coach")
                              : qsTr("Voice input OFF — tap to enable")
                HoverHandler { id: voiceInHover; cursorShape: Qt.PointingHandCursor }
                TapHandler   { id: voiceInTap; onTapped: llmController.setVoiceInput(!llmController.voiceInputEnabled) }
                Label {
                    id: voiceInLabel
                    anchors.centerIn: parent
                    text: qsTr("🎤 Voice in")
                    color: llmController.voiceInputEnabled ? Theme.colorGood : Theme.colorText3
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
                color: llmController.voiceOutputEnabled ? Theme.colorGoodLight : Theme.colorBg3
                scale: voiceOutTap.pressed ? 0.97 : voiceOutHover.hovered ? 1.02 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                border.width: 1
                border.color: llmController.voiceOutputEnabled
                              ? Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.35)
                              : Theme.colorBorderMid
                ToolTip.visible: voiceOutHover.hovered
                ToolTip.text: llmController.voiceOutputEnabled
                              ? qsTr("Voice output ON — coach responses are spoken")
                              : qsTr("Voice output OFF — tap to enable")
                HoverHandler { id: voiceOutHover; cursorShape: Qt.PointingHandCursor }
                TapHandler   { id: voiceOutTap; onTapped: llmController.setVoiceOutput(!llmController.voiceOutputEnabled) }
                Label {
                    id: voiceOutLabel
                    anchors.centerIn: parent
                    text: qsTr("🔊 Voice out")
                    color: llmController.voiceOutputEnabled ? Theme.colorGood : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                }
            }

            Item { Layout.fillWidth: true }

            // Clear history button
            Button {
                text: qsTr("Clear")
                enabled: llmController.conversation.length > 0 && !llmController.llmActive
                onClicked: llmController.clearHistory()
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? Theme.colorText3 : Theme.colorBg3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.pressed ? Theme.colorBg3 : "transparent"
                    radius: Theme.radius
                }
            }
        }

        // ── Text input + Send / Stop ─────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(8)

            TextArea {
                id: messageInput
                Layout.fillWidth: true
                placeholderText: qsTr("Ask your coach…")
                placeholderTextColor: Theme.colorText3
                wrapMode: TextArea.Wrap
                color: Theme.colorText
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                padding: Theme.sp(10)
                implicitHeight: Math.max(Theme.sp(40), contentHeight + Theme.sp(20))

                background: Rectangle {
                    color: Theme.colorSurface
                    radius: Theme.radius
                    border.color: messageInput.activeFocus
                                  ? Theme.colorAccent : Theme.colorBorderMid
                    border.width: 1
                }

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
            Button {
                id: sendButton
                text: llmController.llmActive ? qsTr("Stop ■") : qsTr("Send ▶")
                enabled: llmController.llmActive
                         || (llmController.llmReady && messageInput.text.trim().length > 0)
                onClicked: {
                    if (llmController.llmActive) {
                        llmController.stopGeneration()
                    } else {
                        sendAndClear()
                    }
                }
                contentItem: Text {
                    text: sendButton.text
                    color: sendButton.enabled
                           ? (llmController.llmActive ? Theme.colorWarn : Theme.colorBg)
                           : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: sendButton.enabled
                           ? (llmController.llmActive
                              ? Theme.colorWarnLight
                              : (sendButton.pressed
                                 ? Qt.darker(Theme.colorAccent, 1.1)
                                 : Theme.colorAccent))
                           : Theme.colorBg3
                    border.width: llmController.llmActive && sendButton.enabled ? 1 : 0
                    border.color: Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g,
                                          Theme.colorWarn.b, 0.5)
                }
            }
        }
    }

    function sendAndClear() {
        const text = messageInput.text.trim()
        if (text.length === 0)
            return
        llmController.sendMessage(text)
        messageInput.text = ""
    }
}
