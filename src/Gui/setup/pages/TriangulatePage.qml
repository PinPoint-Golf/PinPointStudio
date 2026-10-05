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

// Session setup — Triangulate (the retired ScreenSessionWizard.qml Panel 2, l.1094–1153, with its footer hint
// and Skip, l.2085–2089, 2109–2111, 2131), ported with every string, colour and size unchanged.
//
// Only in the plan when both camera perspectives are connected (the registry's `applies`).
// Hosts the same stereo-calibration STUB PpCameraPanel's calibrate mode uses; the real ChArUco
// capture flow drops in here when the pipeline lands (TODO). The stub's validity flags are
// SetupContext's, still always false — kept as today (D7, F11).
//
// If the DTL camera is deselected while this page is current, the step stays current and the
// shell's footer reads "No longer needed — Continue" (SetupFlow.noLongerNeeded, N14); nothing
// here has to know.
import QtQuick
import PinPointStudio

WizardPage {
    id: page

    implicitHeight: triangulateCol.implicitHeight + Theme.sp(32)

    canSkip: !page.ctx.triangulationValid
    hint: page.ctx.triangulationValid
          ? qsTr("Triangulation confirmed")
          : page.ctx.anyFixedCamera
              ? qsTr("Optional — cameras are fixed in place")
              : qsTr("Stereo calibration isn't available yet — skip to continue")
    hintTone: (page.ctx.triangulationValid || page.ctx.anyFixedCamera) ? "good" : "warn"

    Column {
        id: triangulateCol
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.sp(32) }
        spacing: Theme.sp(16)

        SetupStepIntro {
            width:   parent.width
            eyebrow: page.stepLabel
            heading: qsTr("Triangulating your cameras")
            body:    qsTr("With a face-on and a down-the-line camera connected, Pinpoint can reconstruct your movement in 3D. Stereo calibration solves where the cameras sit relative to each other; triangulation confirms the result. If your setup hasn't moved since last time, you're good to go.")
        }

        Column {
            width: parent.width
            spacing: 0

            SetupCheckRow {
                width:        parent.width
                ok:           page.ctx.stereoCalibrationValid
                optional:     page.ctx.anyFixedCamera
                label:        qsTr("Stereo calibration")
                subOk:        qsTr("Calibration valid")
                subFail:      page.ctx.anyFixedCamera
                                  ? qsTr("OPTIONAL — CAMERAS ARE FIXED IN PLACE")
                                  : qsTr("CALIBRATION NEEDED")
                showRecal:    true
                recalEnabled: page.ctx.faceOn.length > 0 && page.ctx.dtl.length > 0
                onRecalibrate: page.flow.requestCameraRecalibrate()
            }
            SetupCheckRow {
                width:    parent.width
                ok:       page.ctx.triangulationValid
                optional: page.ctx.anyFixedCamera
                label:    qsTr("Triangulation")
                subOk:    qsTr("Baseline confirmed")
                subFail:  page.ctx.anyFixedCamera
                              ? qsTr("OPTIONAL — CAMERAS ARE FIXED IN PLACE")
                              : qsTr("NOT CONFIRMED")
            }
        }

        // Stereo calibration flow — in-panel STUB until the
        // real pipeline lands; shared with PpCameraPanel. It exists only while this page does
        // (R2): today it lived, hidden, for the wizard's whole life.
        CameraCalibrationFlow {
            width:  parent.width
            height: Theme.sp(300)
            layoutMode: "full"
            showHeader: false
        }
    }
}
