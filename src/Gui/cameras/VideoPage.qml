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
    id: videoPage

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap(16)
        spacing: Theme.gap(12)

        // ── Header row: title + camera selector ───────────────────────────────
        RowLayout {
            spacing: Theme.gap(12)

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
                delegate: PpChoiceChip {
                    readonly property bool chipLocked: cameraManager.isRecording || shotProcessor.isReplaying
                    style:    "solidLift"
                    label:    modelData.alias || modelData.description
                    selected: modelData.selected
                    dimmed:   chipLocked
                    enabled:  !chipLocked
                    onClicked: cameraManager.setSelected(modelData.index, !modelData.selected)
                }
            }
        }

        // ── Camera views ──────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.gap(8)

            Repeater {
                model: cameraManager.instances
                delegate: PpCameraFrame {
                    Layout.fillHeight: true
                    // Hug the video: width follows the (cropped) frame aspect
                    // at full row height — a narrow crop frees horizontal
                    // space instead of letterboxing inside an equal share.
                    Layout.fillWidth: false
                    Layout.preferredWidth: height * videoAspect
                    instance: modelData
                    displayName: modelData.deviceAlias !== "" ? modelData.deviceAlias
                                                              : modelData.deviceDescription
                    roiEditable: true
                }
            }

            // Tiles pack left; the remainder is reserved for future
            // charts/graphs. Hidden when the placeholder below fills the row.
            Item {
                visible: cameraManager.instances.length > 0
                Layout.fillWidth: true
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
            spacing: Theme.gap(8)

            PpButton {
                id: startButton
                style: "labPrimary"; size: "lab"
                label: qsTr("Start ●")
                enabled: !cameraManager.isRecording && cameraManager.anySelected
                onClicked: cameraManager.startAll()
            }

            PpButton {
                id: stopButton
                style: "labStop"; size: "lab"
                label: qsTr("Stop ■")
                enabled: cameraManager.isRecording
                onClicked: cameraManager.stopAll()
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
            spacing: Theme.gap(8)

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
