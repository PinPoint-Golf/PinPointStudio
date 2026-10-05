// Fake HmInstance (src/Gui/imu/hm_instance.h) — the wG3 PERIPHERAL, design §7.2.
//
// What it models is the libwrist calibration choreography as ImuCalibrationFlow consumes
// it (ImuCalibrationFlow.qml l.339–760, 871–898, 1133–1234):
//   - the five marker calls are QUEUED and return nothing; the library's answer arrives
//     later, as a phase/state change or as calibrationCallRefused(status, call). Here the
//     queue is a Timer stepping every `libraryLatencyMs`.
//   - phase, state, abort reason, presence fields and calibrationActive all notify through
//     calibrationStateChanged() in the real class. ⚠ QML cannot declare a property whose
//     NOTIFY is another property's signal, so here calibrationPhase notifies through its
//     own calibrationPhaseChanged (which is what the flow's `hmPhase` binding follows) and
//     every library event ALSO emits calibrationStateChanged() explicitly, after the
//     fields are set. The real class emits ONE signal that both the binding and the
//     Connections handler follow; the order of the two reactions can differ from this.
//
// Modes:
//   "autoScript" — each ACCEPTED call advances the phase the way the library does;
//                  a call that does not match the library's phase is recorded and ignored.
//   "strict"     — as autoScript, but a mismatched (e.g. duplicate) marker is REFUSED with
//                  INVALID_STATE (-2) through calibrationCallRefused, and counted in
//                  `refusals`. This is how a duplicate-marker defect becomes visible.
//   "manual"     — calls are recorded only; the test drives every field itself.
import QtQuick

