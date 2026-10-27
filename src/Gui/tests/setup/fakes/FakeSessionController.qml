// Fake SessionController (src/Gui/session/session_controller.h) — the members Main.qml's
// wizard hand-off uses. start(type) is recorded, with the focus it found set (`focus`: Main sets
// the focus BEFORE start()). defaultType is the real constant
// (SessionController::Type::Wrist); the focus is set before start() and cleared by
// endSession(), as on the real controller.
import QtQuick

QtObject {
    property bool   running:           false
    property int    activeSessionType: -1
    property string activeClub:        ""
    property string elapsedLabel:      "00:00"
    readonly property int defaultType: 1
    property string focusConditionId:  ""
    property int    focusFromShot:     0
    // The real controller's one NOTIFY for both focus properties.
    signal focusChanged()
    onFocusConditionIdChanged: focusChanged()
    onFocusFromShotChanged:    focusChanged()

    property var calls: []
    function start(type) {
        calls.push({ name: "start", args: [type], focus: focusConditionId, t: Date.now() })
        activeSessionType = type
        running = true
    }
    function endSession() {
        calls.push({ name: "endSession", args: [], t: Date.now() })
        running = false
        focusConditionId = ""
        focusFromShot = 0
    }
}
