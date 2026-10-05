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

// The HackMotion (wG3) arm calibration — the DEVICE's routine, driven through libwrist
// (design §4.7). Moved out of ImuCalibrationFlow.qml with its choreography, guards and
// strings unchanged.
//
// Forearm horizontal → one continuous ~30° raise across the chest → the device applies
// its own transform → reference pose → presence check. The order is fixed and enforced
// BY THE LIBRARY; we issue markers and read state. There is nothing to solve host-side,
// nothing to store, and no mount check to run — so the refinement, the arm-down/T-pose
// captures and the AngleWarning belong to the Witmotion routine ONLY.
//
// ⚠ Nothing about the HackMotion calibration is persisted, ever: a plain BLE disconnect
// destroys it (measured 0.70° → 18.80° at the same pose, strap untouched), the library
// makes resume un-expressible, and this UI matches that rather than papering over it.
//
// NON-VISUAL. Inputs are injected: the guide (CalibrationGuide, R9), the segments
// { device, lowerArm, palm } and the pace.
//
// ⚠ UNIT vs DEVICE. `segments.lowerArm` / `palm` are the VIZ objects (HmUnit) — they
// carry the quaternions; `segments.device` is the PERIPHERAL (HmInstance) — every
// wr_calibration_* call, imuConnected, streaming and the calibration state live there.
// They answer to QML by name and they are DIFFERENT OBJECTS. Cube/viz and per-sensor
// quaternions are unit work; calibration is device work.
//
// ⚠ R3 / R7: every Timer.running and Connections.enabled is ANDed with `active`, and so
// is the reaction to the device's phase: a routine that is not active neither calls the
// device nor reacts to it, and a stopped one is inert. Active going false is
// stop("inactive"), which aborts a live device routine EXACTLY ONCE.
//
// Every stage change, marker and entry point writes ONE line to the app log (R10).
import QtQuick

