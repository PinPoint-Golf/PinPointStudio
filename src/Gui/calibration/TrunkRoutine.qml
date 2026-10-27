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

// The trunk calibration — the golfer's ceremony for the hip and chest sensors
// (trunk_imu_sequence_grf_design.md §6.3 steps 1–3, §6.7, §16.2 W2.2). The hop (step 4) is not in W2.
//
//   intro        the guide shows the stand-tall pose; nothing starts until the guide can be watched
//   standTall    ready: nothing runs until the golfer has the sensors on — BOTH upright (the live
//                long axis within the stand-tall gate) and still (gyroDps) for `ready` without a
//                break; the page says which sensor is the reason while it waits. Then settle, then the
//                hold bar: both sensors still and upright for `hold`, then calibrateTrunk() on both. A
//                failure names the sensor (the C++ record's own words), stays on screen for `retry`,
//                and stand tall begins again at ready — it waits for upright and still again, never a
//                blind re-run on a timer.
//   turnAway     the BACKSWING TURN (golfer-facing words: below).
//                demo: the guide turns away (hips 45°, chest 90°) and back, with ONE short instruction,
//                while the golfer faces the screen — everything to read is read here.
//                arming: both sensors still (gyroDps, `ready`'s rule) for `arm` → armed: the step's
//                recording begins NOW (stepStart), at address, never at the demo.
//                armed → turning: the chest's live heading (ImuInstance.trunkHeadingDeg) `_turnStartDeg`
//                from its armed heading. Within turning, the TURN-BACK CUE (turnBackDue) once he has
//                turned enough (below). turning → returned: back within `_turnReturnDeg` of it with
//                both sensors still for `returnStill` — the golfer faces the screen again. Then stepEnd
//                and the recorder's checkTurn("away"), and the verdict on screen.
//   turnThrough  the FOLLOW-THROUGH TURN: the same toward the target, checkTurn("through").
//   done         both turns passed: the A/M from stand tall stand, completed() once.
//   failed       a trunk sensor is missing or dropped its link: Recalibrate starts again.
//
// A turn that fails shows its verdict (`retry`), then re-arms by itself on stillness — no demo again,
// no click — EXCEPT when a sensor reads the turn backwards or the two look
// swapped (checkTurn's cause "backwards" / "swapped"): a sensor that has been re-seated or moved
// invalidates its stand-tall A/M, so the golfer goes back to STAND TALL and then both turns again.
// A turn the golfer made the other way (wrongWay) adds a hint about the hand setting.
//
// ── WHEN IS A TURN DONE? When the sensors say the golfer is back facing the screen. ──
// Mark, second real-sensor run (26 Oct): "asking the user to turn away from the screen AND read
// instructions is a terrible idea — how am I supposed to read and turn away!?" The 10 s window with a
// countdown is gone (design §16.2). A turn step is TAUGHT facing the screen (the demo and one line),
// ARMED on stillness at address, and STARTED and ENDED by the chest's live heading: out by
// `_turnStartDeg`, back within `_turnReturnDeg` and still. The end condition puts the golfer facing
// the screen, so the verdict is read then. This detector decides only WHEN the recording ends; the
// verdict is still checkTurn()'s, read from the recorder's own lanes over the whole step
// (trunk_ceremony.h: "ONE STEP, NOT ONE REPETITION"). Its hangs are bounded: a turn that started
// but never comes back is ended and checked after `turnTimeout`; a turn that never starts waits —
// shown on screen and logged once per `waitLog`, as stand tall's `ready` waits. A reading that is
// not finite (a filter fault) counts as no reading: it neither starts, returns nor arms.
// While armed and still, the armed heading FOLLOWS slow drift (at most `_armDriftFollowDps`): Mark's
// third run (27 Oct) saw the fused heading creep ~0.1 °/s, which over a long wait adds up to the start
// threshold and "starts" a turn nobody made — the page then said turn back before he had moved.
// Every gate here mirrors pp_tuned_constants.h tuned::trunkRoutine (lint W11 holds them equal).
//
// ── GOLF TERMS FOR THE GOLFER (Mark, 27 Oct) ──
// "Away" / "through" are the design's words and stay in the code. Everything the golfer reads says
// BACKSWING TURN (away) and FOLLOW-THROUGH TURN (through), "address" for where he stands facing the
// screen, and the trail shoulder when it says which way (trunk_ceremony.h; CalibrateTrunkPage).
//
// ── THE TWO CUES OF A TURN: when to start, and when to come back (Mark, fourth real-sensor run, 27 Oct:
// "I need to know when to start the turn and when to come back — there is no other way to know") ──
// The golfer faces away while he turns, so each turn attempt gives him two cues, on screen (the page's
// status) and — with the setting on — spoken:
//   START       the moment the step is ARMED (still at address, the recording begun): "Start your
//               backswing turn." / "Start your follow-through turn." Every attempt, a retry too.
//   TURN BACK   once per attempt, when he has turned ENOUGH: the chest's |live turn| reaches
//               `_turnBackCueDeg` (70°, the chest gate plus a margin — tuned::trunkRoutine::
//               kTurnBackCueDeg says why), OR the turn has started and he has STOPPED at the top short
//               of it — both sensors still (`_stillReasons`, kStillGyroDps) for `returnStill` without
//               a break, still beyond `_turnStartDeg` — whichever comes first: "Turn back to address."
//               (`turnBackDue` goes true, and the page's status with it). The stopped-short branch keeps
//               a golfer who cannot reach 70° from standing at the top for ever; the check then judges
//               the turn he made, honestly. The cue only TELLS him: the step still ends when the
//               sensors say he is back (or at the timeout), and checkTurn() still judges it.
// The 26 Oct routine said its one line the instant the turn STARTED (20°): far too early — Mark's
// fourth run heard it at 20.8° and went on to 99°.
//
// SPOKEN, EXPERIMENTAL (Mark, 27 Oct: "we ONLY use it for INSTRUCTIONS"): with `speakInstructions` on
// (AppSettings.experimentalSpokenInstructions, default off) each cue is said through `ttsController`.
// Those three lines are the ONLY things ever spoken: no verdict, status, failure, demo or stand tall.
// The start line is skipped (the reason logged) with no voice, or one not ready or already speaking.
// The turn-back line has PRIORITY: it stops whatever is still playing — a fast golfer can reach 70°
// before the start line ends — and is said at once, or as soon as the voice reports it has stopped.
// Leaving (stop) or Recalibrate (begin) stops what was said. The lines are PREPARED IN ADVANCE (Mark,
// 27 Oct): their words live in spoken_instructions.js, listed as `spokenPhrases` here and on the step's
// descriptor, and the setup shell has ttsController synthesise them when setup is created / the
// setting goes on / the voice is ready or changes — so each cue plays cached audio at once. Each speak
// line in the log names the cue, says whether it was prepared, and the voice's latency follows.
//
// The ceremony file (W2.1c): the routine brackets each step on the recorder — stepStart("standTall")
// … attachResult × 2 … stepEnd, stepStart("turnAway") … checkTurn … stepEnd, and so on — and calls
// end() when it finishes, pass or fail. It begins the recording itself unless one is already running
// (trunk + wG3: the arm's Calibrate step began it, §6.7). A step repeated after a failure is a new
// stepStart of the same name.
//
// NON-VISUAL, the §4.7 routine interface (as WitmotionArmRoutine). Injected: the guide
// (CalibrationGuide, R9 — trunkPose / trunkAnimate), the segments { pelvis, thorax } (the two
// Witmotion UNITS), the recorder (`trunkCeremony`, the context object, handed in by the host) and the
// pace. ⚠ R3: every Timer.running and Connections.enabled below is ANDed with `active`; active going
// false is stop("inactive"). Every stage change and every entry point is ONE app-log line (R10).
import QtQuick
import "spoken_instructions.js" as Spoken