QtObject {
    id: dev

    // ── Library enums (mirrored in ImuCalibrationFlow.qml l.347–375) ──────
    readonly property int pIdle: 0
    readonly property int pAwaitHorizontal: 1
    readonly property int pMarkingPose0: 2
    readonly property int pObservingRaise: 3
    readonly property int pMarkingPose1: 4
    readonly property int pApplying: 5
    readonly property int pVerifying: 6
    readonly property int pComplete: 7
    readonly property int pAborted: 8

    readonly property int sUnknown: 0
    readonly property int sUncalibrated: 1
    readonly property int sCalibrated: 2
    readonly property int sLost: 3

    readonly property int aNone: 0
    readonly property int aCaller: 1
    readonly property int aRaiseTooSlow: 2
    readonly property int aStreamLost: 3
    readonly property int aLinkLost: 4
    readonly property int aNoResult: 5

    readonly property int errInvalidState: -2
    readonly property int errLinkDown: -12
    readonly property int errNoStream: -14
    readonly property int errBusy: -18

    // ── Device properties the production QML reads ───────────────────────
    property string deviceId:          ""
    property string deviceDescription: "HackMotion wG3"
    property string stateLabel:        "Idle"
    property bool   imuConnected:      false
    property bool   busy:              false
    property int    batteryPercent:    -1
    property double dataRateHz:        0

    property QtObject unitLowerArm: FakeHmUnit { unitLabel: "Lower arm" }
    property QtObject unitPalm:     FakeHmUnit { unitLabel: "Palm" }

    property int    calibrationPhase:       pIdle
    property int    calibrationState:       sUncalibrated
    property int    calibrationAbortReason: aNone
    property double presenceAngleDeg:       NaN
    property double poseSpreadMaxDeg:       NaN
    property int    presenceSamplesUsed:    0
    property bool   presenceNotMeasured:    false
    property bool   calibrationActive:      false
    property bool   streaming:              false
    property double relativeAngleDeg:       NaN

    // calibrationStateChanged() is the implicit change signal of calibrationState and is
    // emitted explicitly after every library event (see header).
    signal calibrationCallRefused(int status, string call)
    signal calibrationInvalidated()
    signal relativeAngleChanged()

    // ── Script controls ──────────────────────────────────────────────────
    property string mode: "autoScript"     // "autoScript" | "strict" | "manual"
    property int    libraryLatencyMs: 30   // per queued library step
    // Outcome of confirmReferencePose():
    //   "calibrated" | "uncalibrated" | "notMeasured" | "silent" (never reports)
    property string presenceOutcome: "calibrated"
    // Lower-arm travel the fake reports between OBSERVING_RAISE and confirmRaise (deg).
    // NaN = leave the unit quaternion untouched (the flow then cannot measure, and
    // proceeds permissively). Applied `raiseTravelAfterMs` after OBSERVING_RAISE.
    property real raiseTravelDeg: 30
    property int  raiseTravelAfterMs: 1000

    // ── Records ──────────────────────────────────────────────────────────
    property var calls: []
    property var refusalLog: []
    property int refusals: 0
    property var phaseLog: []      // every phase the fake entered, with t
    property var sink: null

    function _rec(name, args) {
        var r = { name: name, args: args, t: Date.now(), device: deviceId,
                  libPhase: _libPhase, phase: calibrationPhase }
        calls.push(r)
        if (sink) sink.push({ name: deviceId + "." + name, args: args, t: r.t, device: deviceId })
    }
    function countCalls(name) {
        var n = 0
        for (var i = 0; i < calls.length; ++i) if (calls[i].name === name) ++n
        return n
    }

    // The phase the library is in once everything already queued has run — what a new
    // call is judged against (the I/O thread processes calls in order).
    property int _libPhase: pIdle
    property var _queue: []

    property Timer _pump: Timer {
        interval: dev.libraryLatencyMs
        repeat: true
        running: dev._queue.length > 0
        onTriggered: {
            var q = dev._queue.slice()
            var step = q.shift()
            dev._queue = q
            step()
        }
    }

    property Timer _travelTimer: Timer {
        interval: dev.raiseTravelAfterMs
        onTriggered: dev._applyTravel(dev.raiseTravelDeg)
    }

    function _enqueue(fn) { var q = _queue.slice(); q.push(fn); _queue = q }

    function _emitState() { calibrationStateChanged() }

    function _enterPhase(p) {
        calibrationPhase = p
        phaseLog.push({ phase: p, t: Date.now() })
        _emitState()
    }

    function _refuse(status, call) {
        _enqueue(function() {
            dev.refusals += 1
            dev.refusalLog.push({ status: status, call: call, t: Date.now() })
            dev.calibrationCallRefused(status, call)
        })
    }

    // Rotation of the lower-arm unit about X, in degrees (a base 10° tilt keeps it off
    // identity, which the flow reads as "no sample yet").
    function _applyTravel(deg) {
        var a = (10 + deg) * Math.PI / 360
        unitLowerArm.setQuat(Math.cos(a), Math.sin(a), 0, 0)
    }

    // Returns true when the call should proceed; otherwise refuses/ignores it.
    function _gate(call, expectedPhases) {
        if (mode === "manual") return false
        if (!imuConnected) { _refuse(errLinkDown, call); return false }
        if (expectedPhases.indexOf(_libPhase) < 0) {
            if (mode === "strict") _refuse(errInvalidState, call)
            return false
        }
        return true
    }

    // ── The five Q_INVOKABLEs ────────────────────────────────────────────
    function beginCalibration() {
        _rec("beginCalibration", [])
        if (mode === "manual") return
        if (!imuConnected) { _refuse(errLinkDown, "beginCalibration"); return }
        if (!streaming)    { _refuse(errNoStream, "beginCalibration"); return }
        if (!_gate("beginCalibration", [pIdle, pComplete, pAborted])) return
        _libPhase = pAwaitHorizontal
        _enqueue(function() {
            dev.calibrationActive = true
            dev.calibrationAbortReason = dev.aNone
            dev.presenceSamplesUsed = 0
            dev.presenceNotMeasured = false
            dev.presenceAngleDeg = NaN
            dev.poseSpreadMaxDeg = NaN
            dev._enterPhase(dev.pAwaitHorizontal)
        })
    }

    function confirmHorizontal() {
        _rec("confirmHorizontal", [])
        if (!_gate("confirmHorizontal", [pAwaitHorizontal])) return
        _libPhase = pObservingRaise
        _enqueue(function() { dev._enterPhase(dev.pMarkingPose0) })
        _enqueue(function() {
            if (!isNaN(dev.raiseTravelDeg)) {
                dev._applyTravel(0)
                dev._travelTimer.restart()
            }
            dev._enterPhase(dev.pObservingRaise)
        })
    }

    function confirmRaise() {
        _rec("confirmRaise", [])
        if (!_gate("confirmRaise", [pObservingRaise])) return
        _libPhase = pVerifying
        _enqueue(function() { dev._enterPhase(dev.pMarkingPose1) })
        _enqueue(function() { dev._enterPhase(dev.pApplying) })
        _enqueue(function() {
            dev._applyTravel(0)            // back towards pose 0 for the reference hold
            dev._enterPhase(dev.pVerifying)
        })
    }

    function confirmReferencePose() {
        _rec("confirmReferencePose", [])
        if (!_gate("confirmReferencePose", [pVerifying])) return
        if (presenceOutcome === "silent") return
        _libPhase = pComplete
        _enqueue(function() {
            dev.calibrationActive = false
            if (dev.presenceOutcome === "calibrated") {
                dev.presenceAngleDeg = 1.2
                dev.poseSpreadMaxDeg = 0.8
                dev.presenceSamplesUsed = 64
                dev.presenceNotMeasured = false
                dev.relativeAngleDeg = 0.6
                dev.unitLowerArm.anatCalibrated = true
                dev.unitPalm.anatCalibrated = true
                dev.calibrationState = dev.sCalibrated
            } else if (dev.presenceOutcome === "notMeasured") {
                dev.presenceSamplesUsed = 3
                dev.presenceNotMeasured = true
                dev.calibrationState = dev.sUnknown
            } else {
                dev.presenceAngleDeg = 9.0
                dev.presenceSamplesUsed = 64
                dev.presenceNotMeasured = false
                dev.calibrationState = dev.sUncalibrated
            }
            dev._enterPhase(dev.pComplete)
        })
    }

    function abortCalibration() {
        _rec("abortCalibration", [])
        if (mode === "manual") return
        // A local state reset in the library — never refused while a routine runs. With
        // nothing in progress libwrist's wr_calibration_abort() returns
        // WR_ERR_INVALID_STATE ("nothing to abort", wr_session.c l.4758), which
        // HmSessionWorker::calibrationCall turns into calibrationCallRefused; strict mode
        // models that, autoScript ignores it.
        if (_libPhase === pIdle || _libPhase === pComplete || _libPhase === pAborted) {
            if (mode === "strict") _refuse(errInvalidState, "abortCalibration")
            return
        }
        var atVerifying = (_libPhase === pVerifying)
        _libPhase = atVerifying ? pComplete : pAborted
        _enqueue(function() {
            dev.calibrationActive = false
            dev.calibrationAbortReason = dev.aCaller
            dev._enterPhase(atVerifying ? dev.pComplete : dev.pAborted)
        })
    }

    // ── Test helpers ─────────────────────────────────────────────────────
    function setConnected(on) {
        imuConnected = on
        stateLabel = on ? "Connected" : "Idle"
        streaming = on
        if (!on) {
            // A dropped link destroys the calibration (library forces UNCALIBRATED), and
            // the library advances no further phases for the dead session: drop whatever
            // was still queued, and the pending raise travel.
            _queue = []
            _travelTimer.stop()
            var wasCal = calibrationState === sCalibrated || calibrationActive
            _libPhase = pIdle
            calibrationActive = false
            calibrationPhase = pIdle
            calibrationState = sUncalibrated
            unitLowerArm.anatCalibrated = false
            unitPalm.anatCalibrated = false
            _emitState()
            if (wasCal) calibrationInvalidated()
        }
    }
}
