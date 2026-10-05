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

    // ── Live orientation ─────────────────────────────────────────────────
    property real quatW: 1
    property real quatX: 0
    property real quatY: 0
    property real quatZ: 0
    property real angularVelocityDps: 0

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
}
