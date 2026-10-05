// Fake HmUnit (src/Gui/imu/hm_instance.h) — one of a wG3's two sensor units, the object
// imuManager.instanceForRole() hands back for a HackMotion role. It has NO imuConnected,
// stateLabel or calibration methods: those are device properties (FakeHmDevice).
import QtQuick

QtObject {
    property string unitId:    ""
    property string unitLabel: ""

    property real quatW: 1
    property real quatX: 0
    property real quatY: 0
    property real quatZ: 0
    property real angularVelocityDps: 0

    property bool       anatCalibrated: false
    property quaternion anatQuat:       Qt.quaternion(1, 0, 0, 0)
    property bool       calibrated:     false
    property quaternion calibTransform: Qt.quaternion(1, 0, 0, 0)

    function setQuat(w, x, y, z) { quatW = w; quatX = x; quatY = y; quatZ = z }
}
