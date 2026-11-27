// Fake `app.rig` (RigPresenter, src/Gui/app/rig_presenter.h): the Rig as the set-up and settings QML read and write
// it since the cameras' cut-over (platform refactor C-5b) and the sensors' (C-6b.1). The same members, by the same
// names; each command records its call and changes the fake's own state as the Rig would, then emits camerasChanged. A
// QtObject, so a binding re-reads a list property when it changes. Camera keys are whatever the test's fake cameras use:
// the real presenter takes only a camera key's spelling, and camera_rig_link_test covers it.
//
// THE SENSORS (C-6b.1). The real members are sensorRoles, placementPairs, sensorsDisabled, sensorAliasKeys,
// sensorAliasOf and setSensorEnabledOnThisMachine, each derived here, as there, from what the Rig holds. What it holds is
// two TEST SEAMS that the real presenter does not have, so no production QML may read them: sensorRoleMap (placement
// key → role, the Rig's roles as today's imu/roles map; FakeImuManager reads and writes it, as the real manager asks
// the Rig) and sensorAliasMap ("<description>|<id>" → alias). A test stages a set-up by assigning either; each derived
// member, and its notifier (sensorRolesChanged, sensorAliasKeysChanged), follows. imu_rig_link_test covers the real
// presenter's sensor members.
//
// ⚠ AS UNREACTIVE AS THE REAL MANAGER. The real ImuManager's resolvers (roleForDevice, instanceForRole, mountHolder,
// displayNameForDevice) are Q_INVOKABLEs: a binding that calls them re-evaluates only on the dependency it reads
// itself (app.rig.sensorRoles, app.rig.sensorAliasKeys; F40). FakeImuManager's resolvers read the roles and aliases
// through rolesUntracked() / aliasesUntracked(), from a script's variables (fake_rig_store.js), which register no
// dependency, so a production binding that forgets its app.rig read goes stale here as it would in the app. Each change
// updates that store FIRST, then assigns the members the production QML reads, so a binding re-evaluated by their
// notifier never reads the old roles (the real composition root connects the manager before app.rig for the same
// reason).
import QtQuick
import "fake_rig_store.js" as Store

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

    // ── The sensors (C-6b.1) ──
    property var sensorRoleMap: ({})        // TEST SEAM: placement key → role (see the header)
    property var sensorAliasMap: ({})       // TEST SEAM: "<description>|<id>" → alias
    property var sensorsDisabled: []        // the bare ids not enabled on this machine
    property var sensorRoles: []            // assigned by _syncRoles, after the store
    property var placementPairs: []
    property var sensorAliasKeys: []        // assigned by _syncAliases, after the store
    function _copy(m) { var out = {}; for (var k in m) out[k] = m[k]; return out }
    function _syncRoles() {
        var m = _copy(sensorRoleMap), vals = [], pairs = [], letters = { leadForearm: "A", leadHand: "B", leadUpperArm: "C" }
        Store.setRoles(m)
        for (var k in m) { vals.push(m[k]); if (letters[m[k]] !== undefined) pairs.push(k, letters[m[k]]) }
        placementPairs = pairs
        sensorRoles = vals
    }
    function _syncAliases() {
        var m = _copy(sensorAliasMap)
        Store.setAliases(m)
        sensorAliasKeys = Object.keys(m)
    }
    onSensorRoleMapChanged: _syncRoles()
    onSensorAliasMapChanged: _syncAliases()
    Component.onCompleted: { _syncRoles(); _syncAliases() }
    function rolesUntracked() { return Store.roles() }       // no QML dependency (see the header)
    function aliasesUntracked() { return Store.aliases() }
    function sensorAliasOf(key) { return Store.aliases()[key] || "" }
    function setSensorEnabledOnThisMachine(id, on) {
        _rec("setSensorEnabledOnThisMachine", [id, on]); sensorsDisabled = _toggle(sensorsDisabled, id, !on)
    }

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
