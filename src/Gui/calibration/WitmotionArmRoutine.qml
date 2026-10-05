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

// The Witmotion arm calibration — OUR routine (design §4.7). Moved out of
// ImuCalibrationFlow.qml with its maths, holds, gates and strings unchanged.
//
// Arm-down + T-pose captures, stillness-gated, then the abduction refinement and the
// mount-validation gate. It calls ImuInstance-only methods (clearCalibration,
// setNominalCalibration, refineMountAboutLongAxis, calibArmDown, mountDeviationDeg,
// calibrationAngleValid), none of which exist on a HackMotion — which is why the host
// picks this routine or HackMotionArmRoutine by vendor and never mixes them.
//
// NON-VISUAL. Inputs are injected: the guide (CalibrationGuide, R9), the segments
// { a, b, c } (the forearm, hand and upper-arm UNITS — for a Witmotion each unit is its
// own device), and the pace. ⚠ R3: every Timer.running and Connections.enabled below is
// ANDed with `active` — never `visible`, never a step index. A routine runs only while
// `active && <begin() called and stop() not>`; active going false is stop("inactive").
//
// Every stage change and every entry point writes ONE line to the app log (R10).
import QtQuick

QtObject {
    id: r

    // ── Inputs ────────────────────────────────────────────────────────────────
    property bool active: false
    property var  guide: null
    property var  segments: null          // { a, b, c } — A forearm (anchor), B hand, C upper arm
    property bool rightHanded: true

    // Every duration, in ms, and `scale` (1 in the app; tests set it, lint W7).
    readonly property var defaultPace: ({
        introStart:        3000,   // after the guide is ready, before the intro guide
        introAnim:         3000,   // the intro guide, up and then down
        introReady:        2000,   // settle before phase 1
        minHold:           2000,   // phase-1 settle before the still-watch starts
        hold:              2000,   // both stillness holds (_captureHoldMs)
        tick:              100,    // the hold timers' sampling interval
        captureTransition: 800,    // arm-down captured → raise guide
        raiseAnim:         1500,   // the raise guide
        raiseReady:        2000,   // settle before phase 2
        scale:             1
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
    readonly property string stage: {
        if (calibrationDone)   return "done"
        if (mountFailed)       return "mountFailed"
        if (calibrationFailed) return "failed"
        if (!_running)         return "idle"
        if (calibPhase === 0)  return "intro"
        if (calibPhase === 1) {
            if (!_armDownCaptured) return "phase1"
            if (_animStage === "raise" || _raiseReadyArmed) return "raise"
            return "captured"
        }
        return "phase2"
    }
    readonly property bool   done:        calibrationDone
    readonly property bool   failed:      calibrationFailed
    readonly property int    phase:       calibPhase
    readonly property string message:     mountFailed ? mountFailMsg : ""
    readonly property string messageKind: (mountFailed || calibrationFailed) ? "error" : ""
    readonly property real   captureHoldMs: _captureHoldMs

    signal completed()

    // True between begin() and stop() (it outlives done and the failures, which are
    // stages of a run; only stop() and restore() end one).
    property bool _running: false

    // ── State (relocated from the flow's `d`) ─────────────────────────────────
    // phase 0 — user holds lead arm straight down; wait for IMU stable
    // phase 1 — animated guide: arm moves from down → T-pose
    // phase 2 — user raises arm to T-pose; hold to capture
    property int  calibPhase:    0
    property real phaseProgress: 0.0   // 0–1 for phase 2 hold timer

    // Captured reference quaternions from the lead-arm IMU.
    property var  calibArmDownQuat:  null   // phase 0 — arm relaxed at side
    property var  calibArmTPoseQuat: null   // phase 2 — arm raised to T-pose
    property bool calibrationDone:   false
    // Diagnostic — Euler angles at each calibration capture (cleared by _reset)
    property var calibArmDownEuler:  null   // { roll, pitch, yaw } at arm-down capture
    property var calibArmTPoseEuler: null   // { roll, pitch, yaw } at T-pose capture

    // T-pose seed quaternions (match BodyPoseAdapter pre-seed values).
    readonly property quaternion tPoseQuat: r.rightHanded
        ? Qt.quaternion( 0.9948, -0.0105, -0.0011,  0.1012)   // left arm
        : Qt.quaternion( 0.9948, -0.0105,  0.0011, -0.1011)   // right arm

    // Arm-down: shoulder-local +Z = world -Y (verified by computing
    // shoulder.rotation * (0,0,1) for both left and right shoulders).
    // Rotating armNode's +Y to shoulder-local +Z = R_x(+90°) for both arms.
    // The mirror symmetry of the shoulder nodes means both use the same value.
    readonly property quaternion leadArmDownQuat:  Qt.quaternion(0.7071, 0.7071, 0, 0)

    // ── Segments ──────────────────────────────────────────────────────────────
    // ⚠ UNITS. For a Witmotion the unit IS the peripheral, so imuConnected is asked of the
    // unit here exactly as the flow always did. (A HackMotion's HmUnit has no
    // imuConnected — which is why this routine is never given one.)
    // Hand (B) and upper-arm (C) are OPTIONAL for the Wrist session. The precise
    // calibration + mount check run on whatever is connected (A is the anchor).
    readonly property QtObject leadImu: segments && segments.a ? segments.a : null
    readonly property QtObject slotB:   segments && segments.b ? segments.b : null
    readonly property QtObject slotC:   segments && segments.c ? segments.c : null

    function _connectedSegs() {
        // [instance, armDownRef] for every connected segment; A always first.
        var out = []
        if (leadImu && leadImu.imuConnected) out.push([leadImu, _refA])
        if (slotB   && slotB.imuConnected)   out.push([slotB,   _refB])
        if (slotC   && slotC.imuConnected)   out.push([slotC,   _refC])
        return out
    }
    function _curQuat(i) { return i ? Qt.quaternion(i.quatW, i.quatX, i.quatY, i.quatZ) : null }
    // Small Y-rotation bringing the abducted-pose anatomical axis onto Z (= φ).
    function _phiFromAbduction(inst) {
        var q = inst.anatQuat
        var w = Math.min(1, Math.abs(q.scalar))
        var s = Math.sqrt(Math.max(0, 1 - w*w))
        if (s < 1e-4) return 0
        var sg = q.scalar >= 0 ? 1 : -1
        var mx = sg*q.x/s, mz = sg*q.z/s
        var pp = Math.atan2(mx, mz), pm = Math.atan2(-mx, -mz)
        var phi = Math.abs(pp) <= Math.abs(pm) ? pp : pm
        return phi * 180 / Math.PI
    }
    // Per-sensor arm-down reference quaternions (captured at phase-1 completion).
    property var _refA: null
    property var _refB: null
    property var _refC: null
    // Mount validation outcome (set at phase-2 completion).
    property bool   mountFailed: false
    property string mountFailMsg: ""

    // Phase 2: accumulate stable hold duration (target _captureHoldMs).
    property real stableAccumMs: 0.0

    // Stillness-gated capture tuning. Both capture phases (arm-down and
    // abduction) watch the IMU's instantaneous angular velocity and only
    // accumulate samples while the arm is held still; any motion above the
    // threshold resets the hold. The threshold is deliberately forgiving:
    // an arm held out at shoulder height sways/tremors more than a few °/s
    // (an earlier, tighter gate never settled → capture stalled), but stays
    // comfortably below mid-motion (30–100°/s+).
    readonly property real _stillThreshDps: 15.0   // deg/s — held-still ceiling
    readonly property real _captureHoldMs:  _ms("hold")   // ms of continuous stillness

    // Quaternion samples accumulated during each stillness-held capture window.
    property var _phase1Samples: []
    property var _phase2Samples: []

    // Phase 1 accumulator.
    property real phase1AccumMs: 0.0

    function _quatSlerp(a, b, t) {
        var dot = a.scalar * b.scalar + a.x * b.x + a.y * b.y + a.z * b.z
        if (dot < 0) { b = Qt.quaternion(-b.scalar, -b.x, -b.y, -b.z); dot = -dot }
        if (dot > 0.9995) {
            var q = Qt.quaternion(a.scalar + t * (b.scalar - a.scalar),
                                  a.x     + t * (b.x     - a.x),
                                  a.y     + t * (b.y     - a.y),
                                  a.z     + t * (b.z     - a.z))
            var len = Math.sqrt(q.scalar*q.scalar + q.x*q.x + q.y*q.y + q.z*q.z)
            return Qt.quaternion(q.scalar/len, q.x/len, q.y/len, q.z/len)
        }
        var theta0    = Math.acos(dot)
        var sinTheta0 = Math.sin(theta0)
        var s0 = Math.sin((1 - t) * theta0) / sinTheta0
        var s1 = Math.sin(      t * theta0) / sinTheta0
        return Qt.quaternion(s0 * a.scalar + s1 * b.scalar,
                             s0 * a.x     + s1 * b.x,
                             s0 * a.y     + s1 * b.y,
                             s0 * a.z     + s1 * b.z)
    }

    // Iterative slerp mean: slerp(acc, samples[i], 1/(i+1)) converges to
    // the uniform spherical mean when all samples cluster near each other.
    function _slerpAverage(samples) {
        if (samples.length === 0) return Qt.quaternion(1, 0, 0, 0)
        var acc = samples[0]
        for (var i = 1; i < samples.length; i++)
            acc = _quatSlerp(acc, samples[i], 1.0 / (i + 1))
        return acc
    }

    // Which guide animation is in flight ("introUp"/"introDown"/"raise") —
    // the chain advances on the guide's finished() signal,
    // never on a parallel wall-clock timer (which keeps counting while a
    // stalled renderer shows nothing, running the chain ahead of the user).
    property string _animStage: ""
    // Set when arm-down is captured; prevents the phase-1 timer from
    // re-triggering captureTransitionTimer during the raise animation.
    property bool _armDownCaptured: false
    // True only after phase1MinHoldTimer fires — gives the user a 2s settle
    // window after phase 1 begins before the stillness-gated capture starts.
    property bool _phase1MinHoldDone: false

    // Set when the lead IMU disconnects mid-calibration.
    property bool calibrationFailed: false

    // One-shot timers: each runs while `active && _running && <armed>`; arming is the
    // start() the flow used to call, and stop() disarms them all.
    property bool _introReadyArmed:        false
    property bool _phase1MinHoldArmed:     false
    property bool _captureTransitionArmed: false
    property bool _raiseReadyArmed:        false

    function _log(text) { appLog.info("Calib", "witmotion " + text) }
    // Created idle; every stage the routine moves to after that is one line.
    property bool _created: false
    Component.onCompleted: _created = true
    onStageChanged: if (_created) _log("stage=" + stage)

    onCalibrationDoneChanged: if (calibrationDone) r.completed()

    // ── Entry points ──────────────────────────────────────────────────────────
    // A fresh run: clears every connected segment's calibration, as the flow's
    // begin() always did, then the chain starts from the intro.
    function begin() {
        _log("begin")
        _reset()
        _running = true
    }

    // Leave: every timer off, the guide stopped, the stage back to idle (unless done).
    // Nothing on the sensors is cleared or written.
    function stop(reason) {
        if (!_running) return
        var was = stage
        _disarm()
        _running = false
        if (guide) guide.cancel()
        if (!calibrationDone) _resetState()
        _log("stop reason=" + reason + " stage=" + was)
    }

    // The host's reset(): the clear begin() does, without starting a run.
    function reset() {
        _log("reset")
        _running = false
        if (guide) guide.cancel()
        _reset()
    }

    // Restore the completed state WITHOUT re-running — used on backward nav into
    // the wizard step, or when the lead IMU is already calibrated this session.
    // Consolidates the wizard's two former restore branches; both ended in the
    // same visible state (phase 2, progress full, done).
    function restore() {
        var imu = leadImu
        if (imu !== null && imu.calibrated) {
            calibArmDownQuat  = imu.calibArmDown
            calibArmTPoseQuat = imu.calibArmTPose
        }
        _disarm()
        calibPhase         = 2
        phase1AccumMs      = 0
        stableAccumMs      = _captureHoldMs
        phaseProgress      = 1.0
        _animStage         = ""
        if (guide) guide.pose(leadArmDownQuat)
        calibrationFailed  = false
        mountFailed        = false
        mountFailMsg       = ""
        _armDownCaptured   = true
        _phase1MinHoldDone = true
        calibrationDone    = true
        _running           = false
        _log("restore done=" + calibrationDone)
    }

    function _disarm() {
        _introReadyArmed        = false
        _phase1MinHoldArmed     = false
        _captureTransitionArmed = false
        _raiseReadyArmed        = false
    }

    function _reset() {
        // ⚠ EVERY CONNECTED SEGMENT, AND BOTH CALIBRATIONS. This cleared only leadImu, and
        // only clearCalibration() — which resets the legacy transform and leaves the
        // FUNCTIONAL one (m_anatCalibrated / m_alignA / m_mountM) exactly as the previous
        // run left it. clearFunctionalCalibration() is the one that resets those.
        //
        // The consequence was a recalibration that RAN to completion and was then
        // REJECTED, every time after the first: phase 2 derives φ from
        // _phiFromAbduction(), which reads anatQuat — still anchored to the PREVIOUS
        // run's reference — so φ is measured against a stale anchor. It then either trips
        // refineMountAboutLongAxis()'s |φ| > 25° "precise refine REJECTED" guard or fails
        // the mountDeviationDeg ≤ 15° gate, and the coach is told to re-seat a sensor that
        // is mounted perfectly well.
        //
        // ⚠ slotB and slotC were never cleared at all, so their stale anchors outlived
        // even a leadImu that had been cleared. Guarded by method existence rather than
        // vendor: an HmUnit has neither method — its frame is the device's own, and the
        // wG3's routine is what re-establishes it.
        var toClear = [leadImu, slotB, slotC]
        for (var ci = 0; ci < toClear.length; ++ci) {
            var seg = toClear[ci]
            if (!seg) continue
            if (seg.clearCalibration)           seg.clearCalibration()
            if (seg.clearFunctionalCalibration) seg.clearFunctionalCalibration()
        }
        _resetState()
    }

    // The in-memory half of _reset(): the run's progress, the guide's pose, the outcome.
    function _resetState() {
        _disarm()
        _introStarted      = false
        _animStage         = ""
        calibPhase         = 0
        phase1AccumMs      = 0
        stableAccumMs      = 0
        _phase1Samples     = []
        _phase2Samples     = []
        phaseProgress      = 0.0
        if (guide) guide.pose(leadArmDownQuat, Qt.quaternion(1, 0, 0, 0))
        calibrationFailed  = false
        _armDownCaptured   = false
        _phase1MinHoldDone = false
        mountFailed        = false
        mountFailMsg       = ""
        _refA              = null
        _refB              = null
        _refC              = null
        calibArmDownQuat   = null
        calibArmTPoseQuat  = null
        calibrationDone    = false
        calibArmDownEuler  = null
        calibArmTPoseEuler = null
    }

    onActiveChanged: if (!active) stop("inactive")

    // ── Timers and connections (named properties: the test driver finds each by name) ──

    // Phase 2 hold timer.
    property Timer _stabilityHoldTimer: Timer {
        id: stabilityHoldTimer
        interval: r._ms("tick")
        repeat:   true
        running:  r.active && r._running
                  && r.calibPhase === 2
                  && !r.calibrationDone
                  && !r.mountFailed
                  && !r.calibrationFailed       // CW4: not on a lead sensor that dropped
                  && r.leadImu !== null
        onTriggered: {
            var imu = r.leadImu
            if (!imu) return
            // Stillness-gated: only accumulate while the arm is held still;
            // motion resets the hold so the captured pose is genuinely static.
            if (imu.angularVelocityDps > r._stillThreshDps) {
                r._phase2Samples = []
                r.stableAccumMs  = 0
                r.phaseProgress  = 0.0
                return
            }
            r._phase2Samples = r._phase2Samples.concat(
                [Qt.quaternion(imu.quatW, imu.quatX, imu.quatY, imu.quatZ)])
            r.stableAccumMs += interval
            r.phaseProgress = Math.min(r.stableAccumMs / r._captureHoldMs, 1.0)
            if (r.stableAccumMs >= r._captureHoldMs) {
                r.calibArmTPoseQuat = r._slerpAverage(r._phase2Samples)

                // Abduction refinement + mount validation for EVERY connected segment.
                // Each sensor: refine its mounting about the long axis by φ (from the
                // abducted-pose anatomical orientation), then evaluate the two-part gate
                //   gravity check (gravΔ ≤ 25°, catches flip/upside-down) AND
                //   long-axis deviation (φ/strapΔ ≤ 15°, catches strap rotation).
                // PASS requires ALL connected segments to pass; any FAIL → re-seat.
                var segs = r._connectedSegs()
                var allPass = segs.length > 0
                var failNames = []
                var nameFor = function(inst) {
                    return inst === r.leadImu ? qsTr("forearm")
                         : inst === r.slotB    ? qsTr("hand")
                         : qsTr("upper arm")
                }
                for (var k = 0; k < segs.length; ++k) {
                    var s2 = segs[k][0], ref = segs[k][1]
                    if (!ref) continue
                    s2.refineMountAboutLongAxis(ref, r._phiFromAbduction(s2), false)
                    var ok = s2.mountDeviationDeg <= 15.0 && s2.mountGravityErrorDeg <= 25.0
                    if (!ok) { allPass = false; failNames.push(nameFor(s2)) }
                }

                if (allPass) {
                    r.mountFailed = false
                    r.mountFailMsg = ""
                    r.calibrationDone = true
                } else {
                    r.mountFailed = true
                    r.mountFailMsg = qsTr("Sensor mounted incorrectly (%1) — re-seat per the strap guide and tap Recalibrate.")
                        .arg(failNames.join(", "))
                    // Leave calibrationDone false; user must Recalibrate.
                }
                r._log("phase2 captured mount=" + (allPass ? "pass" : "fail " + failNames.join(",")))
            }
        }
    }

    // Phase 0: 3s after the routine becomes active AND the body model has fully
    // loaded, play the 3s rest→T-pose guide animation. The ready gate
    // matters on Windows: the 19 GLB segments + first-draw shader compilation
    // can stall rendering for seconds, and a chain started against a stalled
    // renderer plays to nobody. Gated on active + a begun run: the host may
    // instantiate the flow up front — without the guard the
    // intro fires immediately and the whole capture chain runs in the
    // background.
    // ⚠ THE ONLY ENTRY POINT INTO THE WITMOTION CHAIN: with this gated off,
    // calibPhase never leaves 0 and neither phase-1 nor phase-2 timers can run.
    // ⚠ ONCE PER RUN (F13). A one-shot whose `running` binding is re-evaluated to true restarts:
    // a layout switch replaces the guide's view, `ready` falls and rises with the new view's load,
    // and the intro replayed over a chain already under way. _introStarted holds it off until
    // the run is reset.
    property bool _introStarted: false
    property Timer _introStartTimer: Timer {
        id: introStartTimer
        interval: r._ms("introStart")
        repeat:   false
        running:  r.active && r._running
                  && r.calibPhase === 0
                  && !r._introStarted
                  && r.guide !== null && r.guide.ready
        onTriggered: {
            r._introStarted = true
            r._animStage = "introUp"
            r.guide.pose(r.leadArmDownQuat)
            r.guide.animate(r.tPoseQuat, undefined, r._ms("introAnim"))
        }
    }

    // Guide-animation completion chain: each stage advances when the guide has
    // actually FINISHED drawing (BodyVizView signals the slerp's end), so a
    // stalled renderer delays the chain instead of being outrun by it.
    property Connections _guideFinished: Connections {
        target:  r.guide
        enabled: r.active && r._running
        function onFinished() {
            if (r._animStage === "introUp") {
                // Lower the arm back to rest at the same speed. The return starts
                // from the top.
                r._animStage = "introDown"
                r.guide.pose(r.tPoseQuat)
                r.guide.animate(r.leadArmDownQuat, undefined, r._ms("introAnim"))
            } else if (r._animStage === "introDown") {
                r._animStage = ""
                r._introReadyArmed = true   // 2s settle pause, then phase 1
            } else if (r._animStage === "raise") {
                r._raiseReadyArmed = true   // 2s settle pause, then phase 2
                r._animStage = ""
            }
        }
    }

    // Post-intro settle pause complete: start phase 1.
    property Timer _introReadyTimer: Timer {
        id: introReadyTimer
        interval: r._ms("introReady")
        repeat:   false
        running:  r.active && r._running && r._introReadyArmed
        onTriggered: {
            r._introReadyArmed   = false
            r.guide.pose(r.leadArmDownQuat)   // the guide holds arm-down, still
            r._phase1MinHoldDone = false
            r.calibPhase         = 1
            r._phase1MinHoldArmed = true
            // No hardware zeroing — orientation comes from our own Madgwick
            // fusion (device angle-zeroing is vestigial); the arm-down pose is
            // captured directly by phase1HoldTimer once the IMU is stable.
        }
    }

    property Timer _phase1MinHoldTimer: Timer {
        id: phase1MinHoldTimer
        interval: r._ms("minHold")
        repeat:   false
        running:  r.active && r._running && r._phase1MinHoldArmed
        onTriggered: { r._phase1MinHoldArmed = false; r._phase1MinHoldDone = true }
        // No capture here — phase1HoldTimer handles it reactively.
    }

    // Phase 1 accumulator — same stillness-gated pattern as stabilityHoldTimer
    // (phase 2). The timer runs for the whole phase; each tick decides whether
    // to accumulate (arm held still) or reset the hold (arm moving), watching
    // imu.angularVelocityDps. _armDownCaptured gates it off once captured so it
    // can't re-trigger during the raise animation. _phase1MinHoldDone (set 2s
    // after phase 1 begins via phase1MinHoldTimer) gives the user a settle
    // window before the still-watch starts.
    property Timer _phase1HoldTimer: Timer {
        id: phase1HoldTimer
        interval: r._ms("tick")
        repeat:   true
        running:  r.active && r._running
                  && r.calibPhase === 1
                  && r._phase1MinHoldDone
                  && !r._armDownCaptured
                  && !r.calibrationFailed       // CW4: not on a lead sensor that dropped
                  && r.leadImu !== null
        onTriggered: {
            var imu = r.leadImu
            if (!imu) return
            // Stillness-gated: only accumulate while the arm is held still;
            // motion resets the hold so the captured pose is genuinely static.
            if (imu.angularVelocityDps > r._stillThreshDps) {
                r._phase1Samples = []
                r.phase1AccumMs  = 0
                return
            }
            r._phase1Samples = r._phase1Samples.concat(
                [Qt.quaternion(imu.quatW, imu.quatX, imu.quatY, imu.quatZ)])
            r.phase1AccumMs += interval
            if (r.phase1AccumMs >= r._captureHoldMs) {
                r.calibArmDownQuat = r._slerpAverage(r._phase1Samples)
                // Quick-calibrate EVERY connected segment at arm-down: sets A so
                // anatQuat=identity here, with the fixed nominal mounting M, and runs
                // the gravity (flip) check. Each sensor's arm-down reference is stored
                // for the phase-2 abduction refinement. The hand, forearm and upper-arm
                // sensors are mounted COPLANAR (same strap orientation), so all three share
                // the arm nominal mount → handMount=false for all. (nominalHandMount() is a
                // non-coplanar dorsal placement we do not use.)
                r._refA = r._curQuat(r.leadImu)
                r._refB = r._curQuat(r.slotB)
                r._refC = r._curQuat(r.slotC)
                if (r.leadImu && r.leadImu.imuConnected)
                    r.leadImu.setNominalCalibration(r._refA, false)
                if (r.slotB && r.slotB.imuConnected)
                    r.slotB.setNominalCalibration(r._refB, false)
                if (r.slotC && r.slotC.imuConnected)
                    r.slotC.setNominalCalibration(r._refC, false)
                r._armDownCaptured = true  // stops timer via binding; must be after quat capture
                r._captureTransitionArmed = true
            }
        }
    }

    // Phase 1 → raise: brief pause after arm-down captured, then play the
    // raise guide animation; phase 2 starts via the completion chain above.
    property Timer _captureTransitionTimer: Timer {
        id: captureTransitionTimer
        interval: r._ms("captureTransition")
        repeat:   false
        running:  r.active && r._running && r._captureTransitionArmed
        onTriggered: {
            r._captureTransitionArmed = false
            r._animStage = "raise"
            r.guide.pose(r.leadArmDownQuat)
            r.guide.animate(r.tPoseQuat, undefined, r._ms("raiseAnim"))
        }
    }

    // Raise animation complete + settle pause, then start T-pose capture.
    property Timer _raiseReadyTimer: Timer {
        id: raiseReadyTimer
        interval: r._ms("raiseReady")
        repeat:   false
        running:  r.active && r._running && r._raiseReadyArmed
        onTriggered: {
            r.guide.pose(r.tPoseQuat)         // the guide holds the T-pose, still
            r.calibPhase      = 2
            r._raiseReadyArmed = false
        }
    }

    // ⚠ The lead UNIT here is the Witmotion peripheral itself, which carries
    // imuConnected. (The HackMotion routine watches its PERIPHERAL instead — an HmUnit
    // has no imuConnected at all, and Connections warns when it cannot find a signal on
    // its target.)
    property Connections _leadLink: Connections {
        target:  r.leadImu
        enabled: r.active && r.calibPhase > 0 && !r.calibrationDone
        function onImuConnectedChanged() {
            var imu = r.leadImu
            if (imu && !imu.imuConnected) {
                r.calibrationFailed = true
                r._log("lead disconnected")
            }
        }
    }
}
