// Fake LiveWristAngles (src/Gui/viz/live_wrist_angles.h). `active` is written by the
// wizard's Check-panel Binding; every write is logged in `activeLog`.
import QtQuick

QtObject {
    id: lw
    property bool   active:     false
    property bool   bowValid:   false
    property bool   rollValid:  false
    property double bowValue:   0
    property string bowLabel:   "—"
    property double hingeValue: 0
    property string hingeLabel: "—"
    property double rollValue:  0
    property string rollLabel:  "—"
    property string rollTitle:  "Roll"

    property var activeLog: []
    onActiveChanged: activeLog.push({ value: active, t: Date.now() })
}
