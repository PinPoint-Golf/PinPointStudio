// Fake navController — the members Main.qml uses (back, forward, navigate, navigateRail,
// currentIndex, canGoBack, canGoForward, sessionLocked). Calls are recorded.
import QtQuick

QtObject {
    property int  currentIndex:  0
    property bool canGoBack:     false
    property bool canGoForward:  false
    property bool sessionLocked: false

    property var calls: []
    function _rec(name, args) { calls.push({ name: name, args: args, t: Date.now() }) }
    function back()             { _rec("back", []) }
    function forward()          { _rec("forward", []) }
    function navigate(idx)      { _rec("navigate", [idx]); currentIndex = idx }
    function navigateRail(idx)  { _rec("navigateRail", [idx]) }
}
