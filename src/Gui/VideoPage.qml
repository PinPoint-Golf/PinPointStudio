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
    id: videoPage

    // Face-on camera (perspective === 2) — drives the live body visualisation.
    readonly property QtObject faceOnController: {
        var insts = cameraManager.instances
        for (var i = 0; i < insts.length; ++i) {
            if (insts[i] && insts[i].perspective === 2) return insts[i]
        }
        return null
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp(16)
        spacing: Theme.sp(12)

        // ── Header row: title + camera selector ───────────────────────────────
        RowLayout {
            spacing: Theme.sp(12)

            Label {
                text: qsTr("Camera")
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
                    width: camChipLabel.implicitWidth + Theme.sp(16)
                    height: Theme.sp(24)
                    radius: Theme.radius
                    color: modelData.selected ? Theme.colorAccent : Theme.colorSurface
                    border.width: 1
                    border.color: modelData.selected
                                  ? Theme.colorAccent
                                  : Theme.colorBorderMid
                    readonly property bool chipLocked: cameraManager.isRecording || cameraManager.isReplaying
                    opacity: chipLocked ? 0.5 : 1.0

                    Text {
                        id: camChipLabel
                        anchors.centerIn: parent
                        text: modelData.alias || modelData.description
                        color: modelData.selected ? Theme.colorBg : Theme.colorText2
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight: Font.Normal
                    }

                    TapHandler {
                        enabled: !parent.chipLocked
                        onTapped: cameraManager.setSelected(modelData.index, !modelData.selected)
                    }
                    HoverHandler {
                        enabled: !parent.chipLocked
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }

        // ── Camera views + body visualisation (equal slots) ──────────────────
        // Each camera and the body viz occupies one equal slot in this row.
        // CameraView internally centres the frame at the correct video aspect
        // ratio within its slot so the image never stretches.
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.sp(8)

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

            // Live body visualisation — one equal slot alongside the cameras.
            // poseSource drives animation from the face-on camera's pose estimator;
            // null when no face-on camera is configured (body renders in T-pose).
            BodyVizView {
                Layout.fillWidth:  true
                Layout.fillHeight: true
                visible:      videoPage.faceOnController !== null
                poseSource:   videoPage.faceOnController
                mirroredSource: videoPage.faceOnController !== null
                              && videoPage.faceOnController.isMirrored
            }
        }

        // ── Shared controls ───────────────────────────────────────────────────
        RowLayout {
            spacing: Theme.sp(8)

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
                width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
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
            spacing: Theme.sp(8)

            Item { Layout.fillWidth: true }

            Repeater {
                model: bufferController.sources
                delegate: Label {
                    required property var modelData
                    visible: modelData.overwritten > Math.max(0, modelData.eventsWritten - modelData.slotCount)
                    text: qsTr("⚠ %1 overrun").arg(modelData.name)
                    color: Theme.colorWarn
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
