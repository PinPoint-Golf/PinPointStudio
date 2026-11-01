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
import QtQuick3D.Helpers
import PinPointStudio

Item {
    id: capturePage

    // The first (and in practice only) active IMU instance.
    // ImuVizView and ArmVizView are persistent; only this binding changes.
    property QtObject firstInst: imuManager.instances.length > 0 ? imuManager.instances[0] : null

    // ⚠ WHAT THE CUBE IS BOUND TO IS NOT THE INSTANCE FOR EVERY DEVICE KIND.
    // ImuVizView resolves quat*/accel* by NAME through the metaobject, and an
    // HmInstance has none of them — the wG3's two units each carry their own set
    // (hm_instance.h), because one peripheral holds two sensors that are supposed
    // to disagree. Binding the instance itself produced three "Unable to assign
    // [undefined] to double" warnings per connect, and this page is inside the
    // shell's StackLayout, so its bindings evaluate whether or not anyone ever
    // navigates here. The lower arm is the arbitrary-but-fixed choice for a
    // single cube; ImusPanel is where both units are shown side by side.
    // `unitLowerArm` is undefined on a Witmotion, so that falls through unchanged.
    property QtObject vizController: capturePage.firstInst
                                     && capturePage.firstInst.unitLowerArm
                                        ? capturePage.firstInst.unitLowerArm
                                        : capturePage.firstInst

    // Clear log and backfill any entries already emitted before Connections wired.
    onFirstInstChanged: {
        imuLogModel.clear()
        if (firstInst) {
            var entries = firstInst.logEntries
            for (var i = 0; i < entries.length; ++i)
                imuLogModel.append({ "entry": entries[i] })
            imuLogView.positionViewAtEnd()
        }
    }

    // Wire log entries from the active instance.
    Connections {
        target: capturePage.firstInst
        function onLogEntryAdded(entry) {
            imuLogModel.append({ "entry": entry })
            imuLogView.positionViewAtEnd()
        }
    }

    ListModel { id: imuLogModel }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap(16)
        spacing: Theme.gap(12)

        // ── Header row: title + IMU selector chips ────────────────────────────
        RowLayout {
            spacing: Theme.gap(12)

            Label {
                text: qsTr("Motion sensor")
                color: Theme.colorText
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzHeading
                font.weight: Font.Normal
            }

            Item { Layout.fillWidth: true }

            // One toggle chip per discovered IMU — same pattern as VideoPage camera chips.
            Repeater {
                model: imuManager.imuList
                delegate: PpChoiceChip {
                    readonly property bool isConnecting: modelData.connecting
                    readonly property bool isConnected:  modelData.connected

                    // Drop devices absent from the latest scan (powered off / out
                    // of range). A still-selected device stays present, so a
                    // connected IMU that stopped advertising is never hidden.
                    visible: modelData.present
                    // Solid once connected; picked but not yet connected is the accent wash.
                    style:    "solidFlat"
                    label:    modelData.alias || modelData.description
                    selected: modelData.selected && isConnected
                    pending:  modelData.selected && !isConnected
                    onClicked: imuManager.setSelected(modelData.index, !modelData.selected)

                    // Pulsing ring while connecting
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color:  "transparent"
                        border.width: 2
                        border.color: Theme.colorAccent
                        visible: isConnecting
                        SequentialAnimation on opacity {
                            running: isConnecting
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.2; duration: 600; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }

            // Scan button
            PpButton {
                id: scanChip
                // Bound to the manager's authoritative scan state — NOT a local
                // 30 s timer. The discovery window is 90 s with HackMotion
                // enabled, and a local clock that ran out early re-offered a
                // Scan tap that the enumerator's re-entry guard silently
                // swallowed for the rest of the real window.
                readonly property bool scanning: imuManager.imuScanActive

                style: "washPlain"; size: "compact"
                implicitHeight: Theme.sp(24)
                active: scanning
                sizeLabel: qsTr("Scanning…")
                label: scanChip.scanning ? qsTr("Scanning…") : qsTr("Scan")
                onClicked: imuManager.rescanImu()
            }
        }

        // ── Main content area ─────────────────────────────────────────────────
        //
        // ImuVizView (View3D) and ArmVizView (View3D) live here as persistent
        // items whose `controller` property is rebound when the selection changes.
        // They are NEVER destroyed on deselect — only the binding changes to null.
        // This prevents the Qt Quick 3D render-thread heap corruption that occurred
        // when these were inside the Repeater delegate (destroyed on every deselect).
        //
        Item {
            Layout.fillWidth:  true
            Layout.fillHeight: true

            // Persistent view area — visible whenever at least one IMU is selected.
            ColumnLayout {
                anchors.fill: parent
                visible: imuManager.instances.length > 0
                spacing: Theme.gap(6)

                // ── Tab bar ───────────────────────────────────────────────────
                TabBar {
                    id: imuTabBar
                    Layout.fillWidth: true
                    background: Rectangle {
                        color: Theme.colorSurface
                        radius: Theme.radius
                        border.width: 1
                        border.color: Theme.colorBorderMid
                    }

                    PpTabButton { text: qsTr("Viz") }
                    PpTabButton { text: qsTr("Log") }
                    PpTabButton { text: qsTr("Arm") }
                    PpTabButton { text: qsTr("Body") }
                }

                // ── Persistent 3D views ───────────────────────────────────────
                StackLayout {
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                    currentIndex: imuTabBar.currentIndex

                    // Viz tab — persistent View3D; controller rebinds, view never destroyed.
                    ImuVizView {
                        controller: capturePage.vizController
                    }

                    // Log tab
                    Rectangle {
                        color:        Theme.colorSurface
                        radius:       Theme.radius
                        border.width: 1
                        border.color: Theme.colorBorderMid

                        ListView {
                            id: imuLogView
                            anchors.fill:    parent
                            anchors.margins: Theme.gap(8)
                            clip:  true
                            model: imuLogModel
                            spacing: 1

                            delegate: Text {
                                required property string entry
                                width:      imuLogView.width
                                text:       entry
                                color:      Theme.colorText
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzLabel
                                wrapMode:   Text.NoWrap
                            }
                        }
                    }

                    // Arm viz tab — persistent View3D; reads imuManager.instances directly.
                    ArmVizView {}

                    // Body viz tab — persistent View3D; Y Bot full-body model.
                    // poseSource wired later when pose adapter is plumbed here.
                    BodyVizView {}
                }

                // ── Per-instance controls (Repeater, no View3D) ───────────────
                // One row of controls per selected IMU. The Repeater only creates
                // lightweight Items — no View3D, no QSG resources at stake.
                Repeater {
                    model: imuManager.instances
                    delegate: RowLayout {
                        property QtObject ctrl: modelData

                        Layout.fillWidth: true
                        spacing: Theme.gap(8)

                        // Status dot
                        Rectangle {
                            width:  Theme.sp(6)
                            height: Theme.sp(6)
                            radius: Theme.sp(3)
                            color: ctrl && ctrl.imuConnected ? Theme.colorGood
                                 : ctrl && ctrl.busy         ? Theme.colorAccent
                                 :                              Theme.colorText3
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        }

                        Label {
                            text: {
                                if (!ctrl) return ""
                                if (ctrl.imuConnected && ctrl.dataRateHz > 0)
                                    return ctrl.stateLabel + " · " + ctrl.dataRateHz.toFixed(1) + " Hz"
                                return ctrl.stateLabel
                            }
                            color: ctrl && ctrl.imuConnected ? Theme.colorGood
                                 : ctrl && ctrl.busy         ? Theme.colorWarn
                                 :                              Theme.colorText3
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        }

                        Item { Layout.fillWidth: true }

                        // Battery badge
                        Rectangle {
                            visible: ctrl && ctrl.imuConnected && ctrl.batteryPercent >= 0
                            radius: Theme.radius
                            color: ctrl && ctrl.batteryPercent > 60 ? Theme.colorGoodLight
                                 : Theme.colorWarnLight
                            border.width: 1
                            border.color: ctrl && ctrl.batteryPercent > 60
                                          ? Qt.alpha(Theme.colorGood, 0.25)
                                          : Qt.alpha(Theme.colorWarn, 0.25)
                            implicitWidth:  batLabel.implicitWidth + Theme.sp(10)
                            implicitHeight: batLabel.implicitHeight + Theme.sp(4)

                            Label {
                                id: batLabel
                                anchors.centerIn: parent
                                text: ctrl ? ("BAT: " + ctrl.batteryPercent + "%") : ""
                                color: ctrl && ctrl.batteryPercent > 60 ? Theme.colorGood : Theme.colorWarn
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                            }
                        }

                        // Rate selector
                        PpComboBox {
                            id: rateCombo
                            enabled: ctrl && ctrl.imuConnected
                            model: [10, 50, 100, 200]
                            displaySuffix: " Hz"
                            currentIndex: {
                                if (!ctrl) return 2
                                var idx = model.indexOf(ctrl.outputRateHz)
                                return idx >= 0 ? idx : 2
                            }
                            onActivated: if (ctrl) ctrl.setOutputRateHz(model[currentIndex])
                            implicitWidth: Theme.sp(90)
                        }

                        // Zero button
                        PpButton {
                            id: zeroBtn
                            style: "labOutline"; size: "lab"
                            label: qsTr("Zero")
                            enabled: ctrl && ctrl.imuConnected
                            onClicked: if (ctrl) ctrl.zeroOrientation()
                        }

                        // Save Log button
                        PpButton {
                            id: saveLogBtn
                            style: "labOutline"; size: "lab"
                            label: qsTr("Save Log")
                            onClicked: if (ctrl) ctrl.saveLog()
                        }
                    }
                }
            }

            // Placeholder when no IMU is selected.
            Rectangle {
                anchors.fill: parent
                visible: imuManager.instances.length === 0
                color:        Theme.colorBg2
                radius:       Theme.radius
                border.width: 1
                border.color: Theme.colorBorderMid

                Label {
                    anchors.centerIn: parent
                    text:  qsTr("Select a motion sensor above")
                    color: Theme.colorText3
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                }
            }
        }

    }
}
