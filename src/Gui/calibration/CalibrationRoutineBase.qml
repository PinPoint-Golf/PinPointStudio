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

// The scaffold every calibration routine shares — the §4.7 routine interface, written ONCE
// (session_wizard_refactor_design.md §4.7). WitmotionArmRoutine, HackMotionArmRoutine and
// TrunkRoutine have this as their root type and keep only their own state machines.
//
// ── THE HOST CONTRACT ───────────────────────────────────────────────────────────────
// What a host (ImuCalibrationFlow for the two arm routines, CalibrateTrunkPage for the trunk) may
// use of a routine, and all it may use:
//   in    active                R3: the one activity flag — bound explicitly by the host
//         guide                 CalibrationGuide (R9)
//         segments              the routine's sensors, in the routine's own shape (see each file)
//         rightHanded           the golfer's hand (a routine with no use for it ignores it)
//         pace                  durations — TESTS ONLY (lint W7); the host forwards its own
//   out   stage, substage       where the run is ("idle" before begin(); substage "" when the
//                               routine has no steps inside a stage)
//         done                  = calibrationDone: the routine's word that it finished
//         failed, mountFailed   the run stopped short / a sensor is mounted wrongly
//         phase                 the Witmotion capture phase (0 for the others)
//         message, messageKind  the failure on screen, and "error" | "warn" | ""
//         signal completed()    a run finished (each routine says when — see its file)
//   call  begin()               a fresh run: the routine's clear, then its chain starts
//         stop(reason)          leave: every timer off, the guide stopped, the stage back to idle
//                               unless done; nothing on the sensors cleared. A no-op unless a run
//                               is in hand. Active going false is stop("inactive").
//         reset()               the clear begin() does, without starting a run
//         restore()             show the finished state WITHOUT re-running (R8), from what the
//                               sensors or the device hold — never a replay
// The outputs are DECLARED here and BOUND by each routine; hosts only read them.
//
// ── WHAT EACH ROUTINE SUPPLIES ──────────────────────────────────────────────────────
//   logName        the word on its app-log lines ("witmotion", "hackmotion", "trunk")
//   defaultPace    its table of durations, in ms, and `scale: 1`
//   the hooks — signals this base emits at a fixed point of each entry point, handled
//   synchronously by the routine (on_Clearing, on_Begun, …):
//     _clearing(forRun, wasRunning)  begin() (forRun) and reset(): clear the run, and on a begin
//                                    whatever it starts from. wasRunning: a run was in hand.
//     _begun()                       begin(), once `_running` is true
//     _stopping()                    stop(), while the run is still live (a device routine aborts
//                                    here, so its own stop reads as its last stage)
//     _stopped()                     stop(), once `_running` is false and the guide is cancelled
//     _restoring()                   restore(), before `_running` goes false
//     _guideDone()                   the guide finished a motion this routine is waiting on
//   and, to add to the log lines: _stopNote (stop) and _restoreNote (restore).
//
// ⚠ R3: every Timer.running and Connections.enabled — here and in each routine — is ANDed with
// `active`, never `visible`, never a step index (lint W1). A routine runs only while
// `active && <begin() called and stop() not>`.
//
// ── THE APP LOG (R10) ───────────────────────────────────────────────────────────────
// Every entry point writes ONE line, and so does every stage the routine moves to:
// "stage=<stage>", then " step=<substage>" and a reason when the routine has them. A routine whose
// stage is a binding (the arm routines) leaves that line to onStageChanged below. One that moves
// through steps inside a stage (TrunkRoutine) sets logStageChanges false and writes each transition
// through _logStage(why) — a step change is then a line too, and the reason rides on it.
import QtQuick

QtObject {
    id: base

    // ── Inputs ────────────────────────────────────────────────────────────────
    property bool active: false
    property var  guide: null
    property var  segments: null
    property bool rightHanded: true

    // The arm hanging at the side — the arm routines' rest pose for the guide. Arm-down:
    // shoulder-local +Z = world -Y (verified by computing shoulder.rotation * (0,0,1) for both left
    // and right shoulders); rotating armNode's +Y to shoulder-local +Z = R_x(+90°) for both arms.
    // The mirror symmetry of the shoulder nodes means both use the same value.
    readonly property quaternion leadArmDownQuat: Qt.quaternion(0.7071, 0.7071, 0, 0)

    // Every duration, in ms, and `scale` (1 in the app; tests set it, lint W7). Each routine binds
    // defaultPace to its own table; `pace` overrides any of its keys.
    property var defaultPace: ({ scale: 1 })
    property var pace: defaultPace
    readonly property var _p: {
        var o = {}, k
        for (k in defaultPace) o[k] = defaultPace[k]
        if (pace) for (k in pace) o[k] = pace[k]
        return o
    }
    function _ms(name) { return Math.round(_p[name] * _p.scale) }

    // ── Outputs (the host contract above; each routine binds them) ────────────
    property string stage:       "idle"
    property string substage:    ""
    property bool   calibrationDone: false
    readonly property bool done: calibrationDone
    property bool   failed:      false
    property bool   mountFailed: false
    property int    phase:       0
    property string message:     ""
    property string messageKind: ""

    signal completed()

    // True between begin() and stop() (it outlives done and the failures, which are stages of a
    // run; only stop() and restore() end one).
    property bool _running: false

    // ── The hooks (see the header) ────────────────────────────────────────────
    signal _clearing(bool forRun, bool wasRunning)
    signal _begun()
    signal _stopping()
    signal _stopped()
    signal _restoring()
    signal _guideDone()
    property string _stopNote:    ""
    property string _restoreNote: ""

    // ── The app log ───────────────────────────────────────────────────────────
    property string logName: ""
    function _log(text) { appLog.info("Calib", logName + " " + text) }

    // The stage line. Created idle; every stage the routine moves to after that is one line.
    property bool logStageChanges: true
    function _logStage(why) {
        _log("stage=" + stage + (substage !== "" ? " step=" + substage : "") + (why ? " " + why : ""))
    }
    property bool _created: false
    Component.onCompleted: _created = true
    onStageChanged: if (_created && logStageChanges) _logStage("")

    // ── Entry points ──────────────────────────────────────────────────────────
    function begin() {
        _log("begin")
        _clearing(true, _running)
        _running = true
        _begun()
    }

    function stop(reason) {
        if (!_running) return
        var was = stage
        _stopNote = ""
        _stopping()
        _running = false
        if (guide) guide.cancel()
        _stopped()
        _log("stop reason=" + reason + " stage=" + was + _stopNote)
    }

    function reset() {
        _log("reset")
        var was = _running
        _running = false
        if (guide) guide.cancel()
        _clearing(false, was)
    }

    function restore() {
        _restoreNote = ""
        _restoring()
        _running = false
        _log("restore done=" + calibrationDone + _restoreNote)
    }

    onActiveChanged: if (!active) stop("inactive")

    // Guide-animation completion: each stage of a routine's chain advances when the guide has
    // actually FINISHED drawing (BodyVizView signals the slerp's end), never on a parallel
    // wall-clock timer — which keeps counting while a stalled renderer shows nothing and would run
    // the chain ahead of the golfer. A routine narrows when it listens with _guideWatched.
    // (Named: the test drivers find it by name.)
    property bool _guideWatched: true
    property Connections _guideFinished: Connections {
        target:  base.guide
        enabled: base.active && base._running && base._guideWatched
        function onFinished() { base._guideDone() }
    }
}
