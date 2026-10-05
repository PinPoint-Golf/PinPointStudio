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

// Wrist/arm positional calibration — the host-facing composition. The SAME flow runs in
// two hosts:
//   • the start-session wizard  (layoutMode "full")
//   • the Wrist toolbar IMU panel (layoutMode "compact")
//
// Since Stage 2 of the session-setup refactor (session_wizard_refactor_design.md §4.7)
// this file holds no state machine. It composes:
//   • WitmotionArmRoutine / HackMotionArmRoutine — the two state machines, non-visual;
//   • CalibrationGuide                            — the 3-D guide the routines drive;
//   • ArmCalibrationStatus                        — the status column;
// and keeps the public API the hosts already use (layoutMode, showHeader, stepLabel,
// calibrationDone, calibrationFailed, mountFailed, phase, leadImu, isHackMotion,
// begin(), reset(), showCompleted(), completed(), cancelled()) plus `active`.
//
// ── TWO ROUTINES, PICKED AT THE TOP ────────────────────────────────────────────
// Which routine runs is decided from the DEVICE holding the leadForearm role (slot A):
// a Witmotion runs OUR routine, a HackMotion the DEVICE's. Exactly one exists at a time,
// in a Loader. Neither routine's state means anything for the other, so a vendor change
// stops the old routine and starts the new one fresh — a HackMotion "done" must NOT
// survive into the Witmotion flow (or the reverse).
//
// ── ACTIVITY (R3, R7) ───────────────────────────────────────────────────────────
// The routine Loader is active ONLY while `active`: a hidden host holds no routine and
// reacts to no device signal (finding F3). The semantics, as approved (D1, "stop and
// restart"):
//   • a routine runs only while `active` and begin() has been called (and not reset());
//   • `active` going false mid-run is stop("inactive"): nothing fires afterwards — no
//     timer, no guide callback, no device call other than a HackMotion's single abort,
//     no ting;
//   • `active` going true again: a run that was started and is not done begins FRESH;
//     a done one stays done and shows complete (no re-run, no second ting).
//
// ⚠ NOTHING HERE TOGGLES `visible` ON THE GUIDE VIEW. Its animation chain is what the
// routines wait on. Stopping is `active` and guide.cancel().
//
// ⚠ Nothing about the HackMotion calibration is persisted, ever: a plain BLE disconnect
// destroys it (measured 0.70° → 18.80° at the same pose, strap untouched), the library
// makes resume un-expressible, and this UI matches that rather than papering over it.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: flow

    // ── Config ────────────────────────────────────────────────────────────────
    property string layoutMode: "full"     // "full" (wizard) | "compact" (toolbar panel)
    property bool   showHeader: true        // step eyebrow + "Calibrate Sensors" title
    property string stepLabel:  qsTr("CALIBRATE")   // host-supplied eyebrow text

    // R3: the one activity flag. See the header for what it means. Every host binds it:
    // session setup's CalibrateArmPage (`active: page.active`) and the toolbar's PpImuPanel
    // (`active: mode === "calibrate" && visible`). An unbound flow is inactive and runs nothing.
    // (Until Stage 5b this defaulted to the flow's own visibility, for the old wizard, which
    // could not bind it; `visible` must not gate anything here — lint W1.)
    property bool active: false

    // Durations for the routines — TESTS ONLY (lint W7). null = the routines' own pace.
    property var pace: null

    // ── Outputs (read-only) — hosts bind to these ─────────────────────────────
    // The outcome, valid only while the device still holds it (design §4.6). A Witmotion's
    // done is the routine's word, as it always was. A HackMotion's dies with the link — a
    // plain disconnect destroys it — and that has to read through here even while no routine
    // exists (the wizard's Check and Ready read this gate after the Calibrate step is left).
    // A READ of the peripheral's state; nothing here calls or drives the device.
    readonly property bool calibrationDone:   _done && _outcomeValid
    readonly property bool calibrationFailed: _routine !== null && _routine.failed
    readonly property bool mountFailed:       _routine !== null && _routine.mountFailed
    readonly property int  phase:             _routine !== null ? _routine.phase
                                                                : ((_done && !isHackMotion) ? 2 : 0)
    // The leadForearm (slot-A) viz UNIT (or null): an ImuInstance for a Witmotion, the
    // lower-arm HmUnit for a HackMotion.
    readonly property QtObject leadImu: {
        var _dep = flow._roleDeps
        return imuManager.instanceForRole("leadForearm")
    }
    // Which routine is in play — hosts word their own copy from this.
    // Taken from the device-list entry's `vendor` field — the discriminator the
    // settings panel already uses (ImusPanel.qml:88) and the only one proven in this
    // tree. The HmInstance object also duck-types (it alone carries unitLowerArm /
    // calibrationPhase), but a property-absence probe would be a guess about QML's
    // undefined-property behaviour, and guessing wrong here silently runs OUR routine
    // against a device that has none of its methods.
    readonly property bool isHackMotion: {
        var _dep = flow._roleDeps
        return _vendorOf(imuManager.deviceIdForRole("leadForearm")) === "hackmotion"
    }

    // ── Signals ─────────────────────────────────────────────────────────────
    signal completed()
    signal cancelled()      // compact-mode "Back/Cancel"

    // ── API (host-driven entry points) ────────────────────────────────────────
    // A fresh run (clears, as it always did). While the host is not active the run is
    // requested and starts when it becomes active.
    function begin() {
        _restorePending = false
        _runRequested   = true
        _done           = false
        if (_routine === null) { _log("begin — starts when active"); return }
        // The routine that activation created and began in this same turn IS the fresh
        // run; a second begin() here would only clear the sensors twice.
        if (_beganThisTurn)    { _log("begin — already begun on activation"); return }
        _routine.begin()
    }
    // Clears as begin() does, without starting a run.
    function reset() {
        _restorePending = false
        _runRequested   = false
        if (_routine !== null) _routine.reset()
        _done = false
    }
    // Restore the completed state WITHOUT re-running — used on backward nav into
    // the wizard step, or when the lead IMU is already calibrated this session.
    // The routine decides what "complete" is (a HackMotion reads its device's live
    // state); see restore() in each.
    function showCompleted() {
        _runRequested = false
        if (_routine === null) { _restorePending = true; _log("showCompleted — when active"); return }
        _restoring = true
        _routine.restore()
        _done = _routine.done
        _restoring = false
    }

    // ⚠ A RESTORE IS NOT A COMPLETION. Session setup builds a fresh flow each time its Calibrate
    // page is entered (R2), so a restored done arrives as a false → true edge here; it must not
    // ring the completion ting a second time or report completed() again (CW7, CH10, CW13,
    // CH12). Only a run that completes does.
    // DECISION(stage5b): the one change here beyond the brief's three (`active`, CW4, F13) —
    // without it R8's "re-entry reads, never re-runs" still rang the ting on every re-entry.
    property bool _restoring: false
    onCalibrationDoneChanged: if (calibrationDone && !_restoring) { calibCompleteTing.play(); flow.completed() }

    TingPlayer { id: calibCompleteTing; frequency: 4186.0 }  // C8 — two octaves above the ball ting

    // ── Internal wiring ───────────────────────────────────────────────────────
    // begin() called and reset()/showCompleted() not since.
    property bool _runRequested: false
    // showCompleted() arrived while no routine existed.
    property bool _restorePending: false
    // The outcome, held here so it outlives the routine (which exists only while active).
    property bool _done: false
    // The routine Loader's switch: set from `active` by _syncActive(), AFTER the routine
    // has been stopped, so a routine is never destroyed mid-run without stop().
    property bool _live: false
    property Component _routineComp: null
    property bool _setUp: false
    property bool _beganThisTurn: false
    // The last word of the routine that was unloaded (a HackMotion abort's wording).
    property string _lastMessage:     ""
    property string _lastMessageKind: ""

    readonly property var _routine: routineLoader.item !== undefined ? routineLoader.item : null

    // The guide (the routines are handed it below; the test drivers read its `view`). The two
    // compatibility names for the retired wizard, _bvv and _autoStartGate, went with it at
    // Stage 5c: read `_guide.view` and `_runRequested`.
    readonly property alias _guide: guide

    readonly property bool _rightHanded: athleteController.currentHandedness !== "Left"
    readonly property quaternion _trailArmDownQuat: Qt.quaternion(0.7071, 0.7071, 0, 0)

    function _log(text) { appLog.info("Calib", "flow " + text) }

    // ── Role resolution ───────────────────────────────────────────────────────
    // ImuManager owns the placement lookup (role-keyed; a HackMotion's two units are
    // keyed "<deviceId>#lowerArm" / "#palm", so ONE peripheral fills leadForearm and
    // leadHand). Walking imuDeviceList against the role map here would only
    // re-implement that, wrongly.
    //
    // ⚠ instanceForRole() returns the VIZ object — an HmUnit for a HackMotion role, an
    // ImuInstance for a Witmotion one — while deviceForRole() returns the owning
    // PERIPHERAL. Both answer to QML by name and they are DIFFERENT OBJECTS for a
    // HackMotion. Cube/viz and per-sensor quaternions are unit work; calibration is
    // device work.
    //
    // The reactive dependencies, written ONCE: the resolvers are Q_INVOKABLEs, not
    // properties, so without these reads a binding would resolve once and never
    // re-evaluate when an instance is created by Connect or a role is edited.
    // ⚠ A bare `imuManager.instances` statement is DROPPED by the QML compiler and takes
    // the dependency with it — every binding below ASSIGNS `_roleDeps`.
    readonly property var _roleDeps: [imuManager.instances, appSettings.imuRoles, imuManager.imuDeviceList]

    function _vendorOf(id) {
        if (id === "") return ""
        var list = imuManager.imuDeviceList
        for (var i = 0; i < list.length; ++i)
            if (list[i].id === id) return list[i].vendor
        return ""
    }

    readonly property int  _calCalibrated: 2   // WR_CAL_CALIBRATED (HackMotionArmRoutine.calCalibrated)
    readonly property bool _outcomeValid: !isHackMotion
                                          || (_leadDevice !== null && _leadDevice.imuConnected
                                              && _leadDevice.calibrationState === _calCalibrated)

    // The PERIPHERAL behind leadForearm — the object every wr_calibration_* call goes
    // to. For a Witmotion it is the same ImuInstance leadImu resolves to; for a
    // HackMotion it is the HmInstance that OWNS leadImu.
    readonly property QtObject _leadDevice: {
        var _dep = flow._roleDeps
        return imuManager.deviceForRole("leadForearm")
    }

    // What the routine is given. Resolved here, in one binding, straight from the
    // manager (not from the bindings above), so a role edit never hands a routine a
    // half-updated set.
    //   HackMotion: { device: the peripheral, lowerArm / palm: its two UNITS }
    //   Witmotion:  { a: forearm, b: hand, c: upper arm } — each unit is its own device.
    //               A role held by a wG3 unit is never handed to the Witmotion routine:
    //               an HmUnit has no imuConnected and none of the calibration methods.
    readonly property var _segments: {
        var _dep = flow._roleDeps
        if (_vendorOf(imuManager.deviceIdForRole("leadForearm")) === "hackmotion")
            return { device:   imuManager.deviceForRole("leadForearm"),
                     lowerArm: imuManager.instanceForRole("leadForearm"),
                     palm:     imuManager.instanceForRole("leadHand") }
        var wt = function(role) {
            return _vendorOf(imuManager.deviceIdForRole(role)) === "hackmotion"
                   ? null : imuManager.instanceForRole(role)
        }
        return { a: wt("leadForearm"), b: wt("leadHand"), c: wt("leadUpperArm") }
    }

    // ── Activity and the routine's lifetime ───────────────────────────────────
    // ⚠ STOP NOW, UNLOAD ON THE NEXT TURN. `active` can go false from INSIDE the routine's
    // own handler — the toolbar panel leaves calibrate mode on completed(), which the
    // routine emits from a timer's onTriggered — and a Loader destroys its item
    // synchronously, which would run the rest of that handler in a dead context. stop()
    // runs at once (every timer and connection is off from then on: they are gated on
    // `active`); the stopped routine is dropped on the next event-loop turn, or picked up
    // again by a re-activation in the same turn.
    property bool _unloadPending: false
    function _syncActive() {
        if (!_setUp) return
        if (active) {
            if (!_live) { _log("active"); _live = true }   // the Loader creates the routine
            else if (_unloadPending) {
                _unloadPending = false
                _log("active (stopped routine reused)")
                _onRoutineLoaded()
            }
        } else if (_live && !_unloadPending) {
            if (_routine !== null) {
                _routine.stop("inactive")
                _lastMessage     = _routine.message
                _lastMessageKind = _routine.messageKind
            }
            _unloadPending = true
            _log("inactive")
            Qt.callLater(flow._unloadIfInactive)
        }
    }
    function _unloadIfInactive() {
        if (!_unloadPending) return
        _unloadPending = false
        if (!active) _live = false
    }
    onActiveChanged: _syncActive()

    // Slot A changed KIND under us — a device was reassigned. Stop the old routine,
    // drop the outcome, and let the new routine start if a run was requested.
    onIsHackMotionChanged: {
        if (!_setUp) return
        _log("vendor " + (isHackMotion ? "hackmotion" : "witmotion"))
        if (_routine !== null) _routine.stop("vendor")
        _done = false
        _routineComp = isHackMotion ? hmRoutineComp : wtRoutineComp
    }

    Component.onCompleted: {
        _routineComp = isHackMotion ? hmRoutineComp : wtRoutineComp
        _setUp = true
        _syncActive()
    }

    function _onRoutineLoaded() {
        var r = _routine
        if (r === null) return
        _lastMessage = ""
        _lastMessageKind = ""
        _log("routine " + (isHackMotion ? "hackmotion" : "witmotion"))
        if (_restorePending || _done) {
            _restorePending = false
            _restoring = true
            r.restore()
            _done = r.done
            _restoring = false
        } else if (_runRequested) {
            r.begin()
            _beganThisTurn = true
            Qt.callLater(function() { flow._beganThisTurn = false })
        }
    }

    Loader {
        id: routineLoader
        active:          flow._live
        sourceComponent: flow._routineComp
        onLoaded:        flow._onRoutineLoaded()
    }

    Component {
        id: wtRoutineComp
        WitmotionArmRoutine {
            active:      flow.active
            guide:       flow._guide
            segments:    flow._segments
            rightHanded: flow._rightHanded
            pace:        flow.pace !== null ? flow.pace : defaultPace
        }
    }
    Component {
        id: hmRoutineComp
        HackMotionArmRoutine {
            active:   flow.active
            guide:    flow._guide
            segments: flow._segments
            pace:     flow.pace !== null ? flow.pace : defaultPace
        }
    }

    // The outcome outlives the routine; follow it while one is loaded.
    Connections {
        target:  flow._routine
        enabled: flow.active
        function onDoneChanged() { flow._done = flow._routine.done }
    }

    // The 3-D guide (R9). The view in the active layout registers itself as `guide.view`.
    CalibrationGuide {
        id: guide
        active:         flow.active
        // ⚠ THE HACKMOTION ROUTINE IS INVISIBLE FROM THE DEFAULT FRONT CAMERA. Its
        // pose 0 points the forearm forward, almost along that camera's view axis, so
        // both the pose and the 30° sweep collapse into a stub twitching sideways. The
        // Witmotion routine's arm-down → T-pose happens in the frontal plane and reads
        // perfectly from the front, which is why only this branch moves the camera.
        useGuideCamera: flow.isHackMotion
    }

    // Recalibrate is offered once anything has happened (both layouts).
    readonly property bool _showRecalibrate: {
        var r = _routine
        if (calibrationDone) return true
        if (r === null) return false
        return r.phase > 0 || r.failed || r.mountFailed
               || (isHackMotion && r.hmStep !== undefined && r.hmStep > 0)
    }

    // ── Layout: full (wizard) vs compact (toolbar panel) ───────────────────────
    Loader {
        anchors.fill: parent
        sourceComponent: flow.layoutMode === "compact" ? compactLayout : fullLayout
    }

    // FULL — RowLayout: BodyVizView (fill) | status column (sp(360), right).
    Component {
        id: fullLayout

        RowLayout {
            anchors.fill:    parent
            anchors.margins: Theme.sp(8)
            spacing:         Theme.sp(12)

            BodyVizView {
                id: calibBvvFull
                Layout.fillHeight:   true
                Layout.fillWidth:    true
                Layout.minimumWidth: Theme.sp(200)
                Component.onCompleted: guide.view = calibBvvFull

                poseSource:       null   // no camera input during calibration
                rightHanded:      flow._rightHanded
                highlightLeadArm: true
                leadArmColor:     Theme.colorAccent

                useLeadArmOverride:      true
                leadArmOverrideRotation: guide.armTarget
                // ⚠ THE HACKMOTION RAISE IS THIS PROPERTY. Its two poses share one
                // upper-arm rotation (the elbow must not move), so the travel the
                // device watches is a forearm rotation and the forearm target is
                // what changes between the markers. Identity — unchanged — for the
                // Witmotion routine. Derivation in BodyVizView.qml.
                leadForeArmOverrideRotation: flow.isHackMotion ? guide.foreTarget
                                                               : Qt.quaternion(1, 0, 0, 0)

                useTrailArmOverride:          true
                trailArmOverrideRotation:     flow._trailArmDownQuat
                trailForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

                animateLeadArm: guide.animating

                // See the CalibrationGuide declaration above.
                useGuideCamera: guide.useGuideCamera
            }

            ArmCalibrationStatus {
                Layout.preferredWidth: Theme.sp(360)
                Layout.fillHeight:     true
                Layout.topMargin:      Theme.sp(32)
                mode:            "full"
                routine:         flow._routine
                isHackMotion:    flow.isHackMotion
                leadImu:         flow.leadImu
                leadDevice:      flow._leadDevice
                calibrationDone: flow.calibrationDone
                showHeader:      flow.showHeader
                stepLabel:       flow.stepLabel
                onRecalibrate:   flow.begin()
            }
        }
    }

    // COMPACT — stacked BodyVizView (~sp(220)) + status, scrolling in a Flickable,
    // with the action bar PINNED at the bottom so it is always visible even when a
    // short window clamps the popup. The whole-panel attention frame is drawn by the
    // host panel; here the action bar is a plain container whose main action (Cancel)
    // uses the attention colour.
    Component {
        id: compactLayout

        Item {
            anchors.fill: parent

            // Pinned, always-visible action bar.
            Item {
                id: actionBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                anchors.margins: Theme.sp(12)
                height: Theme.sp(54)

                RowLayout {
                    anchors { fill: parent; leftMargin: Theme.sp(10); rightMargin: Theme.sp(10) }
                    spacing: Theme.sp(8)
                    PpButton {
                        visible: flow._showRecalibrate
                        label:   qsTr("↺  Recalibrate")
                        primary: false
                        onClicked: flow.begin()
                    }
                    Item { Layout.fillWidth: true }
                    PpButton {
                        label:     qsTr("Cancel")
                        attention: true
                        // ⚠ A HackMotion routine given up on must be aborted, or the
                        // library sits in it until its own limits expire. The abort
                        // ALWAYS works — but at VERIFYING it only DECLINES the
                        // presence check, so the routine's stop() words it that way
                        // rather than claiming the calibration was undone.
                        onClicked: { if (flow._routine !== null) flow._routine.stop("cancel"); flow.cancelled() }
                    }
                }
            }

            Flickable {
                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: actionBar.top }
                contentWidth: width
                contentHeight: compactCol.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: compactCol
                    width: parent.width
                    spacing: Theme.sp(12)

                    BodyVizView {
                        id: calibBvvCompact
                        Layout.fillWidth:       true
                        Layout.preferredHeight: Theme.sp(220)
                        Layout.leftMargin:      Theme.sp(12)
                        Layout.rightMargin:     Theme.sp(12)
                        Layout.topMargin:       Theme.sp(12)
                        Component.onCompleted: guide.view = calibBvvCompact

                        poseSource:       null
                        rightHanded:      flow._rightHanded
                        highlightLeadArm: true
                        leadArmColor:     Theme.colorAccent

                        useLeadArmOverride:      true
                        leadArmOverrideRotation: guide.armTarget
                        // See the full layout above: for HackMotion the RAISE itself
                        // lives in this property; identity for the Witmotion routine.
                        leadForeArmOverrideRotation: flow.isHackMotion ? guide.foreTarget
                                                                       : Qt.quaternion(1, 0, 0, 0)

                        useTrailArmOverride:          true
                        trailArmOverrideRotation:     flow._trailArmDownQuat
                        trailForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

                        animateLeadArm: guide.animating

                        // Same reason as the full layout — see the comment there.
                        useGuideCamera: guide.useGuideCamera
                    }

                    ArmCalibrationStatus {
                        Layout.fillWidth:    true
                        Layout.leftMargin:   Theme.sp(15)
                        Layout.rightMargin:  Theme.sp(15)
                        Layout.bottomMargin: Theme.sp(12)
                        mode:            "compact"
                        routine:         flow._routine
                        isHackMotion:    flow.isHackMotion
                        leadImu:         flow.leadImu
                        leadDevice:      flow._leadDevice
                        calibrationDone: flow.calibrationDone
                    }
                }
            }
        }
    }
}
