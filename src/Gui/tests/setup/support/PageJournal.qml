// PageJournal — what the Stage 4 test pages (support/pages/) saw happen to them, in order.
//
// Injected as the `pageJournal` context property of a FlowShell, so every page the flow creates
// through the shell's Loader resolves it unqualified. Each event carries a global sequence number
// (ordering within one turn) and testLog.elapsedMs() (wall clock).
//
// `config` steers the pages' contract outputs per step key, read by a binding on every page:
//   config[key] = { canContinue: bool, canSkip: bool }
// The defaults are PERMISSIVE (both true) so a walk needs no setup; a case narrows what it tests.
import QtQuick

QtObject {
    id: j

    property var events: []
    property int seq: 0
    property int alive: 0
    property int maxAlive: 0
    property int created: 0
    property var config: ({})

    function _push(ev, key, arg) {
        seq = seq + 1
        events.push({ ev: ev, key: key, arg: arg, seq: seq, t: testLog.elapsedMs() })
    }
    function born(p) {
        created = created + 1
        alive = alive + 1
        if (alive > maxAlive) maxAlive = alive
        _push("born", p.stepKey, "")
        return created
    }
    function died(p) {
        alive = alive - 1
        _push("died", p.stepKey, "")
    }
    function record(ev, p, arg) { _push(ev, p.stepKey, arg) }

    function setConfig(key, field, value) {
        var next = {}
        for (var k in config) next[k] = config[k]
        var c = {}
        if (next[key]) for (var f in next[key]) c[f] = next[key][f]
        c[field] = value
        next[key] = c
        config = next
    }
    function cfg(key, field, dflt) {
        var c = config[key]
        return c !== undefined && c[field] !== undefined ? c[field] : dflt
    }

    // "ev:arg" strings for one step key, oldest first (born/died carry no arg).
    function trace(key) {
        var out = []
        for (var i = 0; i < events.length; ++i)
            if (events[i].key === key)
                out.push(events[i].arg === "" ? events[i].ev : events[i].ev + ":" + events[i].arg)
        return out
    }
    function count(ev, arg) {
        var n = 0
        for (var i = 0; i < events.length; ++i)
            if (events[i].ev === ev && (arg === undefined || events[i].arg === arg)) ++n
        return n
    }
    function last(ev) {
        for (var i = events.length - 1; i >= 0; --i) if (events[i].ev === ev) return events[i]
        return null
    }
    function clear() { events = []; maxAlive = alive }
}