QtObject {
    id: r

    // ── Inputs ────────────────────────────────────────────────────────────────
    property bool active: false
    property var  guide: null
    property var  segments: null          // { pelvis, thorax } — ImuInstance units
    property bool rightHanded: true
    // TrunkCeremonyRecorder (trunk_ceremony_recorder.h). Named as the context object is, so lint W5
    // checks every member used below against its header. null = nothing recorded and no turn check.
    property var  trunkCeremony: null
    // The EXPERIMENTAL spoken instructions (header): on → each turn's start and turn-back cues are said.
    property bool speakInstructions: false
    // The voice: TtsController (media/tts_controller.h — speak, stopSpeaking, ttsReady, ttsActive,
    // lastTtsLatencyMs), named as the context object is so lint W5 checks every member used below
    // against its header. The host hands it in; tests hand in a FakeTts that only records. null = no
    // voice (headless): nothing is spoken, and the skip is logged.
    property var  ttsController: null

    // Every duration, in ms, and `scale` (1 in the app; tests set it, lint W7).
    readonly property var defaultPace: ({
        introStart: 1500,   // the guide ready → stand tall
        ready:      1000,   // stand tall: both sensors upright and still this long, unbroken → settle
        settle:     1000,   // stand tall: before the still-watch starts
        hold:       2200,   // the hold bar: calibrateTrunk reads the 2 s BEFORE the call, plus a tick's margin
        tick:       100,    // the hold's and the countdown's sampling interval
        demoTurn:   2500,   // the guide's demonstration turn, each way
        demoPause:  600,    // the guide's pause at the end of the turn
        arm:        1000,   // a turn: both sensors still this long, unbroken → armed (tuned::trunkRoutine::kArmStillMs)
        returnStill: 750,   // a turn: back near the armed heading and still this long → returned (kReturnStillMs)
        turnTimeout: 20000, // a turn that started but never returned: ended and checked after this (kTurnTimeoutMs)
        retry:      2500,   // a failure on screen before the step starts again
        next:       800,    // a passed step → the next one
        waitLog:    1000,   // at most one `trunk wait` app-log line per this, while ready or the hold waits
        scale:      1
    })
    property var pace: defaultPace
    readonly property var _p: {
        var o = {}, k
        for (k in defaultPace) o[k] = defaultPace[k]
        if (pace) for (k in pace) o[k] = pace[k]
        return o
    }
    function _ms(name) { return Math.round(_p[name] * _p.scale) }

    // ── Outputs ───────────────────────────────────────────────────────────────
    // "idle" | "intro" | "standTall" | "turnAway" | "turnThrough" | "done" | "failed"
    readonly property string stage:       _stage
    // Within a step: "ready" | "settle" | "hold" (stand tall); "demo" | "arming" | "armed" | "turning"
    // (a turn; "returned" / "timeout" only for the instant before the check); "passed" | "retry"
    // ("" outside one)
    readonly property string substage:    _sub
    readonly property bool   done:        calibrationDone
    readonly property bool   failed:      _stage === "failed"
    // The failure on screen, golfer-facing (the C++ records' words, plus the routine's hints);
    // kept through the repeat until the step passes. "error" (a sensor) | "warn" (the turn) | "".
    readonly property string message:     _message
    readonly property string messageKind: _messageKind
    readonly property real   holdProgress: calibrationDone ? 1.0 : Math.min(1.0, _holdMs / Math.max(1, _ms("hold")))
    // A turn: the chest's live turn from its armed heading, and the furthest it has been (°, unsigned
    // peak; NaN = no reading / not armed).
    readonly property real   turnDeg:     _turnDeg
    readonly property real   turnPeakDeg: _turnPeakDeg
    // A turn: the TURN-BACK cue has fired in this attempt (header) — the golfer is told to come back.
    readonly property bool   turnBackDue: _turnBackDue
    // Why the routine is waiting, one line per reason — stand tall's ready and hold ("Chest sensor isn't
    // upright", "Hip sensor is moving"), a turn's arming (the same, or no turn reading) and a turn
    // that has not started ("Waiting for you to turn…"). "" when nothing holds it up.
    readonly property string waitReason:  _waitReason
    // The last records: { pelvis, thorax } from calibrateTrunk; { away, through } from checkTurn.
    property var standTallResult: ({})
    property var turnResults:     ({})
    property bool calibrationDone: false

    signal completed()

    readonly property QtObject pelvisImu: segments && segments.pelvis ? segments.pelvis : null
    readonly property QtObject thoraxImu: segments && segments.thorax ? segments.thorax : null

    // ── State ─────────────────────────────────────────────────────────────────
    property bool   _running: false        // between begin() and stop() (outlives done and failed)
    property string _stage: "idle"
    property string _sub: ""
    property string _message: ""
    property string _messageKind: ""
    property real   _holdMs: 0
    property real   _readyMs: 0
    property real   _waitLogMs: 0
    property string _waitReason: ""
    property real   _turnMs: 0             // a turn: since it started (the timeout's clock)
    property real   _armMs: 0              // a turn: still, unbroken, while arming
    property real   _backMs: 0             // a turn: back and still, unbroken, after it started
    property real   _topMs: 0              // a turn: still beyond the start, unbroken, before the cue
    property real   _turnRef: NaN          // a turn: the chest's heading at arming
    property real   _turnDeg: NaN
    property real   _turnPeakDeg: NaN
    property string _anim: ""             // the guide's demonstration: "out" | "pause" | "back" | ""
    property string _retryTo: ""           // after a failed turn: "standTall" | "away" | "through"
    property bool   _turnBackDue: false    // the turn-back cue has fired, once per armed attempt
    property bool   _saidStart: false      // this attempt's start line was said: the turn-back cuts it off
    property bool   _turnBackPending: false // the turn-back line waits for the voice to stop (it had to stop)
    property bool   _spoke: false          // something was said since begin(): stop() silences it
    property bool   _awaitLatency: false   // said; its synthesis latency is logged when it arrives
    property string _latencyCue: ""        // …and which cue it was

    // Still enough to start, and to time the hold: each sensor's gyroDps (|device gyro|, its peak over
    // the last 250 ms — imu_instance.h). The stillness that counts is calibrateTrunk's own gate (the
    // mean gyro per axis over the 2 s, kTrunkStillGyroMaxDps 0.5 °/s — sway about a still stance has
    // no mean, so it passes there); this only keeps the routine from starting, and the bar from filling,
    // while the golfer is plainly moving. 10 °/s: the W0 desk recordings (sensors untouched, 26 Oct)
    // read a 250 ms peak of p99 ≤ 0.6 °/s, max 1.2 (one 10.6 °/s spike in ~14 000 ticks); a person
    // standing still sways a few °/s and a hand-held sensor adds tremor, so the gate sits well above
    // both; putting a sensor on, adjusting it or stepping is tens of °/s. Being a PEAK over 250 ms it is
    // already stricter than the old instantaneous poll at the same number, so it is not set lower.
    // NOT angularVelocityDps: a difference of fused quaternions over host-arrival gaps, 4–5 °/s p95
    // and up to ~30 °/s per sample on a desk.
    // Mirrors tuned::trunkRoutine::kStillGyroDps (lint W11), which a turn's arming and return share.
    readonly property real _stillDps: 10.0
    // A turn has started once the chest is this far (°) from its armed heading, and has returned once
    // back within the second (and still): tuned::trunkRoutine kTurnStartDeg / kTurnReturnDeg, the
    // reasons there.
    readonly property real _turnStartDeg: 20.0
    readonly property real _turnReturnDeg: 15.0
    // The turn-back cue fires once the chest's |live turn| reaches this (°):
    // tuned::trunkRoutine::kTurnBackCueDeg, the reason there (lint W11).
    readonly property real _turnBackCueDeg: 70.0
    // While armed and still, the armed heading follows the live one at up to this (°/s, routine time):
    // tuned::trunkRoutine::kArmDriftFollowDps, the reason there (lint W11).
    readonly property real _armDriftFollowDps: 0.5
    // Upright: sensor +Y (away from the USB end, so the USB end down) within the stand-tall gate of
    // up (kTrunkLongAxisMaxFromUpDeg, 35°, pp_tuned_constants.h — keep the two equal). A sensor within it also passes the face gate (40°).
    readonly property real _uprightMaxDeg: 35.0

    readonly property var _ourSteps: ["standTall", "turnAway", "turnThrough"]

    // The lines ever spoken (header): each turn step's start cue, and the turn-back cue.
    function _startLine() {
        return _stage === "turnAway" ? Spoken.trunkStartBackswing : Spoken.trunkStartFollowThrough
    }
    readonly property string _turnBackLine: Spoken.trunkTurnBack
    // Every line this routine may speak — the same array its step descriptor lists, so the setup
    // shell prepares exactly these in advance (spoken_instructions.js; ScreenSessionSetup).
    readonly property var spokenPhrases: Spoken.trunk

    function _log(text) { appLog.info("Calib", "trunk " + text) }
    // ONE line per transition (R10): the stage, the step within it, and why.
    function _go(stage, sub, why) {
        _stage = stage
        _sub = sub
        _waitReason = ""
        _log("stage=" + stage + (sub !== "" ? " step=" + sub : "") + (why ? " " + why : ""))
    }

    // ── Entry points ──────────────────────────────────────────────────────────
    // A fresh run: both sensors' calibrations cleared (an invalid solve leaves an instance untouched,
    // so a stale A/M must not be there to inherit), the recording begun unless one runs already.
    function begin() {
        _log("begin")
        // A Recalibrate mid-step: the open step ends here, not at the next step of its name; and
        // whatever was said for the run it replaces stops.
        if (_running) _closeStep()
        _stopSpeech()
        _reset()
        _running = true
        var missing = _missingSensor()
        if (missing !== "") { _fail(missing, "missing"); return }
        if (trunkCeremony && !trunkCeremony.recording) trunkCeremony.begin()
        if (guide) guide.trunkPose(0, 0, false)
        _go("intro", "")
    }

    // Leave: every timer off, the guide stopped, the open step closed on the recorder, the stage
    // back to idle (unless done). Nothing on the sensors is cleared or written; the recording goes
    // on (the golfer may come back — and the flow discards it if setup is left without a session).
    function stop(reason) {
        if (!_running) return
        var was = _stage
        _anim = ""
        _running = false
        if (guide) guide.cancel()
        _stopSpeech()
        _closeStep()
        if (!calibrationDone) _resetState()
        _log("stop reason=" + reason + " stage=" + was)
    }

    // Show the finished run WITHOUT re-running (R8): the host calls it when the draft's trunk outcome
    // still holds (both sensors fullyCalibrated). The records shown are the solves in force.
    function restore() {
        var p = pelvisImu, t = thoraxImu
        var tp = p ? p.trunkCalibration : null, tt = t ? t.trunkCalibration : null
        var solved = tp && tp.valid === true && tt && tt.valid === true
        _anim = ""
        standTallResult = { pelvis: tp || ({}), thorax: tt || ({}) }
        _message = ""
        _messageKind = ""
        _holdMs = _ms("hold")
        _clearTurn()
        calibrationDone = true
        _running = false
        if (guide) guide.trunkPose(0, 0, false)
        _stage = "done"
        _sub = ""
        _log("restore done=true solves=" + (solved ? "both" : "not both from stand tall"))
    }

    // ── Internals ─────────────────────────────────────────────────────────────
    function _missingSensor() {
        var p = pelvisImu, t = thoraxImu
        if (!p) return qsTr("No hip sensor in this session — choose one on the Sensors step")
        if (!t) return qsTr("No chest sensor in this session — choose one on the Sensors step")
        if (!p.imuConnected) return qsTr("Hip sensor is not connected — connect it on the Sensors step, then Recalibrate")
        if (!t.imuConnected) return qsTr("Chest sensor is not connected — connect it on the Sensors step, then Recalibrate")
        return ""
    }

    function _clearSensors() {
        var segs = [pelvisImu, thoraxImu]
        for (var i = 0; i < segs.length; ++i) {
            if (!segs[i]) continue
            segs[i].clearFunctionalCalibration()
            segs[i].clearCalibration()
        }
    }

    function _reset() {
        _clearSensors()
        _resetState()
    }

    function _resetState() {
        _anim = ""
        _stage = "idle"
        _sub = ""
        _message = ""
        _messageKind = ""
        _holdMs = 0
        _readyMs = 0
        _waitLogMs = 0
        _waitReason = ""
        _clearTurn()
        _retryTo = ""
        _turnBackDue = false
        _saidStart = false
        _turnBackPending = false
        _awaitLatency = false
        standTallResult = ({})
        turnResults = ({})
        calibrationDone = false
    }

    function _closeStep() {
        if (!trunkCeremony || !trunkCeremony.recording) return
        var open = trunkCeremony.currentStep
        if (_ourSteps.indexOf(open) >= 0) trunkCeremony.stepEnd(open)
    }

    function _endCeremony() {
        if (trunkCeremony && trunkCeremony.recording) trunkCeremony.end()
    }

    function _fail(text, why) {
        _anim = ""
        if (guide) guide.cancel()
        _closeStep()
        _message = text
        _messageKind = "error"
        _go("failed", "", why)
        _endCeremony()
    }

    // Stand tall: a fresh solve each try — the try before may have applied one sensor's A/M. It
    // starts at ready: nothing more happens until both sensors are on, upright and still.
    function _startStandTall() {
        _clearSensors()
        if (trunkCeremony) trunkCeremony.stepStart("standTall")
        if (guide) guide.trunkPose(0, 0, false)
        _holdMs = 0
        _readyMs = 0
        _clearTurn()
        _waitLogMs = _ms("waitLog")     // the first wait is logged at once
        _go("standTall", "ready")
    }

    // What keeps stand tall waiting: one line per sensor that is not upright, or (upright) moving.
    function _sensorReasons() {
        var out = []
        var segs = [[pelvisImu, qsTr("Hip sensor")], [thoraxImu, qsTr("Chest sensor")]]
        for (var i = 0; i < segs.length; ++i) {
            var imu = segs[i][0]
            if (!imu) continue
            if (!(imu.longAxisFromUpDeg <= _uprightMaxDeg))
                out.push(qsTr("%1 isn't upright").arg(segs[i][1]))
            else if (!(imu.gyroDps <= _stillDps))
                out.push(qsTr("%1 is moving").arg(segs[i][1]))
        }
        return out
    }

    // A tick of ready or the hold that has to wait: say why on screen, and in the app log at most once
    // per `waitLog` with both sensors' numbers (so a stall can be read from the log alone).
    function _waitTick(interval, reasons, extra) {
        _waitReason = reasons.join("\n")
        _waitLogMs += interval
        if (_waitLogMs < _ms("waitLog")) return
        _waitLogMs = 0
        var f = function(imu) {
            return imu ? "gyro=" + Number(imu.gyroDps).toFixed(1) + "dps long=" + Number(imu.longAxisFromUpDeg).toFixed(1) + "deg"
                       : "absent"
        }
        _log("wait step=" + _sub + " pelvis " + f(pelvisImu) + " thorax " + f(thoraxImu)
             + (extra ? " " + extra : "") + " — " + reasons.join("; "))
    }

    // The hold is full: both solves, from the 2 s just held.
    function _captureStandTall() {
        var p = pelvisImu.calibrateTrunk("pelvis")
        var t = thoraxImu.calibrateTrunk("thorax")
        if (trunkCeremony) {
            trunkCeremony.attachResult("standTall", p)
            trunkCeremony.attachResult("standTall", t)
            trunkCeremony.stepEnd("standTall")
        }
        standTallResult = { pelvis: p, thorax: t }
        var why = "pelvis=" + (p.valid ? "pass" : "fail") + " thorax=" + (t.valid ? "pass" : "fail")
        if (p.valid && t.valid) {
            _message = ""
            _messageKind = ""
            _go("standTall", "passed", why)
            return
        }
        var lines = []
        if (!p.valid) lines.push(p.failure || qsTr("Hip sensor did not pass stand tall"))
        if (!t.valid) lines.push(t.failure || qsTr("Chest sensor did not pass stand tall"))
        _message = lines.join("\n")
        _messageKind = "error"
        _retryTo = "standTall"
        _go("standTall", "retry", why)
    }

    // A turn, taught: the guide demonstrates (away = +, golfer-relative — CalibrationGuide) while the
    // golfer faces the screen and reads the one instruction. NOTHING is recorded yet: the step opens
    // at arming.
    function _startTurn(dir) {
        var name = dir === "away" ? "turnAway" : "turnThrough"
        var s = dir === "away" ? 1 : -1
        _clearTurn()
        _anim = "out"
        _go(name, "demo")
        if (guide) {
            guide.trunkPose(0, 0, true)
            guide.trunkAnimate(45 * s, 90 * s, true, _ms("demoTurn"))
        }
    }

    // After the demo (or a failed turn's verdict): wait for both sensors still, then arm. The step
    // name is the stage's; the guide holds address with the arms crossed.
    function _armTurn(dir) {
        var name = dir === "away" ? "turnAway" : "turnThrough"
        _anim = ""
        _clearTurn()
        _waitLogMs = _ms("waitLog")     // the first wait is logged at once
        if (guide) guide.trunkPose(0, 0, true)
        _go(name, "arming")
    }

    function _clearTurn() {
        _turnMs = 0
        _armMs = 0
        _backMs = 0
        _topMs = 0
        _turnRef = NaN
        _turnDeg = NaN
        _turnPeakDeg = NaN
    }

    // The chest's live heading: a finite number, or NaN (no reading — imu_instance.h trunkHeadingDeg).
    function _heading() {
        var t = thoraxImu
        var h = t ? Number(t.trunkHeadingDeg) : NaN
        return isFinite(h) ? h : NaN
    }
    // An angle difference (°) wrapped into (−180, 180].
    function _wrap180(d) {
        var w = ((d + 180) % 360 + 360) % 360 - 180
        return w === -180 ? 180 : w
    }
    function _fmt(v) { return isFinite(v) ? Number(v).toFixed(1) + "deg" : "none" }

    // Both sensors still (gyro), the reasons when not, for arming and the return.
    function _stillReasons() {
        var out = []
        var segs = [[pelvisImu, qsTr("Hip sensor")], [thoraxImu, qsTr("Chest sensor")]]
        for (var i = 0; i < segs.length; ++i)
            if (segs[i][0] && !(segs[i][0].gyroDps <= _stillDps))
                out.push(qsTr("%1 is moving").arg(segs[i][1]))
        return out
    }

    // Armed: the golfer stands still at address facing the screen. The step's recording begins here,
    // so checkTurn's reference (the step's first sample) is this address.
    function _arm() {
        var name = _stage
        _turnRef = _heading()
        _turnDeg = 0
        _turnPeakDeg = 0
        _turnMs = 0
        _backMs = 0
        _topMs = 0
        _turnBackDue = false               // a new attempt: its cues are due again
        _saidStart = false
        _turnBackPending = false
        if (trunkCeremony) trunkCeremony.stepStart(name)
        _waitLogMs = 0
        _go(name, "armed", "heading=" + _fmt(_turnRef))
        _waitReason = _waitingForTurn()
        // The START cue: he is at address, facing the screen, and may turn now.
        _speak("start", _startLine())
    }

    // What an armed step waits for, in golf terms.
    function _waitingForTurn() {
        return _stage === "turnAway" ? qsTr("Waiting for your backswing turn")
                                     : qsTr("Waiting for your follow-through turn")
    }

    // One tick of an armed or turning step: the live turn, then start / return / timeout.
    function _turnTick(interval) {
        var h = _heading()
        var d = isFinite(h) && isFinite(_turnRef) ? _wrap180(h - _turnRef) : NaN
        _turnDeg = d
        if (isFinite(d) && Math.abs(d) > _turnPeakDeg) _turnPeakDeg = Math.abs(d)

        if (_sub === "armed") {
            if (isFinite(d) && Math.abs(d) >= _turnStartDeg) {
                _waitReason = ""
                _go(_stage, "turning", "started peak=" + _fmt(_turnPeakDeg))
                _turnBackTick(interval, d)     // a fast turn can be past the cue on its first reading
                return
            }
            // Nothing has started. Still: the armed heading follows the live one at most
            // `_armDriftFollowDps` (routine time — `interval` is scaled ms), so a fused heading creeping
            // while the golfer stands at address never adds up to a start; a turn outruns it at once,
            // and the follow stops while he moves (W11; kArmDriftFollowDps says why a rate).
            if (isFinite(d) && _stillReasons().length === 0) {
                var cap = _armDriftFollowDps * interval / (1000 * _p.scale)
                _turnRef = _wrap180(_turnRef + Math.max(-cap, Math.min(cap, d)))
            }
            // Keep waiting, saying so (on screen, and the numbers once per waitLog).
            var why = isFinite(d) ? _waitingForTurn()
                                  : qsTr("Chest sensor gives no turn reading — waiting for one")
            _waitTick(interval, [why], "turn=" + _fmt(d) + " peak=" + _fmt(_turnPeakDeg))
            return
        }
        // Turning: the turn-back cue, once; then back within the return gate and both sensors still,
        // unbroken, ends it; so does the timeout. A tick with no reading is not "back".
        _turnBackTick(interval, d)
        _turnMs += interval
        var back = isFinite(d) && Math.abs(d) <= _turnReturnDeg && _stillReasons().length === 0
        _backMs = back ? _backMs + interval : 0
        if (_backMs >= _ms("returnStill")) {
            _go(_stage, "returned", "peak=" + _fmt(_turnPeakDeg) + " turn=" + _fmt(d))
            _checkTurn()
        } else if (_turnMs >= _ms("turnTimeout")) {
            _go(_stage, "timeout", "peak=" + _fmt(_turnPeakDeg) + " turn=" + _fmt(d))
            _checkTurn()
        }
    }

    // The TURN-BACK cue (header), once per attempt, on a turning tick: the chest's |turn| has reached
    // `_turnBackCueDeg`, or the golfer has stopped at the top short of it — both sensors still for
    // `returnStill` without a break, still beyond `_turnStartDeg` (a turn on its way back in under the
    // start is not "at the top"). A tick with no reading is neither.
    function _turnBackTick(interval, d) {
        if (_turnBackDue) return
        var a = Math.abs(d)
        var out = isFinite(d) && a >= _turnStartDeg
        _topMs = out && _stillReasons().length === 0 ? _topMs + interval : 0
        if (isFinite(d) && a >= _turnBackCueDeg)
            _cueTurnBack("reached " + Number(_turnBackCueDeg).toFixed(0) + "deg at turn=" + _fmt(d))
        else if (_topMs >= _ms("returnStill"))
            _cueTurnBack("stopped at the top at " + _fmt(a) + " turn=" + _fmt(d))
    }

    function _cueTurnBack(why) {
        _turnBackDue = true
        _log("cue turnBack " + why + " peak=" + _fmt(_turnPeakDeg))
        _speak("turnBack", _turnBackLine)
    }

    // The EXPERIMENTAL spoken instructions (header): a cue's line, only with the setting on and a voice
    // that is ready; otherwise skipped, the reason logged. The START line is skipped too while the voice
    // is speaking (TtsController.speak drops a line then). The TURN-BACK line has priority: whatever is
    // still playing — this attempt's start line (a cached line plays without the voice reading active,
    // so it is stopped whenever it was said), or anything else — is stopped, and the line said now, or
    // as soon as the voice reports it has stopped (_ttsIdle). The log line names the cue and says
    // whether its words were prepared in advance; how long they took to reach the speaker is logged
    // when the voice reports it (_ttsLatency).
    function _speak(cue, text) {
        if (!speakInstructions) return
        var turnBack = cue === "turnBack"
        var why = !ttsController                    ? "no voice (text-to-speech is not available)"
                : ttsController.ttsReady !== true   ? "voice not ready"
                : !turnBack && ttsController.ttsActive === true ? "voice busy"
                : ""
        if (why !== "") { _log("speak cue=" + cue + " skipped: " + why); return }
        if (turnBack && (_saidStart || ttsController.ttsActive === true)) {
            _log("speak cue=" + cue + " stops the voice first" + (_saidStart ? " (the start line)" : " (busy)"))
            ttsController.stopSpeaking()
            if (ttsController.ttsActive === true) {
                _turnBackPending = true
                _log("speak cue=" + cue + " waits for the voice to stop")
                return
            }
        }
        _say(cue, text)
    }

    function _say(cue, text) {
        // Prepared in advance (the setup shell asked ttsController to, ScreenSessionSetup) → it plays
        // at once; not prepared → it is synthesised now and arrives late by that.
        var prepared = ttsController.isPrepared(text) === true
        _log("speak cue=" + cue + " \"" + text + "\" (" + (prepared ? "prepared" : "not prepared — synthesising now") + ")")
        _spoke = true
        _awaitLatency = true
        _latencyCue = cue
        if (cue === "start") _saidStart = true
        ttsController.speak(text)
    }

    // Leave or Recalibrate: silence what this run said (only then — the voice is the app's).
    function _stopSpeech() {
        _awaitLatency = false
        _turnBackPending = false
        if (!_spoke) return
        _spoke = false
        if (ttsController) ttsController.stopSpeaking()
    }

    function _handHint() {
        return rightHanded
            ? qsTr("Set up as a right-handed golfer — if that is wrong, change it in the golfer's profile, then Recalibrate.")
            : qsTr("Set up as a left-handed golfer — if that is wrong, change it in the golfer's profile, then Recalibrate.")
    }

    // The turn is over (returned, or timed out): the recorder judges the whole step, from arming to
    // now (trunk_ceremony.h checkTrunkTurn).
    function _checkTurn() {
        var name = _stage
        var dir = name === "turnAway" ? "away" : "through"
        var rec = trunkCeremony
                  ? trunkCeremony.checkTurn(dir, !rightHanded)
                  : { valid: false, cause: "", wrongWay: false,
                      failure: qsTr("The turn could not be checked — nothing recorded it") }
        if (trunkCeremony) trunkCeremony.stepEnd(name)
        var tr = {}
        for (var k in turnResults) tr[k] = turnResults[k]
        tr[dir] = rec
        turnResults = tr

        if (rec.valid) {
            _message = ""
            _messageKind = ""
            if (dir === "away") _go(name, "passed", "turn=valid")
            else                _finish()
            return
        }
        var cause = rec.cause !== undefined ? rec.cause : ""
        var reseat = cause === "backwards" || cause === "swapped"
        var lines = [rec.failure || qsTr("The turn did not pass — once more")]
        if (rec.wrongWay === true) lines.push(_handHint())
        if (reseat) lines.push(qsTr("Then stand tall again: a sensor that has moved needs a fresh stand tall."))
        _message = lines.join("\n")
        _messageKind = (reseat || cause === "noData") ? "error" : "warn"
        _retryTo = reseat ? "standTall" : dir
        _go(name, "retry", "turn=fail cause=" + cause + (reseat ? " back=standTall" : ""))
    }

    function _finish() {
        calibrationDone = true
        _go("done", "", "turn=valid")
        _endCeremony()
        completed()
    }

    onActiveChanged: if (!active) stop("inactive")

    // ── Timers and connections (named properties: the tests find each by name) ──

    // The intro: the stand-tall pose on show, then stand tall — only once the guide can be watched.
    property Timer _introTimer: Timer {
        interval: r._ms("introStart")
        repeat:   false
        running:  r.active && r._running && r._stage === "intro"
                  && r.guide !== null && r.guide.ready
        onTriggered: r._startStandTall()
    }

    // Ready: hands-free — both sensors upright and still for `ready` without a break, then settle.
    // Every tick that is not counts as a wait (the reason on screen, the numbers in the log).
    property Timer _readyTimer: Timer {
        interval: r._ms("tick")
        repeat:   true
        running:  r.active && r._running && r._stage === "standTall" && r._sub === "ready"
                  && r.pelvisImu !== null && r.thoraxImu !== null
        onTriggered: {
            var reasons = r._sensorReasons()
            if (reasons.length > 0) {
                r._readyMs = 0
                r._waitTick(interval, reasons)
                return
            }
            r._waitReason = ""
            r._waitLogMs += interval
            r._readyMs += interval
            if (r._readyMs >= r._ms("ready")) r._go("standTall", "settle", "upright and still")
        }
    }

    property Timer _settleTimer: Timer {
        interval: r._ms("settle")
        repeat:   false
        running:  r.active && r._running && r._stage === "standTall" && r._sub === "settle"
        onTriggered: { r._holdMs = 0; r._waitLogMs = r._ms("waitLog"); r._go("standTall", "hold") }
    }

    // The hold bar: fills while BOTH sensors are still and upright; anything else empties it, and
    // says which sensor.
    property Timer _holdTimer: Timer {
        interval: r._ms("tick")
        repeat:   true
        running:  r.active && r._running && r._stage === "standTall" && r._sub === "hold"
                  && r.pelvisImu !== null && r.thoraxImu !== null
        onTriggered: {
            var reasons = r._sensorReasons()
            if (reasons.length > 0) {
                r._holdMs = 0
                r._waitTick(interval, reasons)
                return
            }
            r._waitReason = ""
            r._waitLogMs += interval
            r._holdMs += interval
            if (r._holdMs >= r._ms("hold")) r._captureStandTall()
        }
    }

    // The demonstration's pause at the end of the turn, then back to address.
    property Timer _demoPauseTimer: Timer {
        interval: r._ms("demoPause")
        repeat:   false
        running:  r.active && r._running && r._sub === "demo" && r._anim === "pause"
        onTriggered: {
            r._anim = "back"
            if (r.guide) r.guide.trunkAnimate(0, 0, true, r._ms("demoTurn"))
        }
    }

    // The demonstration advances on the guide's finished(), never on a parallel clock (a stalled
    // renderer delays the chain instead of being outrun by it — WitmotionArmRoutine's rule).
    property Connections _guideFinished: Connections {
        target:  r.guide
        enabled: r.active && r._running && r._anim !== ""
        function onFinished() {
            if (r._anim === "out") {
                r._anim = "pause"
            } else if (r._anim === "back") {
                r._armTurn(r._stage === "turnAway" ? "away" : "through")
            }
        }
    }

    // Arming: both sensors still for `arm` without a break, and a turn reading to measure from.
    // Every tick that is not counts as a wait (the reason on screen, the numbers in the log).
    property Timer _armTimer: Timer {
        interval: r._ms("tick")
        repeat:   true
        running:  r.active && r._running && r._sub === "arming"
                  && (r._stage === "turnAway" || r._stage === "turnThrough")
        onTriggered: {
            var reasons = r._stillReasons()
            if (!isFinite(r._heading())) reasons.push(qsTr("Chest sensor gives no turn reading — waiting for one"))
            if (reasons.length > 0) {
                r._armMs = 0
                r._waitTick(interval, reasons)
                return
            }
            r._waitReason = ""
            r._waitLogMs += interval
            r._armMs += interval
            if (r._armMs >= r._ms("arm")) r._arm()
        }
    }

    // Armed and turning: the sensors start and end the step (no window, no countdown).
    property Timer _turnTimer: Timer {
        interval: r._ms("tick")
        repeat:   true
        running:  r.active && r._running && (r._sub === "armed" || r._sub === "turning")
                  && (r._stage === "turnAway" || r._stage === "turnThrough")
        onTriggered: r._turnTick(interval)
    }

    // A passed step → the next.
    property Timer _nextTimer: Timer {
        interval: r._ms("next")
        repeat:   false
        running:  r.active && r._running && r._sub === "passed"
        onTriggered: {
            if (r._stage === "standTall")     r._startTurn("away")
            else if (r._stage === "turnAway") r._startTurn("through")
        }
    }

    // A failed step, on screen for `retry`, then again — or back to stand tall. A failed turn's
    // verdict is read facing the screen: the end condition (back, still) put the golfer there.
    property Timer _retryTimer: Timer {
        interval: r._ms("retry")
        repeat:   false
        running:  r.active && r._running && r._sub === "retry"
        onTriggered: {
            // A failed turn re-arms by itself (no demo again, no click); the verdict stays on screen.
            if (r._retryTo === "standTall") r._startStandTall()
            else                            r._armTurn(r._retryTo)
        }
    }

    // The spoken line's synthesis latency, logged once when the voice reports it (EXPERIMENTAL).
    property Connections _ttsLatency: Connections {
        target:  r.ttsController
        enabled: r.active && r._running && r._awaitLatency
        function onLastTtsLatencyMsChanged() {
            r._awaitLatency = false
            r._log("speak cue=" + r._latencyCue + " latency=" + Math.round(Number(r.ttsController.lastTtsLatencyMs)) + "ms")
        }
    }

    // The turn-back line that had to stop the voice first: said when the voice reports it has stopped —
    // while the golfer is still out on that turn (once he is back, it is no longer an instruction).
    property Connections _ttsIdle: Connections {
        target:  r.ttsController
        enabled: r.active && r._running && r._turnBackPending
        function onTtsActiveChanged() {
            if (r.ttsController.ttsActive === true) return
            r._turnBackPending = false
            if (r._sub === "turning") r._say("turnBack", r._turnBackLine)
            else                      r._log("speak cue=turnBack dropped: the turn ended before the voice stopped")
        }
    }

    // A trunk sensor that drops its link mid-run fails the run (it cannot be judged; Recalibrate).
    readonly property bool _watching: _stage === "intro" || _stage === "standTall"
                                      || _stage === "turnAway" || _stage === "turnThrough"
    property Connections _pelvisLink: Connections {
        target:  r.pelvisImu
        enabled: r.active && r._running && r._watching
        function onImuConnectedChanged() {
            if (r.pelvisImu && !r.pelvisImu.imuConnected)
                r._fail(qsTr("Hip sensor lost its connection — reconnect it on the Sensors step, then Recalibrate"),
                        "pelvis disconnected")
        }
    }
    property Connections _thoraxLink: Connections {
        target:  r.thoraxImu
        enabled: r.active && r._running && r._watching
        function onImuConnectedChanged() {
            if (r.thoraxImu && !r.thoraxImu.imuConnected)
                r._fail(qsTr("Chest sensor lost its connection — reconnect it on the Sensors step, then Recalibrate"),
                        "thorax disconnected")
        }
    }
}
