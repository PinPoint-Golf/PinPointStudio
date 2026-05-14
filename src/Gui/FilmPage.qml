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
import QtMultimedia

Item {

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            text: "Film"
            color: "#cdd6f4"
            font.pixelSize: 20
            font.bold: true
        }

        // ── URL + download controls ───────────────────────────────────────────
        RowLayout {
            spacing: 8

            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: qsTr("Paste YouTube URL…")
                color: "#cdd6f4"
                placeholderTextColor: "#6c7086"
                font.pixelSize: 13
                leftPadding: 8; rightPadding: 8
                background: Rectangle { color: "#313244"; radius: 6 }
                Keys.onReturnPressed: downloadBtn.clicked()
                Keys.onEnterPressed:  downloadBtn.clicked()
            }

            // Browser selector — default to Firefox on Linux (Chrome cookies require
            // secretstorage which is unavailable in the bundled yt-dlp binary)
            ComboBox {
                id: browserBox
                model: ["chrome", "firefox", "safari", "edge", "brave"]
                currentIndex: Qt.platform.os === "linux" ? 4 : 0
                implicitWidth: 90
                contentItem: Text {
                    leftPadding: 8
                    text: browserBox.displayText
                    color: "#cdd6f4"
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: "#313244"; radius: 6 }
                popup.background: Rectangle { color: "#313244"; radius: 6 }
                delegate: ItemDelegate {
                    width: browserBox.width
                    contentItem: Text {
                        text: modelData
                        color: "#cdd6f4"
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: hovered ? "#45475a" : "#313244"
                    }
                }
            }

            Button {
                id: downloadBtn
                text: filmController.isDownloading ? qsTr("Cancel") : qsTr("Download")
                enabled: filmController.isDownloading || urlField.text.trim().length > 0
                onClicked: {
                    if (filmController.isDownloading)
                        filmController.cancelDownload()
                    else
                        filmController.downloadUrl(urlField.text, browserBox.currentText)
                }
                contentItem: Text {
                    text: downloadBtn.text
                    color: downloadBtn.enabled ? "#1e1e2e" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: {
                        if (!downloadBtn.enabled) return "#313244"
                        if (filmController.isDownloading)
                            return downloadBtn.pressed ? "#f38ba8" : "#e64553"
                        return downloadBtn.pressed ? "#7287fd" : "#89b4fa"
                    }
                    radius: 6
                }
            }
        }

        // ── Download progress ─────────────────────────────────────────────────
        ColumnLayout {
            visible: filmController.isDownloading || filmController.downloadStatus.length > 0
            spacing: 4

            ProgressBar {
                Layout.fillWidth: true
                value: filmController.downloadProgress
                background: Rectangle { color: "#313244"; radius: 3; implicitHeight: 8 }
                contentItem: Item {
                    Rectangle {
                        width: parent.width * filmController.downloadProgress
                        height: parent.height
                        radius: 3
                        color: "#89b4fa"
                    }
                }
            }

            Label {
                text: filmController.downloadStatus
                color: filmController.downloadStatus.startsWith("Download failed") ||
                       filmController.downloadStatus.startsWith("Playback error") ||
                       filmController.downloadStatus.startsWith("Could not")
                       ? "#f38ba8" : "#6c7086"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // ── Film cache ────────────────────────────────────────────────────────
        ColumnLayout {
            visible: filmController.cacheEntries.length > 0
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                spacing: 6
                Label {
                    text: qsTr("CACHED")
                    color: "#6c7086"
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1.2
                }
                Label {
                    text: filmController.cacheEntries.length + " video" +
                          (filmController.cacheEntries.length !== 1 ? "s" : "")
                    color: "#45475a"
                    font.pixelSize: 10
                }
            }

            ListView {
                id: cacheList
                Layout.fillWidth: true
                implicitHeight: 116
                orientation: Qt.Horizontal
                spacing: 8
                clip: true
                model: filmController.cacheEntries

                delegate: Item {
                    id: cacheCard
                    width: 164
                    height: 114

                    HoverHandler { id: cardHover }

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: modelData.path === filmController.currentFilePath
                               ? "#2a2a3e" : (cardHover.hovered ? "#252535" : "#181825")
                        border.color: modelData.path === filmController.currentFilePath
                                      ? "#89b4fa" : "transparent"
                        border.width: 1

                        // Thumbnail area
                        Rectangle {
                            id: thumbRect
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 1
                            height: 92
                            radius: 5
                            color: "#0d0d1a"
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: modelData.thumbnailUrl || ""
                                fillMode: Image.PreserveAspectCrop
                                visible: modelData.thumbnailUrl !== ""
                                smooth: true
                            }

                            Label {
                                anchors.centerIn: parent
                                visible: !modelData.thumbnailUrl
                                text: "▶"
                                font.pixelSize: 22
                                color: "#45475a"
                            }

                            // Duration badge
                            Rectangle {
                                visible: modelData.durationMs > 0
                                anchors.bottom: parent.bottom
                                anchors.right: parent.right
                                anchors.margins: 4
                                width: durLabel.implicitWidth + 8
                                height: 16
                                radius: 3
                                color: "#cc000000"

                                Label {
                                    id: durLabel
                                    anchors.centerIn: parent
                                    text: formatTime(modelData.durationMs)
                                    color: "#ffffff"
                                    font.pixelSize: 10
                                    font.family: "Courier New"
                                }
                            }

                            // Delete button
                            Rectangle {
                                visible: cardHover.hovered
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 4
                                width: 18; height: 18; radius: 9
                                color: "#e64553"

                                Text {
                                    anchors.centerIn: parent
                                    text: "×"
                                    color: "#ffffff"
                                    font.pixelSize: 13
                                    font.bold: true
                                }

                                TapHandler {
                                    onTapped: filmController.deleteCacheFile(modelData.path)
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                        }

                        // Title / filename
                        Label {
                            anchors.top: thumbRect.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 3
                            anchors.leftMargin: 5
                            anchors.rightMargin: 5
                            text: modelData.title || modelData.name
                            color: modelData.path === filmController.currentFilePath
                                   ? "#cdd6f4" : "#9399b2"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }

                        TapHandler {
                            onTapped: filmController.openCacheFile(modelData.path)
                        }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }
        }

        // ── Video area ────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#181825"
            radius: 6

            VideoOutput {
                id: filmOut
                anchors.fill: parent
                anchors.margins: 2
                fillMode: VideoOutput.PreserveAspectFit
                Component.onCompleted: filmController.setVideoSink(filmOut.videoSink)
            }

            Label {
                anchors.centerIn: parent
                visible: !filmController.hasMedia && !filmController.isDownloading
                text: filmController.ytdlpAvailable
                      ? qsTr("Paste a YouTube URL and click Download")
                      : qsTr("yt-dlp not available — rebuild to include it")
                color: "#6c7086"
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                width: parent.width * 0.7
            }
        }

        // ── Seek bar ──────────────────────────────────────────────────────────
        RowLayout {
            visible: filmController.hasMedia
            spacing: 8

            Label {
                text: formatTime(filmController.position)
                color: "#6c7086"
                font.pixelSize: 11
                font.family: "Courier New"
            }

            Slider {
                id: seekSlider
                Layout.fillWidth: true
                from: 0
                to: filmController.duration > 0 ? filmController.duration : 1
                enabled: filmController.duration > 0

                Binding on value {
                    when: !seekSlider.pressed
                    value: filmController.position
                }
                onPressedChanged: {
                    if (pressed)
                        filmController.beginScrub()
                    else
                        filmController.endScrub()
                }
                onValueChanged: {
                    if (pressed)
                        filmController.seekTo(Math.round(value))
                }

                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: seekSlider.availableWidth
                    height: 4; radius: 2
                    color: "#313244"
                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height; radius: 2
                        color: "#89b4fa"
                    }
                }
                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition * seekSlider.availableWidth - width / 2
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: 12; height: 12; radius: 6
                    color: "#89b4fa"
                }
            }

            Label {
                text: formatTime(filmController.duration)
                color: "#6c7086"
                font.pixelSize: 11
                font.family: "Courier New"
            }
        }

        // ── Playback controls + pipeline stats ────────────────────────────────
        RowLayout {
            spacing: 8

            // Rewind
            Button {
                text: qsTr("⏪ 10s")
                enabled: filmController.hasMedia
                onClicked: filmController.seekBack()
                contentItem: Text {
                    text: parent.text; color: parent.enabled ? "#cdd6f4" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.enabled ? (parent.pressed ? "#585b70" : "#45475a") : "#313244"
                    radius: 6
                }
            }

            // Play / Pause
            Button {
                text: filmController.isPlaying ? qsTr("⏸ Pause") : qsTr("▶ Play")
                enabled: filmController.hasMedia
                onClicked: filmController.isPlaying ? filmController.pause() : filmController.play()
                contentItem: Text {
                    text: parent.text; color: parent.enabled ? "#1e1e2e" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.enabled ? (parent.pressed ? "#a6e3a1" : "#40a02b") : "#313244"
                    radius: 6
                }
            }

            // Stop
            Button {
                text: qsTr("⏹ Stop")
                enabled: filmController.hasMedia
                onClicked: filmController.stop()
                contentItem: Text {
                    text: parent.text; color: parent.enabled ? "#1e1e2e" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.enabled ? (parent.pressed ? "#f38ba8" : "#e64553") : "#313244"
                    radius: 6
                }
            }

            // Annotate (pose estimation on the current paused frame)
            Button {
                text: filmController.isAnnotating ? qsTr("Annotating…") : qsTr("Annotate")
                visible: filmController.poseAvailable
                enabled: filmController.hasMedia && !filmController.isPlaying
                         && !filmController.isAnnotating
                onClicked: filmController.annotate()
                contentItem: Text {
                    text: parent.text; color: parent.enabled ? "#1e1e2e" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.enabled ? (parent.pressed ? "#f9e2af" : "#fab387") : "#313244"
                    radius: 6
                }
            }

            // Fast-forward
            Button {
                text: qsTr("10s ⏩")
                enabled: filmController.hasMedia
                onClicked: filmController.seekForward()
                contentItem: Text {
                    text: parent.text; color: parent.enabled ? "#cdd6f4" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.enabled ? (parent.pressed ? "#585b70" : "#45475a") : "#313244"
                    radius: 6
                }
            }

            Item { Layout.fillWidth: true }

            // ── Pipeline stats ────────────────────────────────────────────────
            Label {
                visible: filmController.preprocessAvgMs > 0
                text: "Pre: " + filmController.preprocessAvgMs.toFixed(1) + " ms"
                color: "#6c7086"; font.pixelSize: 12; font.family: "Courier New"
            }

            Label {
                visible: filmController.poseFps > 0
                text: "Pose: " + filmController.poseAvgMs.toFixed(1) + " ms  "
                    + filmController.poseFps.toFixed(1) + " fps"
                color: "#6c7086"; font.pixelSize: 12; font.family: "Courier New"
            }

            // ── Pose model selector (Lightning / Thunder) ────────────────────
            Row {
                visible: filmController.moveNetThunderAvailable
                spacing: 0

                Rectangle {
                    height: 20; width: lLabel.implicitWidth + 10
                    topLeftRadius: 4; bottomLeftRadius: 4
                    topRightRadius: 0; bottomRightRadius: 0
                    color: filmController.moveNetModel === 0 ? "#cba6f7" : "#313244"
                    Text {
                        id: lLabel; anchors.centerIn: parent
                        text: qsTr("Lightning")
                        color: filmController.moveNetModel === 0 ? "#1e1e2e" : "#cdd6f4"
                        font.pixelSize: 11; font.bold: filmController.moveNetModel === 0
                    }
                    TapHandler { onTapped: filmController.selectMoveNetModel(0) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                Rectangle {
                    visible: filmController.moveNetThunderAvailable
                    height: 20; width: tLabel.implicitWidth + 10
                    topRightRadius: 4; bottomRightRadius: 4
                    color: filmController.moveNetModel === 1 ? "#cba6f7" : "#313244"
                    Text {
                        id: tLabel; anchors.centerIn: parent
                        text: qsTr("Thunder")
                        color: filmController.moveNetModel === 1 ? "#1e1e2e" : "#cdd6f4"
                        font.pixelSize: 11; font.bold: filmController.moveNetModel === 1
                    }
                    TapHandler { onTapped: filmController.selectMoveNetModel(1) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            // ── ORT backend badge ─────────────────────────────────────────────
            Rectangle {
                visible: filmController.poseBackendLabel !== "" || filmController.poseFps > 0
                width: poseText.implicitWidth + 10; height: 20; radius: 4
                color: filmController.poseBackendLabel !== "" ? "#a6e3a1" : "#6c7086"
                HoverHandler { id: poseHover }
                ToolTip.visible: poseHover.hovered
                ToolTip.text: "MoveNet ORT: " + (filmController.poseBackendLabel || "CPU")
                ToolTip.delay: 500
                Text {
                    id: poseText; anchors.centerIn: parent
                    text: filmController.poseBackendLabel || "CPU"
                    color: "#1e1e2e"; font.pixelSize: 11; font.bold: true
                }
            }
        }
    }

    function formatTime(ms) {
        if (ms <= 0) return "0:00"
        const s = Math.floor(ms / 1000)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
}
