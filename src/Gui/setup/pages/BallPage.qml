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

// Session setup — Ball Detection, v2 temporal detector (the retired ScreenSessionWizard.qml Panel 3,
// l.1155–1287, with its footer hint, Skip and dimming, l.2090–2092, 2112, 2132, 2174), ported
// with every string, colour and size unchanged.
//
// Always in the flow. The user frames the hitting area, presses Learn to seed the empty-mat
// baseline (relearnBallBaseline → Option A), then places a ball; the step stays live (ballPresent,
// with a ting) and GATES Continue on a detected ball (also the registry's `gate`). Skip is an
// escape hatch. No calibration profile — the temporal detector self-calibrates at runtime.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    implicitHeight: ballCalCol.implicitHeight + Theme.sp(32)

    canContinue: page.ctx.ballPresent
    canSkip:     !page.ctx.ballPresent
    hint: page.ctx.ballPresent
          ? qsTr("Ball detected")
          : qsTr("Learn the hitting area, then place a ball — or skip ball detection")
    hintTone: page.ctx.ballPresent ? "good" : "warn"

    // True for ~the seed window after pressing Learn (or a
    // ROI drag, which also re-seeds) — shows "Learning…".
    property bool learning: false

    // The default hitting area when none is set (today: the frame Loader's onLoaded). On enter,
    // and again whenever the frame loads — a face-on camera that connects while this page is up.
    function enter(direction) { page.ctx.ensureBallRoi() }
    // A "Learning…" left over must not outlive the visit; a suspend keeps it (the timer resumes).
    function leave(reason) { if (reason !== "suspend") page.learning = false }

    Column {
        id: ballCalCol
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.sp(32) }
        spacing: Theme.sp(16)

        SetupStepIntro {
            width:   parent.width
            eyebrow: page.stepLabel
            heading: qsTr("Set up ball detection")
            body:    qsTr("Pinpoint learns what your empty hitting area looks like, then detects the ball against it — no calibration needed. Frame the hitting area, empty the mat and press Learn, then place a ball to confirm it's detected.")
        }

        // Cameras step skipped / face-on not connected.
        Text {
            width: parent.width
            visible: page.ctx.ballInstance === null
            text: qsTr("No face-on camera connected — go back to the Cameras step and connect one, or skip ball detection.")
            wrapMode: Text.WordWrap
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            color: Theme.colorWarn
        }

        RowLayout {
            width: parent.width
            visible: page.ctx.ballInstance !== null
            spacing: Theme.sp(20)

            Loader {
                id: ballStepFrame
                Layout.preferredWidth: Theme.sp(420)
                Layout.preferredHeight: Theme.sp(270)
                Layout.alignment: Qt.AlignTop
                // Only live while this page is active (R3) — today: while the wizard's
                // current step was this one, since the wizard stayed instantiated.
                active: page.active && page.ctx.ballInstance !== null
                onLoaded: page.ctx.ensureBallRoi()
                sourceComponent: PpCameraFrame {
                    instance: page.ctx.ballInstance
                    showPoseOverlay:      false
                    showHittingArea:      true
                    roiEditable:          true
                    showPerspectiveBadge: false
                    showStatsOverlay:     false
                    showReplayOverlay:    false
                }
            }

            ColumnLayout {
                id: ballStepCol
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.sp(12)

                Text {
                    Layout.fillWidth: true
                    text: qsTr("1. Empty the hitting area and position the box over where the ball sits at address — drag to move, corners to resize, drag outside to redraw.\n2. Press Learn.\n3. Place a ball — it's detected below.")
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText2
                }

                // Learn the empty-mat baseline (Option A).
                PpButton {
                    label:   qsTr("Learn hitting area")
                    primary: true
                    onClicked: {
                        page.learning = true
                        learnTimer.restart()
                        page.ctx.relearnBallBaseline()
                    }
                }
                Timer {
                    id: learnTimer
                    interval: 2000
                    // R3: runs only while the page is active and a Learn is in progress.
                    running: page.active && page.learning
                    onTriggered: page.learning = false
                }

                // Live present/absent badge. The instance tings on the
                // present transition (ballPresent), so no QML sound here.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.sp(4)
                    spacing: Theme.sp(8)
                    Rectangle {
                        width: Theme.sp(11); height: width; radius: width / 2
                        color: page.learning        ? Theme.colorWarn
                             : page.ctx.ballPresent ? Theme.colorGood
                                                    : Theme.colorText3
                    }
                    Text {
                        text: page.learning        ? qsTr("Learning the mat…")
                            : page.ctx.ballPresent ? qsTr("Ball detected")
                                                   : qsTr("No ball — place one in the hitting area")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color: page.ctx.ballPresent ? Theme.colorGood : Theme.colorText2
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Continue is enabled once a ball is detected.")
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                }
            }
        }
    }
}
