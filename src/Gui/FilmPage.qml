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
import QtMultimedia

Item {

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp(16)
        spacing: Theme.sp(12)

        Label {
                    text: qsTr("Film")
            color: Theme.colorText
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSzHeading
            font.weight: Font.Normal
        }

        // ── URL + download controls ───────────────────────────────────────────
        RowLayout {
            spacing: Theme.sp(8)

            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: qsTr("Paste YouTube URL…")
                color: Theme.colorText
                placeholderTextColor: Theme.colorText3
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                leftPadding: 8; rightPadding: 8
                background: Rectangle {
                    color: Theme.colorSurface
                    radius: Theme.radius
                    border.width: 1
                    border.color: urlField.activeFocus ? Theme.colorAccent : Theme.colorBorderMid
                }
                Keys.onReturnPressed: downloadBtn.clicked()
                Keys.onEnterPressed:  downloadBtn.clicked()
            }

            ComboBox {
                id: browserBox
                model: ["chrome", "firefox", "safari", "edge", "brave"]
                currentIndex: Qt.platform.os === "linux" ? 4 : 0
                implicitWidth: 90
                contentItem: Text {
                    leftPadding: 8
                    text: browserBox.displayText
                    color: Theme.colorText
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: Theme.colorSurface
                    radius: Theme.radius
                    border.width: 1
                    border.color: Theme.colorBorderMid
                }
                popup.background: Rectangle {
                    color: Theme.colorSurface
                    radius: Theme.radius
                    border.width: 1
                    border.color: Theme.colorBorderMid
                }
                delegate: ItemDelegate {
                    width: browserBox.width
                    contentItem: Text {
                        text: modelData
                        color: Theme.colorText
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: hovered ? Theme.colorBg3 : Theme.colorSurface
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
                    color: downloadBtn.enabled
                           ? (filmController.isDownloading ? Theme.colorWarn : Theme.colorBg)
                           : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: {
                        if (!downloadBtn.enabled) return Theme.colorBg3
                        if (filmController.isDownloading) return Theme.colorWarnLight
                        return downloadBtn.pressed ? Qt.darker(Theme.colorAccent, 1.1) : Theme.colorAccent
                    }
                    border.width: filmController.isDownloading ? 1 : 0
                    border.color: Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g, Theme.colorWarn.b, 0.5)
                    radius: Theme.radius
                }
            }
        }

        // ── Download progress ─────────────────────────────────────────────────
        ColumnLayout {
            visible: filmController.isDownloading || filmController.downloadStatus.length > 0
            spacing: Theme.sp(4)

            ProgressBar {
                Layout.fillWidth: true
                value: filmController.downloadProgress
                background: Rectangle {
                    color: Theme.colorBg3
                    radius: Theme.radius - 2
                    implicitHeight: 6
                }
                contentItem: Item {
                    Rectangle {
                        width: parent.width * filmController.downloadProgress
                        height: parent.height
                        radius: Theme.radius - 2
                        color: Theme.colorAccent
                    }
                }
            }

            Label {
                text: filmController.downloadStatus
                color: filmController.downloadStatus.startsWith("Download failed") ||
                       filmController.downloadStatus.startsWith("Playback error") ||
                       filmController.downloadStatus.startsWith("Could not")
                       ? Theme.colorWarn : Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // ── Film cache ────────────────────────────────────────────────────────
        ColumnLayout {
            visible: filmController.cacheEntries.length > 0
            Layout.fillWidth: true
            spacing: Theme.sp(4)

            RowLayout {
                spacing: Theme.sp(6)
                Label {
                    text: qsTr("CACHED")
                    color: Theme.colorText3
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.weight: Font.Normal
                    font.letterSpacing: Theme.trackingMicro
                    font.capitalization: Font.AllUppercase
                }
                Label {
                    text: filmController.cacheEntries.length + " video" +
                          (filmController.cacheEntries.length !== 1 ? "s" : "")
                    color: Theme.colorText3
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                }
            }

            ListView {
                id: cacheList
                Layout.fillWidth: true
                implicitHeight: 116
                orientation: Qt.Horizontal
                spacing: Theme.sp(8)
                clip: true
                model: filmController.cacheEntries

                delegate: Item {
                    id: cacheCard
                    width: Theme.sp(164)
                    height: Theme.sp(114)

                    HoverHandler { id: cardHover }

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: modelData.path === filmController.currentFilePath
                               ? Theme.colorAccentLight
                               : cardHover.hovered ? Theme.colorBg3 : Theme.colorBg2
                        border.color: modelData.path === filmController.currentFilePath
                                      ? Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.4)
                                      : Theme.colorBorderMid
                        border.width: 1

                        // Thumbnail area
                        Rectangle {
                            id: thumbRect
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: Theme.sp(1)
                            height: Theme.sp(92)
                            radius: Theme.radius - 1
                            color: Theme.colorBg
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
                                font.pixelSize: Theme.fontSzData
                                color: Theme.colorText3
                            }

                            // Duration badge
                            Rectangle {
                                visible: modelData.durationMs > 0
                                anchors.bottom: parent.bottom
                                anchors.right: parent.right
                                anchors.margins: Theme.sp(4)
                                width: durLabel.implicitWidth + Theme.sp(8)
                                height: Theme.sp(16)
                                radius: Theme.radius - 2
                                color: Qt.rgba(0, 0, 0, 0.6)

                                Label {
                                    id: durLabel
                                    anchors.centerIn: parent
                                    text: formatTime(modelData.durationMs)
                                    color: Theme.colorText
                                    font.family: Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                }
                            }

                            // Delete button
                            Rectangle {
                                visible: cardHover.hovered
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: Theme.sp(4)
                                width: Theme.sp(18); height: Theme.sp(18); radius: Theme.sp(9)
                                color: Theme.colorWarn

                                Text {
                                    anchors.centerIn: parent
                                    text: "×"
                                    color: Theme.colorBg
                                    font.pixelSize: Theme.fontSzBody
                                    font.weight: Font.Normal
                                }

                                TapHandler { onTapped: filmController.deleteCacheFile(modelData.path) }
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
                                   ? Theme.colorText : Theme.colorText2
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontSzMicro
                            elide: Text.ElideRight
                        }

                        TapHandler { onTapped: filmController.openCacheFile(modelData.path) }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }
        }

        // ── Video area ────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.colorBg2
            radius: Theme.radius
            border.width: 1
            border.color: Theme.colorBorderMid

            VideoOutput {
                id: filmOut
                anchors.fill: parent
                anchors.margins: Theme.sp(2)
                fillMode: VideoOutput.PreserveAspectFit
                Component.onCompleted: filmController.setVideoSink(filmOut.videoSink)
            }

            Label {
                anchors.centerIn: parent
                visible: !filmController.hasMedia && !filmController.isDownloading
                text: filmController.ytdlpAvailable
                      ? qsTr("Paste a YouTube URL and click Download")
                      : qsTr("yt-dlp not available — rebuild to include it")
                color: Theme.colorText3
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                width: parent.width * 0.7
            }
        }

        // ── Seek bar ──────────────────────────────────────────────────────────
        RowLayout {
            visible: filmController.hasMedia
            spacing: Theme.sp(8)

            Label {
                text: formatTime(filmController.position)
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
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
                    if (pressed) filmController.beginScrub()
                    else         filmController.endScrub()
                }
                onValueChanged: {
                    if (pressed)
                        filmController.seekTo(Math.round(value))
                }

                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: seekSlider.availableWidth
                    height: Theme.sp(4); radius: Theme.sp(2)
                    color: Theme.colorBg3
                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height; radius: Theme.sp(2)
                        color: Theme.colorAccent
                    }
                }
                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition * seekSlider.availableWidth - width / 2
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: Theme.sp(12); height: Theme.sp(12); radius: Theme.sp(6)
                    color: Theme.colorAccent
                }
            }

            Label {
                text: formatTime(filmController.duration)
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
            }
        }

        // ── Playback controls + pipeline stats ────────────────────────────────
        RowLayout {
            spacing: Theme.sp(8)

            Button {
                text: qsTr("⏪ 10s")
                enabled: filmController.hasMedia
                onClicked: filmController.seekBack()
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? Theme.colorText2 : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    border.width: 1
                    border.color: parent.enabled ? Theme.colorBorderStrong : Theme.colorBorderMid
                    radius: Theme.radius
                }
            }

            Button {
                text: filmController.isPlaying ? qsTr("⏸ Pause") : qsTr("▶ Play")
                enabled: filmController.hasMedia
                onClicked: filmController.isPlaying ? filmController.pause() : filmController.play()
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? Theme.colorBg : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.enabled
                           ? (parent.pressed ? Qt.darker(Theme.colorAccent, 1.1) : Theme.colorAccent)
                           : Theme.colorBg3
                    radius: Theme.radius
                }
            }

            Button {
                text: qsTr("⏹ Stop")
                enabled: filmController.hasMedia
                onClicked: filmController.stop()
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? Theme.colorWarn : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.enabled ? Theme.colorWarnLight : Theme.colorBg3
                    border.width: parent.enabled ? 1 : 0
                    border.color: Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g, Theme.colorWarn.b, 0.5)
                    radius: Theme.radius
                }
            }

            Button {
                text: filmController.isAnnotating ? qsTr("Annotating…") : qsTr("Annotate")
                visible: filmController.poseAvailable
                enabled: filmController.hasMedia && !filmController.isPlaying
                         && !filmController.isAnnotating
                onClicked: filmController.annotate()
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? Theme.colorText2 : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    border.width: 1
                    border.color: parent.enabled ? Theme.colorBorderStrong : Theme.colorBorderMid
                    radius: Theme.radius
                }
            }

            Button {
                text: qsTr("10s ⏩")
                enabled: filmController.hasMedia
                onClicked: filmController.seekForward()
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? Theme.colorText2 : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    border.width: 1
                    border.color: parent.enabled ? Theme.colorBorderStrong : Theme.colorBorderMid
                    radius: Theme.radius
                }
            }

            Item { Layout.fillWidth: true }

            // ── Pipeline stats ────────────────────────────────────────────────
            Label {
                visible: filmController.preprocessAvgMs > 0
                text: "Pre: " + filmController.preprocessAvgMs.toFixed(1) + " ms"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzBody2
            }

            Label {
                visible: filmController.poseFps > 0
                text: "Pose: " + filmController.poseAvgMs.toFixed(1) + " ms  "
                    + filmController.poseFps.toFixed(1) + " fps"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzBody2
            }

            // ── Pose model selector (Lightning / Thunder) ────────────────────
            Row {
                visible: filmController.moveNetThunderAvailable
                spacing: 0

                Rectangle {
                    height: Theme.sp(20); width: lLabel.implicitWidth + Theme.sp(10)
                    topLeftRadius: Theme.radius; bottomLeftRadius: Theme.radius
                    topRightRadius: 0; bottomRightRadius: 0
                    color: filmController.moveNetModel === 0 ? Theme.colorAccentMid : Theme.colorSurface
                    border.width: 1
                    border.color: filmController.moveNetModel === 0
                                  ? Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.4)
                                  : Theme.colorBorderMid
                    Text {
                        id: lLabel; anchors.centerIn: parent
                        text: qsTr("Lightning")
                        color: filmController.moveNetModel === 0 ? Theme.colorAccent : Theme.colorText2
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzLabel
                        font.weight: Font.Normal
                    }
                    TapHandler { onTapped: filmController.selectMoveNetModel(0) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                Rectangle {
                    visible: filmController.moveNetThunderAvailable
                    height: Theme.sp(20); width: tLabel.implicitWidth + Theme.sp(10)
                    topRightRadius: Theme.radius; bottomRightRadius: Theme.radius
                    topLeftRadius: 0; bottomLeftRadius: 0
                    color: filmController.moveNetModel === 1 ? Theme.colorAccentMid : Theme.colorSurface
                    border.width: 1
                    border.color: filmController.moveNetModel === 1
                                  ? Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.4)
                                  : Theme.colorBorderMid
                    Text {
                        id: tLabel; anchors.centerIn: parent
                        text: qsTr("Thunder")
                        color: filmController.moveNetModel === 1 ? Theme.colorAccent : Theme.colorText2
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzLabel
                        font.weight: Font.Normal
                    }
                    TapHandler { onTapped: filmController.selectMoveNetModel(1) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            // ── ORT backend badge ─────────────────────────────────────────────
            Rectangle {
                visible: filmController.poseBackendLabel !== "" || filmController.poseFps > 0
                width: poseText.implicitWidth + 10; height: 20; radius: Theme.radius
                color: filmController.poseBackendLabel !== "" ? Theme.colorGoodLight : Theme.colorSurface
                border.width: 1
                border.color: filmController.poseBackendLabel !== ""
                              ? Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.3)
                              : Theme.colorBorderMid
                HoverHandler { id: poseHover }
                ToolTip.visible: poseHover.hovered
                ToolTip.text: "MoveNet ORT: " + (filmController.poseBackendLabel || "CPU")
                ToolTip.delay: 500
                Text {
                    id: poseText; anchors.centerIn: parent
                    text: filmController.poseBackendLabel || "CPU"
                    color: filmController.poseBackendLabel !== "" ? Theme.colorGood : Theme.colorText3
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzLabel
                    font.weight: Font.Normal
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
