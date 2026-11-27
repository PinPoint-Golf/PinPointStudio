// Fake `app.rig` (RigPresenter, src/Gui/app/rig_presenter.h): the Rig as the set-up and settings QML read and write
// it since the cameras' cut-over (platform refactor C-5b). The same members, by the same names; each command records
// its call and changes the fake's own state as the Rig would, then emits camerasChanged. A QtObject, so a binding
// re-reads a list property when it changes. Camera keys are whatever the test's fake cameras use: the real presenter
// takes only a camera key's spelling, and camera_rig_link_test covers it.
import QtQuick

QtObject {
    id: rig

    property string rigId: "fake-rig"
    property var camerasMirrored: []
    property var camerasFixedInPlace: []
    property var camerasDisabled: []
    property var perspectives: ({})   // key -> code
    property var hittingAreas: ({})   // key -> Qt.rect
    property var aliases: ({})        // key -> text
    signal camerasChanged()

    property var calls: []
    function _rec(name, args) { calls.push({ name: name, args: args }) }
    function countCalls(name) {
        var n = 0
        for (var i = 0; i < calls.length; ++i) if (calls[i].name === name) ++n
        return n
    }
    function _toggle(list, key, on) {
        var out = list.slice(), i = out.indexOf(key)
        if (on && i < 0) out.push(key)
        else if (!on && i >= 0) out.splice(i, 1)
        return out
    }
    function _put(map, key, value) {
        var out = {}
        for (var k in map) out[k] = map[k]
        if (value === undefined) delete out[key]
        else out[key] = value
        return out
    }

    function perspectiveOf(key) { return perspectives[key] || 0 }
    function hittingAreaOf(key) { var b = hittingAreas[key]; return b ? b : Qt.rect(0, 0, 0, 0) }
    function aliasOf(key) { return aliases[key] || "" }

    function assignPerspective(key, code) {
        _rec("assignPerspective", [key, code])
        var p = perspectives
        if (code === 4) for (var k in p) if (k !== key && p[k] === 4) p = _put(p, k, undefined)   // Impact is exclusive
        perspectives = _put(p, key, code > 0 ? code : undefined)
        camerasChanged()
    }
    function setMirrored(key, on) {
        _rec("setMirrored", [key, on]); camerasMirrored = _toggle(camerasMirrored, key, on); camerasChanged()
    }
    function setFixedInPlace(key, on) {
        _rec("setFixedInPlace", [key, on]); camerasFixedInPlace = _toggle(camerasFixedInPlace, key, on); camerasChanged()
    }
    function setHittingArea(key, r) {
        _rec("setHittingArea", [key, r]); hittingAreas = _put(hittingAreas, key, Qt.rect(r.x, r.y, r.width, r.height)); camerasChanged()
    }
    function clearHittingArea(key) {
        _rec("clearHittingArea", [key]); hittingAreas = _put(hittingAreas, key, undefined); camerasChanged()
    }
    function setAlias(key, text) {
        _rec("setAlias", [key, text]); aliases = _put(aliases, key, text ? text : undefined); camerasChanged()
    }
    function setEnabledOnThisMachine(key, on) {
        _rec("setEnabledOnThisMachine", [key, on]); camerasDisabled = _toggle(camerasDisabled, key, !on); camerasChanged()
    }
}
