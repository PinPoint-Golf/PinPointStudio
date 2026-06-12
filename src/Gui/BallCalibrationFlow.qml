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

// Ball-detection calibration flow (docs/design/ball_detection_calibration.md
// §5/§8). Controls-only — the host supplies the live camera view (PpCameraFrame
// with the hitting-area overlay) beside or above this; the flow drives the
// BallCalibrationController state machine and renders its phase, instruction,
// capture progress and robustness meter. Shape-compatible with
// ImuCalibrationFlow / CameraCalibrationFlow hosting (layoutMode / completed()
// / cancelled()).
//
// Hosts: Settings → Cameras row section (§8.1) and the session wizard's
// cameras step (§8.2).

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: flow

    // The BallCalibrationController for one camera —
    // cameraManager.ballCalibrationFor(instance). Null = inert.
    property QtObject controller: null
    property string   layoutMode: "compact"   // shape parity (single layout today)

    signal completed()
    signal cancelled()

    function begin() { if (controller) controller.begin() }
    function reset() { if (controller) controller.cancel() }

    readonly property string _phase: controller ? controller.phase : "idle"
    readonly property bool   _running: controller ? controller.busy : false

    implicitHeight: col.implicitHeight

    Connections {
        target: flow.controller
        function onCompleted() { flow.completed() }
        function onCancelled() { flow.cancelled() }
    }

    ColumnLayout {
        id: col
        anchors.left:  parent.left
        anchors.right: parent.right
        spacing: Theme.sp(10)

        // ── Status row: phase chip + robustness meter ───────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(10)

            Rectangle {
                implicitWidth:  phaseLabel.implicitWidth + Theme.sp(16)
                implicitHeight: Theme.sp(20)
                radius: height / 2
                color: flow._phase === "done"   ? Theme.colorGoodLight
                     : flow._phase === "failed" ? Theme.colorWarnLight
                     : flow._running            ? Theme.colorAccentLight
                                                : Theme.colorBg3
                Text {
                    id: phaseLabel
                    anchors.centerIn: parent
                    text: flow._phase === "done"   ? qsTr("CALIBRATED")
                        : flow._phase === "failed" ? qsTr("NEEDS ATTENTION")
                        : flow._running            ? qsTr("CALIBRATING")
                                                   : qsTr("READY")
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color: flow._phase === "done"   ? Theme.colorGood
                         : flow._phase === "failed" ? Theme.colorWarn
                         : flow._running            ? Theme.colorAccent
                                                    : Theme.colorText3
                }
            }

            Item { Layout.fillWidth: true }

            // Robustness meter — visible once a threshold exists.
            RowLayout {
                spacing: Theme.sp(6)
                visible: flow.controller !== null && flow.controller.margin > 0

                Text {
                    text: qsTr("ROBUSTNESS")
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText3
                }
                Rectangle {
                    implicitWidth: Theme.sp(72); implicitHeight: Theme.sp(5)
                    radius: height / 2
                    color: Theme.colorBg3
                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width:  parent.width * (flow.controller ? flow.controller.robustness : 0)
                        height: parent.height
                        radius: height / 2
                        color: (flow.controller && flow.controller.robustness >= 0.4)
                               ? Theme.colorGood : Theme.colorWarn
                        Behavior on width { NumberAnimation { duration: Theme.durationNormal } }
                    }
                }
                Text {
                    text: flow.controller
                          ? qsTr("margin %1").arg(flow.controller.margin.toFixed(2)) : ""
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText2
                }
            }
        }

        // ── Instruction ─────────────────────────────────────────────────────
        Text {
            Layout.fillWidth: true
            visible: text !== ""
            text: flow.controller ? flow.controller.instruction : ""
            wrapMode: Text.WordWrap
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            color: flow._phase === "failed" ? Theme.colorWarn : Theme.colorText
        }

        // ── Capture progress ────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.sp(5)
            radius: height / 2
            color: Theme.colorBg3
            visible: flow._phase === "captureEmpty" || flow._phase === "captureBall"
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width:  parent.width * (flow.controller ? flow.controller.progress : 0)
                height: parent.height
                radius: height / 2
                color:  Theme.colorAccent
            }
        }

        // ── Buttons ─────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(8)

            PpButton {
                visible: flow._phase === "idle"
                primary: true
                label:   qsTr("Calibrate ball detection")
                onClicked: flow.begin()
            }
            PpButton {
                visible: flow.controller !== null && flow.controller.awaitingConfirm
                primary: true
                label:   qsTr("Continue")
                onClicked: flow.controller.confirm()
            }
            PpButton {
                visible: flow._phase === "failed"
                primary: true
                label:   qsTr("Try again")
                onClicked: flow.controller.retry()
            }
            PpButton {
                visible: flow.controller !== null && flow.controller.canAcceptMarginal
                label:   qsTr("Accept marginal")
                onClicked: flow.controller.acceptMarginal()
            }
            PpButton {
                visible: flow._phase === "done"
                label:   qsTr("Recalibrate")
                onClicked: flow.begin()
            }
            PpButton {
                visible: flow._running
                label:   qsTr("Cancel")
                onClicked: flow.controller.cancel()
            }

            Item { Layout.fillWidth: true }
        }
    }
}