QtObject {
    id: r

    // ── Inputs ────────────────────────────────────────────────────────────────
    property bool active: false
    property var  guide: null
    property var  segments: null          // { device, lowerArm, palm }

    // Every duration, in ms, and `scale` (1 in the app; tests set it, lint W7).
    readonly property var defaultPace: ({
        hmStart:      1500,   // read window before beginCalibration
        settle:       2000,   // _hmSettleMs — pose-0 settle before `a2 00`
        raiseAnim:    3000,   // _hmRaiseAnimMs
        raiseSettle:  500,    // _hmRaiseSettleMs
        returnAnim:   1500,   // _hmReturnAnimMs
        refSettle:    1500,   // _hmRefSettleMs
        presenceWait: 6000,   // _hmPresenceWaitMs
        scale:        1
    })
    property var pace: defaultPace
    readonly property var _p: {
        var o = {}, k
        for (k in defaultPace) o[k] = defaultPace[k]
        if (pace) for (k in pace) o[k] = pace[k]
        return o
    }
    function _ms(name) { return Math.round(_p[name] * _p.scale) }

    // The PERIPHERAL (see the header) and the lower-arm UNIT.
    readonly property QtObject leadDevice: segments && segments.device ? segments.device : null
    readonly property QtObject leadImu:    segments && segments.lowerArm ? segments.lowerArm : null

    // ── Outputs ───────────────────────────────────────────────────────────────
    readonly property string stage: calibrationDone ? "hm5"
                                  : !_running       ? "idle"
                                  : hmStep === 9    ? "hmStopped"
                                  :                   "hm" + hmStep
    readonly property bool   done:        calibrationDone
    readonly property bool   failed:      calibrationFailed
    readonly property bool   mountFailed: false
    readonly property int    phase:       0
    readonly property string message:     hmFailMsg
    readonly property string messageKind: hmFailMsg !== "" ? hmFailKind : ""

    signal completed()

    // True between begin() and stop()/restore().
    property bool _running: false

    property bool calibrationDone:   false
    // Set when the routine stops on a failure. calibrationFailed is reused so the shared
    // StatusBadge and the host gates read correctly without a second flag.
    property bool calibrationFailed: false

    // The rest pose the shared reset leaves the guide in until the routine poses pose 0.
    readonly property quaternion leadArmDownQuat: Qt.quaternion(0.7071, 0.7071, 0, 0)

    // ═══ HackMotion — the device-native routine ════════════════════════════
    //
    // Library enum values, from libwrist's wrist/event.h and
    // wrist/types.h. QML cannot see the C enums, so the integers are
    // named ONCE here rather than spelled at each comparison.
    // ⚠ QML forbids a property name beginning with a capital, so these cannot
    // carry the library's own spelling; it is in the trailing comment on every
    // line so the mapping stays greppable both ways.
    readonly property int calpIdle:            0   // WR_CALP_IDLE
    readonly property int calpAwaitHorizontal: 1   // WR_CALP_AWAIT_HORIZONTAL
    readonly property int calpMarkingPose0:    2   // WR_CALP_MARKING_POSE0
    readonly property int calpObservingRaise:  3   // WR_CALP_OBSERVING_RAISE
    readonly property int calpMarkingPose1:    4   // WR_CALP_MARKING_POSE1
    readonly property int calpApplying:        5   // WR_CALP_APPLYING
    readonly property int calpVerifying:       6   // WR_CALP_VERIFYING
    readonly property int calpComplete:        7   // WR_CALP_COMPLETE
    readonly property int calpAborted:         8   // WR_CALP_ABORTED

    readonly property int calUnknown:      0   // WR_CAL_UNKNOWN
    readonly property int calUncalibrated: 1   // WR_CAL_UNCALIBRATED
    readonly property int calCalibrated:   2   // WR_CAL_CALIBRATED
    readonly property int calLost:         3   // WR_CAL_LOST — never appears live

    // wr_calibration_abort_reason. Read off the header rather than any summary:
    // it has exactly SIX enumerators, so NO_RESULT — the last one — is 5.
    readonly property int abortNone:         0   // WR_CAL_ABORT_NONE
    readonly property int abortCaller:       1   // WR_CAL_ABORT_CALLER
    readonly property int abortRaiseTooSlow: 2   // WR_CAL_ABORT_RAISE_TOO_SLOW
    readonly property int abortStreamLost:   3   // WR_CAL_ABORT_STREAM_LOST
    readonly property int abortLinkLost:     4   // WR_CAL_ABORT_LINK_LOST
    readonly property int abortNoResult:     5   // WR_CAL_ABORT_NO_RESULT

    // wr_status, the few a calibration call can be refused with.
    readonly property int errInvalidState:  -2   // WR_ERR_INVALID_STATE
    readonly property int errLinkDown:     -12   // WR_ERR_LINK_DOWN
    readonly property int errNoStream:     -14   // WR_ERR_NO_STREAM
    readonly property int errBusy:         -18   // WR_ERR_BUSY

    // Our step in the choreography. The DEVICE's phase is the authority on
    // where the library is; this only distinguishes the sub-steps a phase
    // cannot — before begin, and the reference-pose wait against the
    // confirmation readout, both of which sit inside VERIFYING/COMPLETE.
    //   0 ready   1 pose 0 (horizontal)   2 raise   3 applying
    //   4 reference pose   5 confirmed    9 stopped, needs an explicit re-run
    property int  hmStep: 0
    // True between confirmReferencePose() and the presence values landing. The
    // call RETURNS BEFORE THE MEASUREMENT EXISTS, so this is the wait on the
    // measurement, not on the call.
    property bool hmAwaitingPresence: false
    property string hmFailMsg:  ""
    property string hmFailKind: "error"    // "error" | "warn"
    // The calibration was destroyed under us (link drop). Distinct from a
    // failed attempt: there is nothing to resume and nothing was stored.
    property bool hmInvalidated: false

    // The library's phase, mirrored as a declarative dependency so the
    // transitions are edge-triggered by the device rather than by our timers.
    readonly property int hmPhase: leadDevice !== null ? leadDevice.calibrationPhase : calpIdle
    // ⚠ R7: only an ACTIVE routine reacts to the device, and only to a run it began — a
    // stopped routine is inert (the abort's own ABORTED/COMPLETE must not rewrite it).
    onHmPhaseChanged: if (r.active && r._running) _hmOnPhase(hmPhase)

    // Which guide animation is in flight ("hmRaise"/"hmReturn") — the chain advances on
    // the guide's finished() signal and nowhere else.
    property string _animStage: ""

    // ── Pacing ────────────────────────────────────────────────────────────
    readonly property int _hmSettleMs:      _ms("settle")   // pose-0 settle before `a2 00`
    // ⚠ THE GUIDE ANIMATION IS FUNCTIONAL, NOT DECORATIVE: the device watches
    // the raise CONTINUOUSLY from the instant it enters OBSERVING_RAISE, so the
    // animation paces the athlete and the elapsed time between the two markers
    // becomes OURS to control rather than the athlete's. 3000 ms + a 500 ms
    // settle lands the second marker at ~3.5 s, comfortably inside the
    // library's 6 s calibration_raise_limit_us default.
    //
    // ⚠ DO NOT RAISE THAT LIMIT AND DO NOT ADD A WALL-CLOCK FALLBACK that
    // confirms the raise anyway. If a stalled renderer overruns it the library
    // aborts with WR_CAL_ABORT_RAISE_TOO_SLOW, and a legible failure the coach
    // can repeat is worth more than a marker fired at an arm that never moved —
    // which is precisely the attempt that scores BEST on the presence check
    // (0.70°, §8.2). The device imposes no deadline of its own: one measured
    // attempt took 15.6 s and was still applied.
    readonly property int _hmRaiseAnimMs:   _ms("raiseAnim")
    readonly property int _hmRaiseSettleMs: _ms("raiseSettle")
    readonly property int _hmReturnAnimMs:  _ms("returnAnim")   // paced return to pose 0
    readonly property int _hmRefSettleMs:   _ms("refSettle")    // stillness settle before the check
    // Bound on the wait for the presence measurement. The library averages up
    // to 64 live samples, and a resting wrist streams at ~25 Hz → ~2.6 s worst
    // case, so this is a ceiling with margin, not a policy.
    readonly property int _hmPresenceWaitMs: _ms("presenceWait")

    // ── Did the raise actually happen? ────────────────────────────────────
    // ⚠ THE DEVICE WILL REPORT A CALIBRATION FOR AN ATTEMPT WHERE NOTHING MOVED,
    // AND THAT IS NOT A BUG WE CAN FIX IN THE LIBRARY. §8.2 measured the presence
    // check at 1.96° for the correct routine, 6.10° for a raise about the wrong
    // axis, and 0.70° — the BEST score of the three — for pose 1 marked without
    // moving at all. The check tests the ZEROING, which cannot fail; the raise is
    // what determines the anatomical axis, and the presence angle is blind to it.
    // So wr_session_calibration_state() reaches WR_CAL_CALIBRATED for a routine
    // the athlete never performed, and a UI that pings on that alone tells a coach
    // their sensor is calibrated when its frame is undetermined.
    //
    // The one thing that CAN see it is the stream we are already receiving. The
    // lower-arm unit sits on the forearm, which is the segment this routine
    // rotates, so the angular travel of its own reported orientation between the
    // two markers IS the raise. That is an independent measurement, not a
    // reinterpretation of the presence angle — and §8.2 names exactly this gap
    // ("the payload carries the one thing the presence check is blind to —
    // whether the raise happened at all, and about which axis").
    //
    // ⚠ IT GATES BEFORE `a2 01`, NOT AFTER. Below the threshold we ABORT instead
    // of marking pose 1: before 0x94 nothing has been applied, so the device is
    // left alone rather than given a transform we know is undetermined.
    property var  _hmRaiseStartQuat: null
    property real _hmRaiseTravelDeg: Number.NaN
    // §8.2's separable ~30° against its unseparable 4.2°/7.6° attempts. Sits
    // between the two populations with margin either way.
    readonly property real _hmMinRaiseTravelDeg: 15.0

    // One-shot timers: each runs while `active && _running && <armed>`; arming is the
    // start()/restart() the flow used to call.
    property bool _hmHorizontalSettleArmed: false
    property bool _hmRaiseConfirmArmed:     false
    property bool _hmRefSettleArmed:        false
    property bool _hmPresenceWaitArmed:     false

    function _log(text) { appLog.info("Calib", "hackmotion " + text) }
    // Created idle; every stage the routine moves to after that is one line.
    property bool _created: false
    Component.onCompleted: _created = true
    onStageChanged: if (_created) _log("stage=" + stage)
    onCalibrationDoneChanged: if (calibrationDone) r.completed()

    // ── Entry points ──────────────────────────────────────────────────────────
    // A fresh run. ⚠ Nothing to clear host-side — the device holds the calibration
    // and clearCalibration()/setNominalCalibration() do not exist on it. An in-flight
    // routine is aborted so a Recalibrate always starts from a known state.
    function begin() {
        _log("begin")
        _hmAbortIfActive()
        _hmResetState()
        _animStage        = ""
        if (guide) guide.pose(leadArmDownQuat, Qt.quaternion(1, 0, 0, 0))
        calibrationFailed = false
        calibrationDone   = false
        _running          = true
    }

    // Leave: every timer off, the guide stopped, a live device routine aborted ONCE (with
    // today's wording, see _hmAbortIfActive), the stage back to idle unless done. The
    // abort's message is kept as the routine's last word.
    function stop(reason) {
        if (!_running) return
        var was = stage
        _hmDisarm()
        if (guide) guide.cancel()
        var aborted = _hmAbortIfActive()
        _running = false
        if (!calibrationDone) {
            hmStep             = 0
            hmAwaitingPresence = false
            calibrationFailed  = false
            _animStage         = ""
        }
        _log("stop reason=" + reason + " stage=" + was + (aborted ? " aborted" : ""))
    }

    // The host's reset(): what begin() does (an in-flight device routine aborted, the
    // state cleared), without starting a run.
    function reset() {
        _log("reset")
        _running = false
        if (guide) guide.cancel()
        _hmAbortIfActive()
        _hmResetState()
        _animStage        = ""
        calibrationFailed = false
        calibrationDone   = false
    }

    // Show complete from the DEVICE's live state; never re-runs. There is nothing
    // host-side to restore — the DEVICE holds the calibration and only its own state
    // says whether one is live. So the restore is a read of that state, never a
    // replay of stored fields, and it must not synthesise a Witmotion phase-2
    // completion.
    function restore() {
        _hmDisarm()
        var dev = leadDevice
        hmStep          = (dev && dev.calibrationState === calCalibrated) ? 5 : 0
        calibrationDone = (hmStep === 5)
        _running        = false
        _log("restore done=" + calibrationDone)
    }

    function _hmDisarm() {
        _hmHorizontalSettleArmed = false
        _hmRaiseConfirmArmed     = false
        _hmRefSettleArmed        = false
        _hmPresenceWaitArmed     = false
    }

    function _hmUnitQuat() {
        var u = leadImu
        if (!u) return null
        var q = Qt.quaternion(u.quatW, u.quatX, u.quatY, u.quatZ)
        // A unit that has not delivered a sample yet reads as identity; treat
        // that as "cannot measure" rather than as a real orientation.
        if (q.scalar === 1 && q.x === 0 && q.y === 0 && q.z === 0) return null
        return q
    }

    // Angle between two orientations, degrees. ⚠ Convention-blind by design —
    // this asks only HOW FAR, never about direction or axis, which is all the
    // "did it move" question needs.
    function _hmQuatAngleDeg(a, b) {
        var dot = Math.abs(a.scalar*b.scalar + a.x*b.x + a.y*b.y + a.z*b.z)
        return 2 * Math.acos(Math.min(1, dot)) * 180 / Math.PI
    }

    function _hmResetState() {
        // ⚠ hmStartTimer is NOT stopped here: its `running` is a binding on
        // hmStep, and the file's idiom is to leave binding-driven timers to
        // their bindings.
        _hmDisarm()
        hmStep             = 0
        hmAwaitingPresence = false
        hmFailMsg          = ""
        hmFailKind         = "error"
        hmInvalidated      = false
        _hmRaiseStartQuat  = null
        _hmRaiseTravelDeg  = Number.NaN
    }

    // Cancel/Recalibrate mid-routine. ⚠ abortCalibration() ALWAYS works — it is
    // a local state reset and writes nothing. But at VERIFYING the device's
    // transform is ALREADY APPLIED and no command reverses it, so aborting
    // there DECLINES THE PRESENCE CHECK; it does not undo a calibration, and
    // the wording must not claim it did. Returns true when it aborted.
    function _hmAbortIfActive() {
        var dev = leadDevice
        if (!dev || !dev.calibrationActive) return false
        var atVerifying = (dev.calibrationPhase === calpVerifying)
        _log("abortCalibration" + (atVerifying ? " at=verifying" : ""))
        dev.abortCalibration()
        if (atVerifying)
            _hmStop("warn", qsTr("Presence check declined. The sensor applied its transform "
                                 + "already and nothing reverses that — the calibration was NOT "
                                 + "undone, it is simply unverified. Re-run to check it."))
        else
            _hmStop("warn", qsTr("Calibration cancelled before the sensor applied anything."))
        return true
    }

    // Terminal state: the routine stopped and only an explicit Recalibrate
    // restarts it. calibrationFailed is reused so the shared StatusBadge and
    // the host gates read correctly without a second flag.
    function _hmStop(kind, msg) {
        // hmStartTimer is left to its binding (see _hmResetState) — hmStep 9
        // holds it off.
        _hmDisarm()
        hmStep             = 9
        hmAwaitingPresence = false
        hmFailKind         = kind
        hmFailMsg          = msg
        if (guide) guide.cancel()
        _animStage         = ""
        calibrationFailed  = true
        calibrationDone    = false
        _log("stopped kind=" + kind)
    }

    // ── Step 1 — preconditions, then `wr_calibration_begin()` ─────────────
    // ⚠ wr_calibration_begin() returns WR_ERR_NO_STREAM when no stream is
    // running, and there is DELIBERATELY no AWAIT_STREAM phase in the library:
    // the device observes a continuous raise, which two static samples cannot
    // supply. Under our one-stream cycle the stream comes up just after connect
    // and stays open, so this is normally satisfied — but say plainly what is
    // missing when it is not. Do NOT queue a begin for later and do NOT retry.
    function _hmBegin() {
        var dev = leadDevice
        if (!dev) return
        // ⚠ DEVICE, not unit: imuConnected and streaming live on the peripheral.
        if (!dev.imuConnected) {
            _hmStop("error", qsTr("The wrist sensor is not connected. Connect it on the IMUs "
                                  + "step, then tap Recalibrate."))
            return
        }
        if (!dev.streaming) {
            _hmStop("error", qsTr("The wrist sensor is connected but not streaming, and the "
                                  + "sensor has to WATCH the raise — it cannot calibrate from "
                                  + "two still poses. Wait for the stream, then tap Recalibrate."))
            return
        }
        hmFailMsg     = ""
        hmInvalidated = false
        hmStep        = 1
        // Pose the guide at pose 0 before the first marker. No animation: this
        // is the starting position, not a motion to follow.
        if (guide) {
            _animStage = ""
            guide.pose(guide.hmUpperArm, guide.hmForePose0)
        }
        // ⚠ Queued onto the I/O thread and returns NOTHING. A refusal arrives
        // only as calibrationCallRefused; the settle window that leads to
        // `a2 00` is started by the AWAIT_HORIZONTAL phase, not by this call.
        _log("beginCalibration")
        dev.beginCalibration()
    }

    // ── The library's phase transitions ───────────────────────────────────
    function _hmOnPhase(p) {
        var dev = leadDevice
        if (!dev) return

        if (p === calpAwaitHorizontal) {
            // The library is ready for `a2 00`. Give the athlete a settle
            // window and confirm from the timer — never from _hmBegin(), which
            // returns before the library has moved.
            hmStep = 1
            _hmHorizontalSettleArmed = false
            _hmHorizontalSettleArmed = true

        } else if (p === calpObservingRaise) {
            // ⚠ THE DEVICE IS WATCHING FROM THIS INSTANT. OBSERVING_RAISE is
            // the signal to start the raise — not a timer of ours — so the
            // guide animation starts here and nowhere else.
            hmStep = 2
            // The device is watching from now, so this is the instant to anchor
            // the travel measurement against.
            _hmRaiseStartQuat = _hmUnitQuat()
            _hmRaiseTravelDeg = Number.NaN
            if (guide) {
                // ⚠ The UPPER ARM DOES NOT MOVE — the elbow stays put and the
                // forearm elevates. Anchor both segments, then change only the
                // forearm target; BodyVizView's forearm onChanged handler is
                // what starts the slerp, because the arm target is unchanged.
                guide.pose(guide.hmUpperArm, guide.hmForePose0)
                _animStage = "hmRaise"
                guide.animate(guide.hmUpperArm, guide.hmForePose1, _hmRaiseAnimMs)
            }

        } else if (p === calpApplying) {
            hmStep     = 3
            if (guide) guide.pose(guide.hmUpperArm, guide.hmForePose1)   // held, no motion
            _animStage = ""

        } else if (p === calpVerifying) {
            // ⚠ VERIFYING MEANS THE TRANSFORM IS ALREADY APPLIED AND THE
            // PRESENCE CHECK IS NOT YET MEASURED. It is not success: no tick,
            // no Continue, nothing written. It is the cue for the reference
            // pose, which is NOT OPTIONAL — skipping it leaves the recording at
            // WR_CAL_UNKNOWN, and it also yields the anchor Phase D needs.
            hmStep = 4
            if (guide) {
                guide.pose(guide.hmUpperArm, guide.hmForePose1)
                _animStage = "hmReturn"
                guide.animate(guide.hmUpperArm, guide.hmForePose0, _hmReturnAnimMs)
            } else {
                _hmRefSettleArmed = false
                _hmRefSettleArmed = true
            }

        } else if (p === calpComplete) {
            // ⚠ COMPLETE is not calibrated, and WR_CAL_ABORT_CALLER is carried
            // on a transition to COMPLETE as well as to ABORTED — so
            // "abort_reason != NONE" is NOT "the routine failed". The verdict
            // is computed in _hmEvaluate() from the STATE.
            _hmEvaluate()

        } else if (p === calpAborted) {
            // ⚠ OUR OWN ABORT COMES BACK AS ABORTED/CALLER — AND MUST NOT REWORD OUR STOP. When
            // this routine has already stopped with its own message (the travel gate's abort,
            // a refusal, an invalidation — hmStep 9), the library's answer to that abort
            // arrives one queue step later as ABORTED with reason CALLER. Rewording it to
            // "Calibration cancelled." replaced the one message that said what went wrong (the
            // forearm moved 8°, not 30°) with one that says nothing. Every other reason, and
            // any abort while we are still running, is the device's news and keeps its text.
            if (hmStep === 9 && dev.calibrationAbortReason === abortCaller) {
                _log("aborted reason=caller after our own stop — message kept")
                return
            }
            // RAISE_TOO_SLOW is "that took too long — try again", not a fault in
            // the sensor, so it reads as a warning and the others as errors.
            _hmStop(dev.calibrationAbortReason === abortRaiseTooSlow ? "warn" : "error",
                    _hmAbortText(dev.calibrationAbortReason))
        }
    }

    // ── The verdict ───────────────────────────────────────────────────────
    // Called on every calibration state change and on COMPLETE, and idempotent
    // by construction. ⚠ calibrationDone comes from calibrationState ===
    // WR_CAL_CALIBRATED and from nothing else: never from phase === COMPLETE
    // (the device applies its transform for every attempt, including rejected
    // ones), and never from the presence angle being small (it INVERTS — the
    // attempt with no axis information scored best).
    function _hmEvaluate() {
        var dev = leadDevice
        if (!dev) return
        if (calibrationDone || hmStep === 9) return
        // ⚠ A VERDICT IS ONLY READ FOR A ROUTINE WE HAVE DRIVEN PAST THE RAISE.
        // The device's phase, state and abort reason persist from the PREVIOUS
        // attempt until the library moves them, and a state change does arrive
        // while a fresh routine is still being begun — so evaluating at step
        // 0/1/2 would read last attempt's COMPLETE as this one's outcome and
        // kill a run that is going fine.
        if (hmStep < 3) return

        // ⚠ CALIBRATED is reachable only through a presence measurement, which
        // only happens after confirmReferencePose() — so requiring step 4 here
        // costs nothing and removes the last way a stale state could be read as
        // this attempt's success.
        if (hmStep >= 4 && dev.calibrationState === calCalibrated) {
            _hmPresenceWaitArmed = false
            hmAwaitingPresence = false
            hmStep             = 5
            calibrationFailed  = false
            hmFailMsg          = ""
            calibrationDone    = true
            return
        }

        // Not calibrated. Anything before COMPLETE is still in flight.
        if (dev.calibrationPhase !== calpComplete) return

        if (hmStep < 4) {
            // Defensive: the routine finished without VERIFYING ever being
            // seen, so the reference pose was never offered. The library will
            // refuse a late confirm, so the honest outcome is a re-run.
            _hmStop("error", qsTr("The sensor finished before the reference pose could be "
                                  + "taken, so nothing checked the calibration. Tap "
                                  + "Recalibrate."))
            return
        }

        if (dev.calibrationAbortReason === abortCaller) {
            _hmStop("warn", qsTr("Presence check declined. The sensor's transform is applied "
                                 + "and nothing reverses that — it is unverified, not undone. "
                                 + "Re-run to check it."))
            return
        }
        // ⚠ THE MEASUREMENT NEVER HAPPENED. The library collected too few live
        // samples at the reference pose to average one (WR_WARN_PRESENCE_NOT_
        // MEASURED), and the phase reaches COMPLETE either way — so without this
        // flag a check that never ran would read as a success. Its own outcome,
        // distinct from "measured and passed" and from "declined".
        // ⚠ It also decides what presenceSamplesUsed MEANS on this path
        // (collected, not used), so the count is never shown without it.
        if (dev.presenceNotMeasured) {
            _hmStop("error", qsTr("Too few readings arrived at the reference pose to check the "
                                  + "calibration, so it is NOT confirmed — the sensor applied "
                                  + "its transform, but nothing verified it. Hold the first "
                                  + "position still and tap Recalibrate."))
            return
        }
        // presenceSamplesUsed > 0 proves the measurement LANDED (the presence
        // event may arrive after the phase event, so a bare state read here
        // would flash a false failure). With a measurement in hand and the
        // state still not CALIBRATED, the check did not pass.
        if (dev.presenceSamplesUsed > 0)
            _hmStop("error", qsTr("The check at the reference pose did not confirm a "
                                  + "calibration on the sensor. Re-seat nothing — just hold "
                                  + "the first position still and tap Recalibrate."))
        // Otherwise keep waiting; hmPresenceWaitTimer bounds it.
    }

    // The calibration is GONE. A plain BLE disconnect destroys it and the
    // library forces UNCALIBRATED on link-down, so this drives the routine back to
    // uncalibrated and asks for a re-run. ⚠ Nothing is resumed and nothing was
    // stored — there is deliberately no persistence to restore from.
    function _hmInvalidated() {
        hmInvalidated = true
        _hmStop("error", qsTr("The sensor's calibration is gone — a dropped link destroys it "
                              + "(measured 0.70° → 18.80° at the same pose with the strap "
                              + "untouched). It cannot be resumed or restored. Re-run the "
                              + "routine once the sensor is back."))
    }

    // ⚠ Every wr_calibration_* call is queued onto the I/O thread and returns
    // nothing; a refusal arrives ONLY here. NO_STREAM and BUSY must not collapse
    // into one generic error — they ask the coach for different things.
    function _hmRefused(status, call) {
        _log("refused call=" + call + " status=" + status)
        if (status === errNoStream)
            _hmStop("error", qsTr("The sensor stopped streaming, so it cannot watch the raise "
                                  + "(%1 was refused). Tap Recalibrate once data is flowing.")
                                 .arg(call))
        else if (status === errBusy)
            _hmStop("warn", qsTr("The sensor is busy retrieving swing data (%1 was refused) — "
                                 + "try again in a moment.").arg(call))
        else if (status === errLinkDown)
            _hmStop("error", qsTr("The sensor's link went down (%1 was refused).").arg(call))
        else if (status === errInvalidState)
            _hmStop("error", qsTr("The sensor was not in a state to accept %1. Tap Recalibrate "
                                  + "to start the routine from the beginning.").arg(call))
        else
            _hmStop("error", qsTr("The sensor refused %1 (status %2).").arg(call).arg(status))
    }

    function _hmAbortText(reason) {
        if (reason === abortRaiseTooSlow)
            // Not a sensor fault, and not the athlete's either — the guide sets
            // the pace, so this reads as "repeat it", never as an error.
            return qsTr("That took too long between the two positions — the sensor needs one "
                        + "continuous raise. Tap Recalibrate and follow the guide.")
        if (reason === abortStreamLost)
            return qsTr("The sensor's data stream stopped part-way through, so the raise could "
                        + "not be watched. Tap Recalibrate once data is flowing.")
        if (reason === abortLinkLost)
            return qsTr("The sensor's link dropped part-way through. Reconnect it, then run the "
                        + "routine again — a dropped link destroys any calibration.")
        if (reason === abortNoResult)
            return qsTr("The sensor did not answer in time, so no calibration was applied. Tap "
                        + "Recalibrate.")
        if (reason === abortCaller)
            return qsTr("Calibration cancelled.")
        return qsTr("The calibration routine stopped before it finished. Tap Recalibrate.")
    }

    onActiveChanged: if (!active) stop("inactive")

    // ── Device connections ─────────────────────────────────────────────────────
    // All four are reachable and each reads differently. ⚠ There is no return value
    // to check anywhere: the invokables are queued onto the I/O thread and return
    // nothing, so a refusal exists ONLY as calibrationCallRefused.
    // ⚠ The target is the PERIPHERAL (segments.device), never a unit. Live while active and
    // a run is in hand — begun, or restored as done (a link drop must still invalidate it).
    property Connections _deviceLink: Connections {
        target:  r.leadDevice
        enabled: r.active && (r._running || r.calibrationDone)

        function onCalibrationCallRefused(status, call) { r._hmRefused(status, call) }

        // The calibration is gone — drive back to uncalibrated and ask for a re-run.
        function onCalibrationInvalidated() { r._hmInvalidated() }

        // Phase, state, presence angle, spread and sample count all notify through
        // this one signal. The verdict is recomputed rather than latched, because
        // the presence event and the phase event are separate library events and
        // may land in either order.
        function onCalibrationStateChanged() { r._hmEvaluate() }

        // A plain disconnect DESTROYS the calibration (§8.3). The library forces
        // UNCALIBRATED and emits calibrationInvalidated for the same event; this
        // path exists so the routine still regresses if the transport signal is the
        // only one that reaches us.
        function onImuConnectedChanged() {
            var dev = r.leadDevice
            if (dev && !dev.imuConnected && (r.hmStep > 0 || r.calibrationDone))
                r._hmInvalidated()
        }
    }

    // ══ Timers ══════════════════════════════════════════════════════════════════
    // The chain is: this timer → beginCalibration() → [library: AWAIT_HORIZONTAL]
    // → settle → confirmHorizontal() → [library: OBSERVING_RAISE] → guide raise →
    // guide finished → settle → confirmRaise() → [library: APPLYING →
    // VERIFYING] → guide back to pose 0 → settle → confirmReferencePose() → wait on
    // the MEASUREMENT. Every arrow into the library is a queued call with no return
    // value, and every arrow out of it is a phase or state change.

    // Same ready gate as the Witmotion intro: a chain started against a
    // stalled renderer plays to nobody, and here that renderer is pacing a motion
    // the device is timing. The short interval is a read window for the first
    // instruction, not a settle — the settle happens after the library is ready.
    property Timer _hmStartTimer: Timer {
        id: hmStartTimer
        interval: r._ms("hmStart")
        repeat:   false
        running:  r.active && r._running
                  && r.hmStep === 0
                  && r.guide !== null && r.guide.ready
        onTriggered: r._hmBegin()
    }

    // Guide-animation completion chain.
    property Connections _guideFinished: Connections {
        target:  r.guide
        enabled: r.active && r._running
        function onFinished() {
            if (r._animStage === "hmRaise") {
                // ⚠ THE CHAIN ADVANCES HERE AND NOWHERE ELSE — never on a parallel
                // wall-clock timer, which keeps counting while a stalled renderer
                // shows nothing and would run the chain ahead of the athlete. The
                // stakes are higher on this branch than on the Witmotion one: the
                // DEVICE is measuring the very motion the guide is pacing, and a
                // marker fired at an arm that never moved is the attempt that scores
                // BEST on the presence check (0.70°, §8.2) while carrying no axis
                // information at all.
                r._animStage = ""
                r._hmRaiseConfirmArmed = true   // short settle, then `a2 01`
            } else if (r._animStage === "hmReturn") {
                r._animStage = ""
                r.guide.pose(r.guide.hmUpperArm, r.guide.hmForePose0)   // held, no motion
                r._hmRefSettleArmed = true      // stillness settle, then the check
            }
        }
    }

    // Pose-0 settle, then `a2 00`. Started by the AWAIT_HORIZONTAL phase.
    property Timer _hmHorizontalSettleTimer: Timer {
        id: hmHorizontalSettleTimer
        interval: r._hmSettleMs
        repeat:   false
        running:  r.active && r._running && r._hmHorizontalSettleArmed
        onTriggered: {
            r._hmHorizontalSettleArmed = false
            var dev = r.leadDevice
            if (dev && r.hmStep === 1) {
                r._log("confirmHorizontal")
                dev.confirmHorizontal()
            }
        }
    }

    // Raise complete (the GUIDE has finished drawing it) + a short settle, then
    // `a2 01`. ⚠ No wall-clock fallback: if this never fires because the renderer
    // stalled, the library aborts with RAISE_TOO_SLOW and the coach repeats the
    // routine, which is worth more than a marker fired at a motionless arm.
    property Timer _hmRaiseConfirmTimer: Timer {
        id: hmRaiseConfirmTimer
        interval: r._hmRaiseSettleMs
        repeat:   false
        running:  r.active && r._running && r._hmRaiseConfirmArmed
        onTriggered: {
            r._hmRaiseConfirmArmed = false
            var dev = r.leadDevice
            if (!dev || r.hmStep !== 2) return

            // ⚠ MEASURE THE TRAVEL BEFORE MARKING POSE 1 — see _hmRaiseTravelDeg.
            var now = r._hmUnitQuat()
            if (r._hmRaiseStartQuat !== null && now !== null) {
                r._hmRaiseTravelDeg = r._hmQuatAngleDeg(r._hmRaiseStartQuat, now)
                if (r._hmRaiseTravelDeg < r._hmMinRaiseTravelDeg) {
                    // Nothing has been applied yet, so abort rather than hand the
                    // device a transform whose axis we know is undetermined.
                    r._log("abortCalibration travel=" + Math.round(r._hmRaiseTravelDeg))
                    dev.abortCalibration()
                    r._hmStop("warn", qsTr("Your forearm only moved %1° — the sensor needs about "
                                           + "%2° to work out which way your wrist bends, and it "
                                           + "cannot tell on its own that the movement was missing. "
                                           + "Nothing was applied. Follow the guide and tap "
                                           + "Recalibrate.")
                                          .arg(Math.round(r._hmRaiseTravelDeg))
                                          .arg(Math.round(r.guide ? r.guide.hmRaiseDeg : 30)))
                    return
                }
            }
            // Could not measure (no live unit, or no sample yet): proceed and let the
            // presence check speak. ⚠ Deliberately permissive — a measurement we
            // failed to take must not make the device unusable.
            r._log("confirmRaise travel=" + (isNaN(r._hmRaiseTravelDeg) ? "unmeasured"
                                                                        : Math.round(r._hmRaiseTravelDeg)))
            dev.confirmRaise()
        }
    }

    // Back at the reference pose and still, so the presence check may be measured.
    // ⚠ confirmReferencePose() RETURNS BEFORE THE MEASUREMENT EXISTS — WR_OK only
    // means the run started — so what follows is a wait on the presence values, not
    // on the call, bounded by hmPresenceWaitTimer.
    property Timer _hmRefSettleTimer: Timer {
        id: hmRefSettleTimer
        interval: r._hmRefSettleMs
        repeat:   false
        running:  r.active && r._running && r._hmRefSettleArmed
        onTriggered: {
            r._hmRefSettleArmed = false
            var dev = r.leadDevice
            if (!dev || r.hmStep !== 4) return
            r.hmAwaitingPresence = true
            r._log("confirmReferencePose")
            dev.confirmReferencePose()
            r._hmPresenceWaitArmed = false
            r._hmPresenceWaitArmed = true
        }
    }

    // The measurement never landed. Distinct from the library's own
    // presenceNotMeasured warning (which DID reach a verdict); this is the case
    // where nothing at all came back.
    property Timer _hmPresenceWaitTimer: Timer {
        id: hmPresenceWaitTimer
        interval: r._hmPresenceWaitMs
        repeat:   false
        running:  r.active && r._running && r._hmPresenceWaitArmed
        onTriggered: {
            r._hmPresenceWaitArmed = false
            r._hmEvaluate()      // it may have landed in the same instant
            if (!r.calibrationDone && r.hmStep === 4)
                r._hmStop("error", qsTr("The sensor never reported a check at the reference pose, "
                                        + "so the calibration is NOT confirmed. Hold the first "
                                        + "position still and tap Recalibrate."))
        }
    }
}
