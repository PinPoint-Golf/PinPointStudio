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

// Session setup — Confirm tracking (the arm group's check step; the retired ScreenSessionWizard.qml Panel 6,
// l.1662–1822), strings, colours and layout unchanged.
//
// The ArmVizView is THIS page's: it exists only while Check is current (R2 — CK2, and with the
// Calibrate page's guide view, at most one View3D under the shell: L2). The live wrist computer
// runs only while the page is active, switched in enter() and leave() — NOT by a permanent
// Binding, which forced it false on every step change and so overrode anyone else who had turned
// it on (F6, CK1).
//
// Recalibrate drops the arm outcome and jumps back to Calibrate with Calibrate and Check pending
// (N15); the Calibrate page then begins a fresh run, because nothing valid is recorded.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    fullBleed: true

    // The live wrist computer (LiveWristAngles), through the context — never the app's context
    // property directly (lint W4: hardware through ctx). Named as the context object is, so the
    // member reads below are the ones lint W5 checks against live_wrist_angles.h.
    readonly property var liveWrist: page.ctx.liveWrist

    function enter(direction) { if (liveWrist) liveWrist.active = true }
    function leave(reason)    { if (liveWrist) liveWrist.active = false }

    function recalibrate() {
        page.draft.clearOutcome("arm")
        page.flow.goTo("calibrateArm", true)
    }

    ColumnLayout {
        anchors.fill:    parent
        anchors.margins: Theme.gap(8)
        spacing:         Theme.gap(12)

        Column {
            Layout.fillWidth:    true
            Layout.maximumWidth: Theme.contentWidth(page.width)
            Layout.topMargin:    Theme.gap(32)
            spacing:             Theme.gap(8)

            Text {
                text:               page.stepLabel
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:              Theme.colorText3
            }

            PpDisplayText {
                width:          parent.width
                text:           qsTr("Check your sensor")
                pixelSize:      Math.min(Theme.sp(22), Theme.fontSzDisplay)
                wrapMode:       Text.WordWrap
            }

            Text {
                width:          parent.width
                text:           qsTr("Move your arm slowly in all directions. The 3D model should track your movement precisely. If it doesn't, go back and recalibrate.")
                font.family:    Theme.fontBody
                font.weight:    Theme.fontBodyWeight
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText2
                lineHeight:     1.65
                wrapMode:       Text.WordWrap
            }
        }

        // Live tracking — the arm avatar is driven entirely by the calibrated sensors
        // (anatQuat), resolved per role inside ArmVizView. Move your arm and confirm the model
        // follows.
        ArmVizView {
            id: confirmArmViz
            Layout.fillWidth:  true
            Layout.fillHeight: true

            // ── Live lead-wrist metrics (vs neutral) ──────────────────
            // A CHILD of the ArmVizView instance — ArmVizView.qml is not modified. Read-only
            // check; '—' until the sensors are calibrated (roll needs the optional upper arm).
            Rectangle {
                visible: page.liveWrist !== null
                anchors { top: parent.top; right: parent.right; margins: Theme.gap(10) }
                width:   metricsCol.width  + Theme.sp(20)
                height:  metricsCol.height + Theme.sp(14)
                radius:  Theme.radius
                color:   Qt.alpha(Theme.colorBg2, 0.85)
                border.width: 1
                border.color: Theme.colorBorderMid

                Column {
                    id: metricsCol
                    anchors.centerIn: parent
                    spacing: Theme.gap(6)

                    Row {
                        spacing: Theme.gap(10)
                        Text {
                            text: qsTr("Bow / cup"); width: Theme.sp(54)
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                            color: Theme.colorText3
                        }
                        Text {
                            text: liveWrist ? liveWrist.bowLabel : ""
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                            color: liveWrist && liveWrist.bowValid ? Theme.colorText : Theme.colorText3
                        }
                    }
                    Row {
                        spacing: Theme.gap(10)
                        Text {
                            text: qsTr("Hinge"); width: Theme.sp(54)
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                            color: Theme.colorText3
                        }
                        Text {
                            text: liveWrist ? liveWrist.hingeLabel : ""
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                            color: liveWrist && liveWrist.bowValid ? Theme.colorText : Theme.colorText3
                        }
                    }
                    Row {
                        spacing: Theme.gap(10)
                        Text {
                            text: liveWrist ? liveWrist.rollTitle : ""; width: Theme.sp(54)
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                            color: Theme.colorText3
                        }
                        Text {
                            text: liveWrist ? liveWrist.rollLabel : ""
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                            color: liveWrist && liveWrist.rollValid ? Theme.colorText : Theme.colorText3
                        }
                    }
                }
            }
        }

        // Recalibrate affordance — returns to the Calibrate step for a fresh capture, used when
        // tracking looks wrong. Calibration itself happens there, not on this screen.
        Row {
            Layout.fillWidth:    true
            Layout.bottomMargin: Theme.gap(8)
            spacing:             Theme.gap(6)

            Text {
                text:           qsTr("Not tracking your movement?")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText3
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text:           qsTr("Recalibrate")
                font.family:    Theme.fontBody
                font.weight:    Theme.fontBodyWeight
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorAccent
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    anchors.fill: parent
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    page.recalibrate()
                }
            }
        }
    }
}
