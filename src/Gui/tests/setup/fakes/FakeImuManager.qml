// Fake ImuManager (src/Gui/imu/imu_manager.h) — design §7.2.
//
// Placement is held where the real one holds it — appSettings.imuRoles (the REAL
// AppSettings over a scratch QSettings), keyed by placement key → role name:
//   Witmotion  "<devId>"          → "leadForearm" | "leadHand" | "leadUpperArm" | …
//   HackMotion "<devId>#lowerArm" → "leadForearm", "<devId>#palm" → "leadHand"
// The tests place devices with assign(id, letter) below — a test shorthand (A leadForearm,
// B leadHand, C leadUpperArm) written straight onto appSettings.imuRoles; the real class's
// derived imuPlacement view is read-only (Stage 5c), and the real ImuManager has no …ForSlot
// methods or setPlacementForDevice any more — neither does this.
// The resolvers follow the real ladder (imu_manager.cpp): an enumerated (present) and
// session-enabled claimant first, then a present-but-disabled one, then any.
//
// Every production call is recorded in `calls` as {name, args, t}; the fake devices'
// own calls are mirrored there too (prefixed "<devId>."), giving one ordered timeline.
import QtQuick

QtObject {
    id: mgr

    // ── Properties the production QML reads ──────────────────────────────
    property var  _devices: []          // internal records, see addWitmotion/addHackMotion
    property var  instances: []         // live instances of SELECTED devices
    property bool imuScanActive: false
    property string imuScanError: ""
    property bool anyConnecting: false

    readonly property var imuDeviceList: {
        var out = []
        for (var i = 0; i < _devices.length; ++i) {
            var d = _devices[i]
            // The capability fields Settings ▸ IMUs binds to bools/strings (imu_manager.cpp
            // imuDeviceList) are present with the real class's types, so hosting ImusPanel
            // costs no "Unable to assign [undefined]" warning.
            out.push({ index: i, id: d.id, imuKey: d.id, description: d.description,
                       alias: d.alias, transport: d.transport, vendor: d.vendor,
                       sessionEnabled: sessionImuExcluded.indexOf(d.id) < 0,
                       present: d.present, selected: d.selected,
                       firmwareVersion: "", supportedRatesHz: [], defaultRateHz: 100,
                       supportsMagCalibration: false, supportsConfigPersistence: false })
        }
        return out
    }
    readonly property var imuList: imuDeviceList
    property var sessionImuExcluded: []

    readonly property bool anySelected: {
        var _dep = instances
        for (var i = 0; i < _devices.length; ++i) if (_devices[i].selected) return true
        return false
    }
    readonly property bool imuConnected: {
        for (var i = 0; i < instances.length; ++i) if (instances[i].imuConnected) return true
        return false
    }
    readonly property int imuCount: {
        var n = 0
        for (var i = 0; i < instances.length; ++i) if (instances[i].imuConnected) ++n
        return n
    }

    // ── Script controls ──────────────────────────────────────────────────
    // setSelected(index, true) connects the device after `connectDelayMs` (0 = at once);
    // false leaves it selected-but-connecting until the test calls connectDevice().
    property bool autoConnectOnSelect: true
    property int  connectDelayMs: 0

    // ── Call log ─────────────────────────────────────────────────────────
    property var calls: []
    function _rec(name, args) { calls.push({ name: name, args: args, t: Date.now() }) }
    function countCalls(name) {
        var n = 0
        for (var i = 0; i < calls.length; ++i) if (calls[i].name === name) ++n
        return n
    }
    function callsNamed(name) {
        var out = []
        for (var i = 0; i < calls.length; ++i) if (calls[i].name === name) out.push(calls[i])
        return out
    }

    property Component _witComp: Component { FakeImuInstance {} }
    property Component _hmComp:  Component { FakeHmDevice {} }

    // ── Internal ─────────────────────────────────────────────────────────
    function _find(id) {
        for (var i = 0; i < _devices.length; ++i) if (_devices[i].id === id) return _devices[i]
        return null
    }
    function _touchDevices() { _devices = _devices.slice() }
    function _rebuildInstances() {
        var out = []
        for (var i = 0; i < _devices.length; ++i)
            if (_devices[i].selected && _devices[i].inst) out.push(_devices[i].inst)
        instances = out
        var connecting = false
        for (var j = 0; j < _devices.length; ++j)
            if (_devices[j].selected && _devices[j].inst && !_devices[j].inst.imuConnected) connecting = true
        anyConnecting = connecting
    }
    function _isHmKey(key) { return key.indexOf("#") > 0 }
    function _ownerOf(key) { return _isHmKey(key) ? key.substring(0, key.lastIndexOf("#")) : key }

    // ── Role resolvers (Q_INVOKABLE in the real class) ───────────────────
    readonly property var _slotRoles: ({ A: "leadForearm", B: "leadHand", C: "leadUpperArm" })
    function _roleOfSlot(slot) { return _slotRoles[slot] || "" }

    function placementKeyForRole(role) {
        if (!role) return ""
        var roles = appSettings.imuRoles
        var firstKey = "", presentKey = "", enabledKey = ""
        for (var key in roles) {
            if (roles[key] !== role) continue
            if (firstKey === "") firstKey = key
            var d = _find(_ownerOf(key))
            if (!d || !d.present) continue
            if (sessionImuExcluded.indexOf(d.id) >= 0) {
                if (presentKey === "") presentKey = key
            } else if (enabledKey === "") {
                enabledKey = key
            }
        }
        return enabledKey !== "" ? enabledKey : presentKey !== "" ? presentKey : firstKey
    }
    function deviceIdForRole(role) {
        var key = placementKeyForRole(role)
        return key === "" ? "" : _ownerOf(key)
    }
    function unitLabelForRole(role) {
        var key = placementKeyForRole(role)
        if (key === "" || !_isHmKey(key)) return ""
        return key.substring(key.lastIndexOf("#") + 1) === "palm" ? "Palm" : "Lower arm"
    }
    function deviceForRole(role) {
        var id = deviceIdForRole(role)
        return id === "" ? null : instanceFor(id)
    }
    function instanceForRole(role) {
        var key = placementKeyForRole(role)
        if (key === "") return null
        var d = _find(_ownerOf(key))
        if (!d || !d.selected || !d.inst) return null
        if (_isHmKey(key)) {
            if (d.vendor !== "hackmotion") return null
            return key.substring(key.lastIndexOf("#") + 1) === "palm" ? d.inst.unitPalm
                                                                      : d.inst.unitLowerArm
        }
        if (d.vendor === "hackmotion") return null   // bare key on a wG3: no unit resolvable
        return d.inst
    }
    // imu_role_map.cpp roleForDevice: the lower-arm key, then the bare key, then the palm key.
    function roleForDevice(deviceId) {
        var roles = appSettings.imuRoles
        if (!deviceId) return ""
        if (roles[deviceId + "#lowerArm"] !== undefined) return roles[deviceId + "#lowerArm"]
        if (roles[deviceId] !== undefined) return roles[deviceId]
        if (roles[deviceId + "#palm"] !== undefined) return roles[deviceId + "#palm"]
        return ""
    }
    function roleHolder(role) {
        var key = placementKeyForRole(role)
        if (key === "") return ""
        var d = _find(_ownerOf(key))
        return (d && d.present && sessionImuExcluded.indexOf(d.id) < 0) ? d.id : ""
    }
    // imu_role_map.cpp claimRole + ImuManager::setRoleForDevice, rule for rule:
    //   "" unassigns every key the device can own; an unknown role, or a wG3 asked for anything
    //   but leadForearm, is a caller error (false, no signal); a role held by ANOTHER present,
    //   session-enabled device is refused (false, roleRefused(deviceId, role, holderId) — for a
    //   wG3 the role named may be leadHand); claims by absent owners are displaced (a wG3's pair
    //   together); a present-but-disabled owner's claim is spared. A wG3 writes both unit keys.
    signal roleRefused(string deviceId, string role, string holderId)
    readonly property var placementRoles: ["pelvis", "thorax", "leadUpperArm", "leadForearm", "leadHand"]
    function setRoleForDevice(deviceId, role) {
        _rec("setRoleForDevice", [deviceId, role])
        if (!deviceId) return false
        var cur = appSettings.imuRoles, roles = {}
        for (var k0 in cur) roles[k0] = cur[k0]
        var lower = deviceId + "#lowerArm", palm = deviceId + "#palm"
        if (!role) {
            delete roles[deviceId]; delete roles[lower]; delete roles[palm]
            appSettings.imuRoles = roles
            return true
        }
        if (placementRoles.indexOf(role) < 0) return false
        var self = _find(deviceId)
        var hm = self !== null && self.vendor === "hackmotion"
        if (hm && role !== "leadForearm") return false
        var targets = hm ? [[lower, "leadForearm"], [palm, "leadHand"]] : [[deviceId, role]]
        for (var t = 0; t < targets.length; ++t) {
            for (var key in cur) {
                if (cur[key] !== targets[t][1]) continue
                var owner = _ownerOf(key)
                if (owner === deviceId) continue
                var d = _find(owner)
                if (d && d.present && sessionImuExcluded.indexOf(owner) < 0) {
                    roleRefused(deviceId, targets[t][1], owner)
                    return false
                }
            }
        }
        for (var t2 = 0; t2 < targets.length; ++t2) {
            for (var key2 in cur) {
                if (cur[key2] !== targets[t2][1]) continue
                var owner2 = _ownerOf(key2)
                if (owner2 === deviceId) continue
                var d2 = _find(owner2)
                if (d2 && d2.present) continue
                delete roles[key2]
                if (_isHmKey(key2)) { delete roles[owner2 + "#lowerArm"]; delete roles[owner2 + "#palm"] }
            }
        }
        delete roles[deviceId]; delete roles[lower]; delete roles[palm]
        for (var t3 = 0; t3 < targets.length; ++t3) roles[targets[t3][0]] = targets[t3][1]
        appSettings.imuRoles = roles
        return true
    }
    readonly property var rolesInSession: {
        var roles = appSettings.imuRoles
        var _dep = sessionImuExcluded
        var out = []
        for (var key in roles) {
            var d = _find(_ownerOf(key))
            if (d && d.present && sessionImuExcluded.indexOf(d.id) < 0 && out.indexOf(roles[key]) < 0)
                out.push(roles[key])
        }
        return out
    }

    function instanceFor(deviceId) {
        var d = _find(deviceId)
        return (d && d.selected && d.inst) ? d.inst : null
    }
    // imu_roles::sensorDisplayName, rule for rule (imu_role_map.h): the coach's alias (not the
    // seeded "<description> <id>"), else the description — a listed device's own, else the one
    // remembered in its appSettings.imuAlias key ("<description>|<id>") — else "HackMotion wG3"
    // for a wG3 unit key in the roles, else "sensor". UUID-shaped tokens are stripped.
    function _stripIds(s) {
        s = (s || "").replace(/\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?/g, "")
        return s.replace(/\s+/g, " ").trim()
    }
    function displayNameForDevice(deviceId) {
        var d = _find(deviceId), desc = "", alias = ""
        var aliases = appSettings.imuAlias || ({})
        if (d) {
            desc = d.description
            alias = d.alias || aliases[desc + "|" + deviceId] || ""
        } else {
            for (var key in aliases)
                if (key.length > deviceId.length && key.slice(-(deviceId.length + 1)) === "|" + deviceId) {
                    desc = key.slice(0, key.length - deviceId.length - 1); alias = aliases[key]; break
                }
        }
        if (alias === desc + " " + deviceId) alias = ""
        alias = _stripIds(alias)
        if (alias !== "") return alias
        desc = _stripIds(desc)
        if (desc !== "") return desc
        var roles = appSettings.imuRoles
        if (roles[deviceId + "#lowerArm"] !== undefined || roles[deviceId + "#palm"] !== undefined)
            return "HackMotion wG3"
        return "sensor"
    }
    function isHackMotionDevice(deviceId) {
        var d = _find(deviceId)
        return d !== null && d.vendor === "hackmotion"
    }

    // ── Commands (recorded) ──────────────────────────────────────────────
    function setSelected(index, selected) {
        _rec("setSelected", [index, selected])
        var d = _devices[index]
        if (!d) return
        if (selected) {
            d.selected = true
            _ensureInstance(d)
            if (autoConnectOnSelect) {
                if (connectDelayMs <= 0) _setConnected(d, true)
                else {
                    var t = Qt.createQmlObject("import QtQuick; Timer {}", mgr)
                    t.interval = connectDelayMs
                    t.triggered.connect(function() { mgr._setConnected(d, true); t.destroy() })
                    t.start()
                }
            }
        } else {
            _setConnected(d, false)
            d.selected = false
        }
        _touchDevices()
        _rebuildInstances()
    }
    function disconnectAll() {
        _rec("disconnectAll", [])
        _pacedTimer.stop(); _pacedQueue = []; pacedConnectActive = false
        for (var i = 0; i < _devices.length; ++i) {
            if (_devices[i].inst) _setConnected(_devices[i], false)
            _devices[i].selected = false
        }
        _touchDevices()
        _rebuildInstances()
    }
    function setSessionImuEnabled(deviceId, on) {
        _rec("setSessionImuEnabled", [deviceId, on])
        var ex = sessionImuExcluded.slice()
        var i = ex.indexOf(deviceId)
        if (on && i >= 0) ex.splice(i, 1)
        else if (!on && i < 0) ex.push(deviceId)
        sessionImuExcluded = ex
        if (!on) {
            var d = _find(deviceId)
            if (d && d.selected) { _setConnected(d, false); d.selected = false; _rebuildInstances() }
        }
        _touchDevices()
    }
    function rescanImu() { _rec("rescanImu", []) }

    // ── Paced connect (imu_manager.h connectPaced) ───────────────────────
    // The first device at once, each next one gapMs after the previous connect was issued.
    // Ids not enumerated, disabled this session, or already selected are skipped without
    // costing a gap. A second call while active REPLACES the remaining queue.
    property var  _pacedQueue: []
    property int  _pacedGapMs: 2000
    property bool pacedConnectActive: false
    function _pacedEligible(id) {
        var d = _find(id)
        return d !== null && d.present && sessionImuExcluded.indexOf(id) < 0 && !d.selected
    }
    function _pacedNext() {
        var q = _pacedQueue.slice()
        while (q.length > 0 && !_pacedEligible(q[0])) q.shift()
        if (q.length === 0) { _pacedQueue = []; pacedConnectActive = false; return }
        var id = q.shift()
        _pacedQueue = q
        setSelected(_devices.indexOf(_find(id)), true)
        var more = false
        for (var i = 0; i < q.length; ++i) if (_pacedEligible(q[i])) more = true
        pacedConnectActive = more
        if (more) _pacedTimer.restart()
        else _pacedQueue = []
    }
    function connectPaced(deviceIds, gapMs) {
        _rec("connectPaced", [deviceIds, gapMs])
        _pacedGapMs = gapMs !== undefined ? gapMs : 2000
        _pacedTimer.stop()
        _pacedQueue = deviceIds.slice()
        _pacedNext()
    }
    function cancelPacedConnect() {
        _rec("cancelPacedConnect", [])
        _pacedTimer.stop()
        _pacedQueue = []
        pacedConnectActive = false
    }
    property Timer _pacedTimer: Timer {
        interval: mgr._pacedGapMs
        repeat: false
        onTriggered: mgr._pacedNext()
    }

    // ── Test helpers (not recorded) ──────────────────────────────────────
    function addWitmotion(id, alias) {
        var d = { id: id, vendor: "witmotion", alias: alias || "", description: "WT901BLE " + id,
                  transport: "BLE", present: true, selected: false, inst: null }
        var a = _devices.slice(); a.push(d); _devices = a
        return d
    }
    function addHackMotion(id, alias) {
        var d = { id: id, vendor: "hackmotion", alias: alias || "", description: "HackMotion wG3 " + id,
                  transport: "BLE", present: true, selected: false, inst: null }
        var a = _devices.slice(); a.push(d); _devices = a
        return d
    }
    // Witmotion: any letter. HackMotion: "A" fills leadForearm (lower arm) and leadHand (palm)
    // together; "" clears.
    function assign(deviceId, slot) { _place(deviceId, slot) }
    function connectDevice(deviceId) {
        var d = _find(deviceId)
        if (!d) return null
        d.selected = true
        _ensureInstance(d)
        _setConnected(d, true)
        _touchDevices()
        _rebuildInstances()
        return d.inst
    }
    function disconnectDevice(deviceId) {
        var d = _find(deviceId)
        if (!d || !d.inst) return
        _setConnected(d, false)
        _rebuildInstances()
    }
    function device(deviceId) { var d = _find(deviceId); return d ? d.inst : null }

    // Writes the roles map directly (no one-claim rule — a test may stage any placement,
    // including a claim by a device that is not in the list). A letter with no role clears.
    function _place(deviceId, slot) {
        var map = {}
        var cur = appSettings.imuRoles
        for (var k in cur) map[k] = cur[k]
        var d = _find(deviceId)
        var hm = d && d.vendor === "hackmotion"
        delete map[deviceId]
        delete map[deviceId + "#lowerArm"]
        delete map[deviceId + "#palm"]
        var role = _roleOfSlot(slot)
        if (role !== "") {
            if (hm) { map[deviceId + "#lowerArm"] = "leadForearm"; map[deviceId + "#palm"] = "leadHand" }
            else    { map[deviceId] = role }
        }
        appSettings.imuRoles = map
    }
    function _ensureInstance(d) {
        if (d.inst) return
        if (d.vendor === "hackmotion") {
            d.inst = _hmComp.createObject(mgr, { deviceId: d.id })
            d.inst.unitLowerArm.unitId = d.id + "#lowerArm"
            d.inst.unitPalm.unitId     = d.id + "#palm"
        } else {
            d.inst = _witComp.createObject(mgr, { deviceId: d.id, deviceDescription: d.description })
        }
        d.inst.sink = mgr.calls
    }
    function _setConnected(d, on) {
        if (!d.inst) return
        if (d.vendor === "hackmotion") {
            d.inst.setConnected(on)
        } else {
            d.inst.imuConnected = on
            d.inst.stateLabel = on ? "Connected" : "Idle"
        }
        // The real manager re-emits instancesChanged on any imuConnected change.
        _rebuildInstances()
    }
}
