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
import PinPoint

Item {

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── Header row: title + camera selector ───────────────────────────────
        RowLayout {
            spacing: 12

            Label {
                text: "Camera"
                color: Theme.colorText
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzHeading
                font.weight: Font.Normal
            }

            Item { Layout.fillWidth: true }

            // One toggle chip per discovered camera.
            Repeater {
                model: cameraManager.cameraList
                delegate: Rectangle {
                    width: camChipLabel.implicitWidth + 16
                    height: 24
                    radius: Theme.radius
                    color: modelData.selected ? Theme.colorAccent : Theme.colorSurface
                    border.width: 1
                    border.color: modelData.selected
                                  ? Theme.colorAccent
                                  : Theme.colorBorderMid
                    opacity: cameraManager.isRecording ? 0.5 : 1.0

                    Text {
                        id: camChipLabel
                        anchors.centerIn: parent
                        text: modelData.description
                        color: modelData.selected ? Theme.colorBg : Theme.colorText2
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight: Font.Normal
                    }

                    TapHandler {
                        enabled: !cameraManager.isRecording
                        onTapped: cameraManager.setSelected(modelData.index, !modelData.selected)
                    }
                    HoverHandler {
                        enabled: !cameraManager.isRecording
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }

        // ── Camera views (side-by-side, equal width) ──────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Repeater {
                model: cameraManager.instances
                delegate: CameraView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    controller: modelData
                }
            }

            // Placeholder shown when no camera is selected.
            Rectangle {
                visible: cameraManager.instances.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.colorBg2
                radius: Theme.radius
                border.width: 1
                border.color: Theme.colorBorderMid

                Label {
                    anchors.centerIn: parent
                    text: qsTr("Select a camera above")
                    color: Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                }
            }
        }

        // ── Shared controls ───────────────────────────────────────────────────
        RowLayout {
            spacing: 8

            Button {
                id: startButton
                text: qsTr("Start ●")
                enabled: !cameraManager.isRecording && cameraManager.anySelected
                onClicked: cameraManager.startAll()
                contentItem: Text {
                    text: startButton.text
                    color: startButton.enabled ? Theme.colorBg : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: startButton.enabled
                           ? (startButton.pressed ? Qt.darker(Theme.colorAccent, 1.1) : Theme.colorAccent)
                           : Theme.colorBg3
                    radius: Theme.radius
                }
            }

            Button {
                id: stopButton
                text: qsTr("Stop ■")
                enabled: cameraManager.isRecording
                onClicked: cameraManager.stopAll()
                contentItem: Text {
                    text: stopButton.text
                    color: stopButton.enabled ? Theme.colorWarn : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: stopButton.enabled ? Theme.colorWarnLight : Theme.colorBg3
                    border.width: stopButton.enabled ? 1 : 0
                    border.color: Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g, Theme.colorWarn.b, 0.5)
                    radius: Theme.radius
                }
            }

            Rectangle {
                visible: cameraManager.isRecording
                width: 8; height: 8; radius: 4
                color: Theme.colorWarn
                SequentialAnimation on opacity {
                    running: cameraManager.isRecording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 900; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                }
            }

            Label {
                visible: cameraManager.isRecording
                text: qsTr("Recording…")
                color: Theme.colorWarn
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody
            }

            Item { Layout.fillWidth: true }
        }

        // ── Buffer status (visible while recording) ───────────────────────────
        RowLayout {
            visible: cameraManager.isRecording
            spacing: 8

            Item { Layout.fillWidth: true }

            Label {
                visible: cameraManager.bufferState !== "idle"
                text: bufferController.totalEvents + " events"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzBody2
                verticalAlignment: Text.AlignVCenter
            }

            Repeater {
                model: bufferController.sources
                delegate: Label {
                    required property var modelData
                    visible: modelData.overwritten > Math.max(0, modelData.eventsWritten - modelData.slotCount)
                    text: "⚠ " + modelData.name + " overrun"
                    color: Theme.colorWarn
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
