// Fake launchMonitor. ⚠ Placeholder: Main.qml has no `launchMonitor.` member reads today
// (checked 5 Oct 2026), so nothing here is load-bearing yet. Extend it from the QML that
// actually reads it when a shell-level test needs it.
import QtQuick

QtObject {
    property bool   configured: false
    property string state:      "idle"
    property string deviceName: ""
}
