// Fake SessionController (src/Gui/session/session_controller.h) — the members Main.qml's
// wizard hand-off uses. start(type) is recorded.
import QtQuick

QtObject {
    property bool   running:           false
    property int    activeSessionType: -1
    property string activeClub:        ""
    property string elapsedLabel:      "00:00"

    property var calls: []
    function start(type) {
        calls.push({ name: "start", args: [type], t: Date.now() })
        activeSessionType = type
        running = true
    }
    function endSession() {
        calls.push({ name: "endSession", args: [], t: Date.now() })
        running = false
    }
}
