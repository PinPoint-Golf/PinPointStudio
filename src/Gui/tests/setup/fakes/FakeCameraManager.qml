// Fake CameraManager (src/Gui/cameras/camera_manager.h) — design §7.2.
//
// `instances` stays EMPTY unless a test adds one with addInstance(): a live instance makes
// the wizard instantiate PpCameraFrame (a video item), which these tests do not want.
// Entries use the real cameraList keys (camera_manager.cpp:159–212).
import QtQuick

QtObject {
    id: mgr

    property var  _cams: []
    property var  instances: []
    property bool isRecording: false
    property bool anyConnecting: false
    property var  sessionCameraExcluded: []

    readonly property var cameraList: {
        var out = []
        for (var i = 0; i < _cams.length; ++i) {
            var c = _cams[i]
            out.push({ index: i, cameraKey: c.key, description: c.description, alias: c.alias,
                       perspective: c.perspective, selected: c.selected,
                       sessionEnabled: sessionCameraExcluded.indexOf(c.key) < 0,
                       serialNumber: c.serial || "", interface: "USB3",
                       maxWidth: 1440, maxHeight: 1080 })
        }
        return out
    }
    readonly property bool anySelected: {
        for (var i = 0; i < _cams.length; ++i) if (_cams[i].selected) return true
        return false
    }

    property var calls: []
    function _rec(name, args) { calls.push({ name: name, args: args, t: Date.now() }) }
    function countCalls(name) {
        var n = 0
        for (var i = 0; i < calls.length; ++i) if (calls[i].name === name) ++n
        return n
    }

    function _touch() { _cams = _cams.slice() }

    // ── Commands (recorded) ──────────────────────────────────────────────
    function setSelected(index, selected) {
        _rec("setSelected", [index, selected])
        if (_cams[index]) { _cams[index].selected = selected; _touch() }
    }
    function startAll()    { _rec("startAll", []);    isRecording = true }
    function stopCapture() { _rec("stopCapture", []); isRecording = false }
    function startCapture(){ _rec("startCapture", []) }
    function disconnectAll() {
        _rec("disconnectAll", [])
        for (var i = 0; i < _cams.length; ++i) _cams[i].selected = false
        _touch()
    }
    function setBallRoi(inst, rect)      { _rec("setBallRoi", [inst, rect]); if (inst) inst.roi = rect }
    function relearnBallBaseline(inst)   { _rec("relearnBallBaseline", [inst]) }
    function setSessionCameraEnabled(key, on) {
        _rec("setSessionCameraEnabled", [key, on])
        var ex = sessionCameraExcluded.slice()
        var i = ex.indexOf(key)
        if (on && i >= 0) ex.splice(i, 1)
        else if (!on && i < 0) ex.push(key)
        sessionCameraExcluded = ex
    }

    // ── Test helpers ─────────────────────────────────────────────────────
    // perspective: the CameraInstance enum value (None 0, DownTheLine 1, FaceOn 2, Other 3, Impact 4)
    function addCamera(key, perspective, alias) {
        var a = _cams.slice()
        a.push({ key: key, perspective: perspective, alias: alias || "", description: "Camera " + key,
                 selected: false })
        _cams = a
    }
    function setCameraSelected(key, selected) {
        for (var i = 0; i < _cams.length; ++i) if (_cams[i].key === key) _cams[i].selected = selected
        _touch()
    }

    // A live CameraInstance stand-in for `key` (added by B1 for the Ball step and the row
    // thumbnails). It carries what the wizard reads (cameraKey, ballPresent, roi) and what
    // PpCameraFrame reads when the wizard instantiates one over it (the frame/pose/level
    // fields and the four sink methods, all inert). `props` overrides any field.
    // ⚠ A live instance makes the wizard build PpCameraFrame, which also needs a
    // `shotReplay` context property — SetupDriver supplies an inert one.
    property Component _instComp: Component {
        QtObject {
            property string cameraKey:       ""
            property int    perspective:     0
            property bool   ballPresent:     false
            property rect   roi:             Qt.rect(0, 0, 0, 0)
            property bool   needsDebayer:    false
            property int    frameWidth:      1440
            property int    frameHeight:     1080
            property bool   isRecording:     false
            property bool   isPreviewFeed:   false
            property real   configuredFps:   120
            property real   cameraFps:       120
            property bool   poseEnabled:     false
            property var    poseKeypoints:   []
            property real   levelPeak:       0
            property bool   levelClipped:    false
            property real   levelBackground: 0
            property bool   ballDetected:    false
            property real   ballX:           0
            property real   ballY:           0
            property real   ballRadius:      0
            function setRoi(r)            { roi = r }
            function addVideoSink(s)      {}
            function removeVideoSink(s)   {}
            function addBayerItem(i)      {}
            function removeBayerItem(i)   {}
        }
    }
    function addInstance(key, props) {
        var persp = 0
        for (var i = 0; i < _cams.length; ++i) if (_cams[i].key === key) persp = _cams[i].perspective
        var p = { cameraKey: key, perspective: persp }
        if (props) for (var k in props) p[k] = props[k]
        var inst = _instComp.createObject(mgr, p)
        var a = instances.slice(); a.push(inst); instances = a
        return inst
    }
    function instanceFor(key) {
        for (var i = 0; i < instances.length; ++i) if (instances[i].cameraKey === key) return instances[i]
        return null
    }
}
