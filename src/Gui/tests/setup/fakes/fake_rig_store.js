// The sensors' roles and aliases a FakeRig holds, kept where a read registers no QML dependency (FakeRig.qml, "THE
// SENSORS"). Not a .pragma library: each FakeRig gets its own copy, so one test's Rig never leaks into the next.
var _roles = {}
var _aliases = {}
function setRoles(m) { _roles = m }
function roles() { return _roles }
function setAliases(m) { _aliases = m }
function aliases() { return _aliases }
