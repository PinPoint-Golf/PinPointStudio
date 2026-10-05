// Fake CalibrationGuide (src/Gui/calibration/CalibrationGuide.qml) — the same members, no
// 3-D view. For routine-level cases.
//   ready   settable (true by default — the guide is "loaded")
//   pose()  records the pose
//   animate() records the target and emits finished() after `ms` (already scaled by the
//           routine's pace) — or on finish() — unless `stall` is set, when it never does
//   cancel() stops a pending finish; no finished()
import QtQuick

QtObject {
    id: g

    property bool ready: true
    property bool useGuideCamera: false
    property bool stall: false
    property bool active: true

    readonly property quaternion hmUpperArm:  Qt.quaternion(0.6158076, 0.7131185, -0.2317729, 0.2419182)
    readonly property quaternion hmForePose0: Qt.quaternion(0.1753510, 0.3046825, 0.6926323, 0.6298263)
    readonly property quaternion hmForePose1: Qt.quaternion(0.2482337, 0.2489165, 0.5060204, 0.7876319)
    readonly property real       hmRaiseDeg:  30

    signal finished()

    property quaternion armTarget:  Qt.quaternion(0.7071, 0.7071, 0, 0)
    property quaternion foreTarget: Qt.quaternion(1, 0, 0, 0)
    property bool       animating:  false
    property bool       inFlight:   false
    // Every call, oldest first: { name, arm, fore, ms, t }
    property var log: []
    property int cancels: 0

    function _q(q) { return q === undefined ? undefined : Qt.quaternion(q.scalar, q.x, q.y, q.z) }
    function pose(armQ, foreQ) {
        log.push({ name: "pose", arm: _q(armQ), fore: _q(foreQ), t: Date.now() })
        _timer.stop(); inFlight = false; animating = false
        armTarget = armQ
        if (foreQ !== undefined) foreTarget = foreQ
    }
    function animate(armQ, foreQ, ms) {
        log.push({ name: "animate", arm: _q(armQ), fore: _q(foreQ), ms: ms, t: Date.now() })
        animating = true; inFlight = true
        armTarget = armQ
        if (foreQ !== undefined) foreTarget = foreQ
        _timer.interval = Math.max(1, ms)
        if (!stall) _timer.restart()
    }
    function cancel() {
        log.push({ name: "cancel", t: Date.now() })
        cancels += 1
        _timer.stop(); inFlight = false; animating = false
    }
    // Ends the animation in flight now (the test's hand on the clock).
    function finish() {
        if (!inFlight) return
        _timer.stop(); inFlight = false
        g.finished()
    }
    function count(name) {
        var n = 0
        for (var i = 0; i < log.length; ++i) if (log[i].name === name) ++n
        return n
    }

    property Timer _timer: Timer {
        repeat: false
        onTriggered: { g.inFlight = false; g.finished() }
    }
}
