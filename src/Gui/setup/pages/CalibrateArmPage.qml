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

// Session setup — Calibrate (the arm group's calibrate step; the retired ScreenSessionWizard.qml Panel 5,
// l.1640–1660, with its footer hint and Skip, l.2098–2100, 2134). Design §4.7 "Hosts".
//
// ONE guide view and ONE routine: the page hosts ImuCalibrationFlow in its full layout, which
// holds exactly one BodyVizView and loads exactly one routine — the leadForearm holder's vendor,
// the same answer as ctx.groups.arm.routine. The page exists only while Calibrate is current
// (R2), so neither does the routine (R7, fixes F3 for the wizard host).
//
// Lifecycle (D1, R8):
//   enter  — a recorded arm outcome that is still VALID (draft.outcome: the devices' live
//            validity) shows complete with no re-run (showCompleted, fixes F1 for both vendors);
//            otherwise a fresh run (begin).
//   leave  — nothing to do here: the flow sets `active` false after leave(), and the calibration
//            flow stops its routine on that edge (a HackMotion aborts, once).
//   completed — the outcome goes into the draft, which outlives this page (Check, Ready, the
//            indicator read it there).
// Recalibrate (the status column's button) begins a fresh run; the outcome is dropped from the
// draft as the run's done goes false (below), so Continue closes at once.
import QtQuick
import PinPointStudio

WizardPage {
    id: page

    fullBleed: true

    readonly property bool _done: page.draft.outcome("arm").done

    canContinue: _done
    canSkip:     !_done
    hint:        _done ? qsTr("Calibration complete")
                       : qsTr("Follow the guide to calibrate your sensors — or skip to continue without calibration")
    hintTone:    _done ? "good" : "warn"

    // What the outcome is checked against while it stands (design §4.6): a Witmotion run's
    // segment UNITS (fullyCalibrated each), a wG3's PERIPHERAL (connected + CALIBRATED). The
    // segments are the arm roles in the session that are connected — the ones the routine
    // calibrated (WitmotionArmRoutine._connectedSegs).
    function _outcomeDevices() {
        var r = page.ctx.roles, g = page.ctx.groups.arm
        if (g.routine === "hackmotion")
            return r.leadForearm.deviceObject !== null ? [r.leadForearm.deviceObject] : []
        var out = []
        for (var i = 0; i < g.roles.length; ++i) {
            var e = r[g.roles[i]]
            if (e.unit !== null && e.connected && out.indexOf(e.unit) < 0) out.push(e.unit)
        }
        return out
    }

    function enter(direction) {
        if (page.draft.outcome("arm").done) calibFlow.showCompleted()
        else                                calibFlow.begin()
    }

    ImuCalibrationFlow {
        id: calibFlow
        anchors.fill: parent
        layoutMode:   "full"
        showHeader:   true
        stepLabel:    page.stepLabel
        // R3: bound EXPLICITLY — the flow has no activity of its own.
        active:       page.active

        onCompleted: page.draft.recordOutcome("arm", page.ctx.groups.arm.routine, page._outcomeDevices())
    }

    // A done that goes away while the page is up is a Recalibrate (begin() clears it) or a lost
    // link: either way the recorded outcome no longer stands.
    Connections {
        target:  calibFlow
        enabled: page.active
        function onCalibrationDoneChanged() {
            if (!calibFlow.calibrationDone) page.draft.clearOutcome("arm")
        }
    }
}
