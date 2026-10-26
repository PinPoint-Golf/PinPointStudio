// Fake Witmotion ImuInstance (src/Gui/imu/imu_instance.h) — design §7.2.
//
// Models only what the wizard, ImuCalibrationFlow and ArmVizView read. Every method call
// is recorded in `calls` as {name, args, t}. The mount refinement's outcome is scripted by
// `mountResult`.
import QtQuick

QtObject {
    id: inst

    // ── Identity / transport ─────────────────────────────────────────────
    property string deviceId:          ""
    property string deviceDescription: ""
    property string stateLabel:        "Idle"
    property bool   imuConnected:      false
    property bool   busy:              false
    property int    outputRateHz:      100
    property double dataRateHz:        0
    property int    batteryPercent:    -1
    property int    gimbalDropCount:   0      // Settings ▸ IMUs reads it on a connected Witmotion

    // ── Live orientation ─────────────────────────────────────────────────
    property real quatW: 1
    property real quatX: 0
    property real quatY: 0
    property real quatZ: 0
    property real angularVelocityDps: 0
    // The trunk routine's stillness and mount (imu_instance.h gyroDps, longAxisFromUpDeg); scriptable.
    // The defaults are a worn sensor standing still: |gyro| 0, sensor +Y (away from the USB end) straight up.
    property real gyroDps: 0
    property real longAxisFromUpDeg: 0
    // The live trunk heading the turn steps start and end on (imu_instance.h trunkHeadingDeg): NaN
    // until the sensor is anatomically calibrated, as the real one. `headingDeg` is the scripted
    // reading (set NaN for a filter fault); play() scripts it over time with the gyro.
    property real headingDeg: 0
    readonly property real trunkHeadingDeg: anatCalibrated ? headingDeg : NaN

    // ── Legacy two-pose calibration ──────────────────────────────────────
    property bool       calibrated:            false
    property quaternion calibArmDown:          Qt.quaternion(1, 0, 0, 0)
    property quaternion calibArmTPose:         Qt.quaternion(1, 0, 0, 0)
    property quaternion calibTransform:        Qt.quaternion(1, 0, 0, 0)
    property bool       calibrationAngleValid: true

    // ── Functional (anatomical) calibration ──────────────────────────────
    property quaternion anatQuat:             Qt.quaternion(1, 0, 0, 0)
    property bool       anatCalibrated:       false
    property double     mountDeviationDeg:    0
    property double     mountGravityErrorDeg: 0
    // The composite gate (imu_instance.h fullyCalibrated(): anatomical transform AND both mount
    // checks within kMountDeviationMaxDeg 15° / kMountGravityErrorMaxDeg 25°). Added for Stage 4:
    // SetupDraft.outcome() reads it as a Witmotion segment's live validity.
    readonly property bool fullyCalibrated: anatCalibrated && mountDeviationDeg <= 15
                                            && mountGravityErrorDeg <= 25

    // ── Trunk stand-tall solve (imu_instance.h calibrateTrunk, W2.1a) ────
    // The record of the solve that set A/M; empty unless the current calibration came from it
    // (cleared by clearFunctionalCalibration, as the real instance's is).
    property var trunkCalibration: ({})
    // Scripted results of calibrateTrunk(role), oldest first: each call takes the next entry
    // (a map merged over the default record — e.g. { valid: false, failure: "Hip sensor: …" }),
    // and once the script is spent every call is valid. A valid result applies A/M, so
    // anatCalibrated and fullyCalibrated read true, as on the real instance; an invalid one
    // changes nothing.
    property var trunkScript: []

    // Scripted outcome of refineMountAboutLongAxis(). The real gate is dev ≤ 15°, grav ≤ 25°.
    property var mountResult: ({ anatCalibrated: true, dev: 3.0, grav: 4.0 })

    // ── Call log ─────────────────────────────────────────────────────────
    property var calls: []
    // Optional shared sink (e.g. the manager's `calls`) — also receives every record, with
    // the device id prefixed, so a test can read one ordered timeline.
    property var sink: null

    function _rec(name, args) {
        var r = { name: name, args: args, t: Date.now(), device: deviceId }
        calls.push(r)
        if (sink) sink.push({ name: deviceId + "." + name, args: args, t: r.t, device: deviceId })
    }
    function countCalls(name) {
        var n = 0
        for (var i = 0; i < calls.length; ++i) if (calls[i].name === name) ++n
        return n
    }

    // ── API the production QML calls ─────────────────────────────────────
    function clearCalibration() {
        _rec("clearCalibration", [])
        calibrated = false
        calibArmDown = Qt.quaternion(1, 0, 0, 0)
        calibArmTPose = Qt.quaternion(1, 0, 0, 0)
        calibTransform = Qt.quaternion(1, 0, 0, 0)
    }
    function clearFunctionalCalibration() {
        _rec("clearFunctionalCalibration", [])
        anatCalibrated = false
        anatQuat = Qt.quaternion(1, 0, 0, 0)
        mountDeviationDeg = 0
        mountGravityErrorDeg = 0
        trunkCalibration = ({})
    }
    function calibrateTrunk(role) {
        _rec("calibrateTrunk", [role])
        var label = role === "pelvis" ? "Hip sensor" : "Chest sensor"
        var rec = { valid: true, applied: true, failure: "", role: role, deviceId: deviceId,
                    serial: deviceId, windowEndUs: Date.now() * 1000, samplesOk: true, stillOk: true,
                    gravityOk: true, longAxisOk: true, faceOk: true, sampleFraction: 0.99,
                    maxAbsGyroDps: 0.1, gravityErrPct: 0.5, longAxisFromUpDeg: 3, faceFromHorizontalDeg: 4,
                    deltaTier: "δ0", sigmaDeltaDeg: 10, samplesReceived: 397, samplesExpected: 400,
                    windowS: 2, alignA: [1, 0, 0, 0], mountM: [1, 0, 0, 0] }
        if (trunkScript.length > 0) {
            var s = trunkScript[0]
            trunkScript = trunkScript.slice(1)
            for (var k in s) rec[k] = s[k]
            if (rec.valid === false && rec.failure === "") rec.failure = label + ": scripted failure"
        }
        rec.applied = rec.valid
        if (rec.valid) {
            rec.calibratedAt = new Date().toISOString()
            anatQuat = Qt.quaternion(1, 0, 0, 0)
            mountDeviationDeg = 0
            mountGravityErrorDeg = 0
            anatCalibrated = true
            calibrated = true
            trunkCalibration = rec
        }
        return rec
    }
    function setNominalCalibration(q, handMount) {
        _rec("setNominalCalibration", [q, handMount])
        anatQuat = Qt.quaternion(1, 0, 0, 0)
        anatCalibrated = true
    }
    function refineMountAboutLongAxis(ref, phiDeg, handMount) {
        _rec("refineMountAboutLongAxis", [ref, phiDeg, handMount])
        var r = mountResult || {}
        anatCalibrated       = r.anatCalibrated !== undefined ? r.anatCalibrated : true
        mountDeviationDeg    = r.dev  !== undefined ? r.dev  : 0
        mountGravityErrorDeg = r.grav !== undefined ? r.grav : 0
    }
    function setCalibration(a, b) {
        _rec("setCalibration", [a, b])
        calibArmDown = a
        calibArmTPose = b
        calibrated = true
    }

    // ── Test helpers ─────────────────────────────────────────────────────
    function setQuat(w, x, y, z) { quatW = w; quatX = x; quatY = y; quatZ = z }

    // A scripted movement: frames [{ heading, gyro, ms }], oldest first — each frame's heading (°)
    // and gyroDps are set, then held `ms` (real ms) before the next; the last frame's values stay.
    // A frame may leave out heading or gyro to keep the one before. play() replaces a movement in
    // flight; stopPlay() freezes where it is.
    property var  _frames: []
    property bool playing: false
    function play(frames) {
        _playTimer.stop()
        _frames = frames.slice()
        playing = true
        _nextFrame()
    }
    function stopPlay() { _playTimer.stop(); _frames = []; playing = false }
    function _nextFrame() {
        if (_frames.length === 0) { playing = false; return }
        var f = _frames[0]
        _frames = _frames.slice(1)
        if (f.heading !== undefined) headingDeg = f.heading
        if (f.gyro !== undefined)    gyroDps = f.gyro
        if (_frames.length === 0) { playing = false; return }
        _playTimer.interval = Math.max(1, f.ms || 1)
        _playTimer.start()
    }
    property Timer _playTimer: Timer {
        repeat: false
        onTriggered: inst._nextFrame()
    }
}
