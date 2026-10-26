// Fake TrunkCeremonyRecorder (src/Gui/imu/trunk_ceremony_recorder.h) — the "trunkCeremony" context
// property. No samples: it keeps the recorder's STATE (recording, pendingWrite, currentStep) the
// way the real one moves it, records every call in order, and answers checkTurn() from a script.
//
//   calls       every call, oldest first: { name, args, t }
//   order()     the calls as one string each — "begin", "stepStart:standTall",
//               "attachResult:standTall:pelvis", "checkTurn:away", "stepEnd:turnAway", "end",
//               "discard" — so a case can compare the whole sequence at once
//   turnScript  checkTurn's answers, oldest first; each is merged over a VALID record of the real
//               keys (trunk_ceremony_file.h turnRecordToMap). Spent → every turn is valid.
//   beginResult what begin() returns (the real one: false when neither trunk sensor is live)
import QtQuick

QtObject {
    id: tcr

    property bool   recording:    false
    property bool   pendingWrite: false
    property int    laneCount:    0
    property string currentStep:  ""

    property bool beginResult: true
    property var  turnScript: []
    property var  calls: []

    function _rec(name, args) { calls.push({ name: name, args: args, t: Date.now() }) }
    function order() {
        return calls.map(function(c) {
            if (c.name === "attachResult") return "attachResult:" + c.args[0] + ":" + (c.args[1].role || "")
            if (c.name === "checkTurn")    return "checkTurn:" + c.args[0]
            return c.args.length > 0 && typeof c.args[0] === "string" ? c.name + ":" + c.args[0] : c.name
        })
    }
    function count(name) {
        var n = 0
        for (var i = 0; i < calls.length; ++i) if (calls[i].name === name) ++n
        return n
    }
    function reset() { calls = []; recording = false; pendingWrite = false; currentStep = ""; turnScript = [] }

    // ── The recorder's API ───────────────────────────────────────────────
    function begin() {
        _rec("begin", [])
        pendingWrite = false
        currentStep = ""
        recording = beginResult
        laneCount = beginResult ? 2 : 0
        return beginResult
    }
    function stepStart(name) {
        _rec("stepStart", [name])
        if (recording) currentStep = name
    }
    function stepEnd(name) {
        _rec("stepEnd", [name])
        if (recording && currentStep === name) currentStep = ""
    }
    function mark(name)                 { _rec("mark", [name]) }
    function attachResult(stepName, map) { _rec("attachResult", [stepName, map]) }
    function end() {
        _rec("end", [])
        if (!recording) return
        recording = false
        pendingWrite = true
        currentStep = ""
    }
    function discard() {
        _rec("discard", [])
        recording = false
        pendingWrite = false
        currentStep = ""
    }
    function checkTurn(direction, leftHanded) {
        _rec("checkTurn", [direction, leftHanded])
        var sign = (direction === "away" ? -1 : 1) * (leftHanded ? -1 : 1)
        var sensor = function(range) {
            return { samples: 400, rangeDeg: range, turnDeg: range * sign, siTurnDeg: range * sign,
                     axisFromVerticalDeg: 3, rangeOk: true, senseOk: true, axisOk: true, failure: "",
                     serial: "", deviceId: "", trunkCalibrated: true, windowStartUs: 0, windowEndUs: 0,
                     alignA: [1, 0, 0, 0], mountM: [1, 0, 0, 0] }
        }
        var rec = { valid: true, failure: "", cause: "", direction: direction, leftHanded: leftHanded,
                    expectedSign: sign, chestOverHips: 2, orderOk: true, wrongWay: false,
                    pelvis: sensor(45), thorax: sensor(90) }
        if (turnScript.length > 0) {
            var s = turnScript[0]
            turnScript = turnScript.slice(1)
            for (var k in s) rec[k] = s[k]
        }
        return rec
    }
}
