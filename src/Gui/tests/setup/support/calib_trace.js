// calib_trace.js — the golden trace of a calibration routine (platform refactor C-9a; docs/design/
// rig_design.md §4.8, "The equivalence test", stage "Now: golden traces"). Beside CalibDriver.qml,
// whose event shape ({t, src, kind, name, value}) it keeps.
//
// Two halves:
//   attachTrunk(o)  runs TrunkRoutine.qml against scripted fakes, DRIVEN BY THE ROUTINE'S OWN TICKS: the
//                   fakes' values for a tick are set in the slot after the tick before, so every run
//                   feeds the routine the same values in the same order, whatever the wall clock did.
//                   Time is the routine's own: the sum of the nominal (unscaled) intervals it consumed
//                   (a tick timer's `tick`, a one-shot's interval, the guide's demo turn) — never a
//                   wall clock. It records the event log, and from it the INPUT SET (every value the
//                   routine read from a fake, when it read it, and the result of each request it
//                   made), as data a C++ test can feed to TrunkProcedure later.
//   project(events, tickMs)  the event log → the ordered list of stages, verdicts, reason codes,
//                   cue codes and the requests made, each time rounded to the pace tick. That list is
//                   what the fixtures freeze and what the C++ procedure is proved against.
//
// ⚠ It relies on the routine's own handlers running BEFORE a slot connected later (Timer.onTriggered,
// the Connections on guide.finished, imuConnected and the voice): Qt calls slots in connection order,
// and the routine connects its own when it is created. attachTrunk asserts it on every one-shot timer
// and every guide finish (`orderFault`), so a change in that order fails loudly, never silently.
//
// Words are never compared as words: each routine line maps to a code (WORDS below). A line not in the
// table is kept verbatim ("text:…"), never dropped; a C++ record's own failure words are "record:…"
// (they are an input: the result of the request).

// ── Words → codes (TrunkRoutine.qml's qsTr lines and spoken_instructions.js, verbatim) ─────────────────
var WORDS = {
    "Hip sensor isn't upright": "pelvis.notUpright",
    "Chest sensor isn't upright": "thorax.notUpright",
    "Hip sensor is moving": "pelvis.moving",
    "Chest sensor is moving": "thorax.moving",
    "Chest sensor gives no turn reading — waiting for one": "thorax.noHeading",
    "Waiting for your backswing turn": "waitTurn.away",
    "Waiting for your follow-through turn": "waitTurn.through",
    "No hip sensor in this session — choose one on the Sensors step": "missing.pelvis.absent",
    "No chest sensor in this session — choose one on the Sensors step": "missing.thorax.absent",
    "Hip sensor is not connected — connect it on the Sensors step, then Recalibrate": "missing.pelvis.notConnected",
    "Chest sensor is not connected — connect it on the Sensors step, then Recalibrate": "missing.thorax.notConnected",
    "Hip sensor lost its connection — reconnect it on the Sensors step, then Recalibrate": "linkLost.pelvis",
    "Chest sensor lost its connection — reconnect it on the Sensors step, then Recalibrate": "linkLost.thorax",
    "Hip sensor did not pass stand tall": "standTallFailed.pelvis",
    "Chest sensor did not pass stand tall": "standTallFailed.thorax",
    "The turn could not be checked — nothing recorded it": "turnUnchecked",
    "The turn did not pass — once more": "turnFailed",
    "Set up as a right-handed golfer — if that is wrong, change it in the golfer's profile, then Recalibrate.": "handHint.right",
    "Set up as a left-handed golfer — if that is wrong, change it in the golfer's profile, then Recalibrate.": "handHint.left",
    "Then stand tall again: a sensor that has moved needs a fresh stand tall.": "reseat",
    "Start your backswing turn.": "say.start.away",
    "Start your follow-through turn.": "say.start.through",
    "Turn back to address.": "say.turnBack"
}
function code(line, records) {
    if (WORDS[line] !== undefined) return WORDS[line]
    if (records && records.indexOf(line) >= 0) return "record:" + line
    return "text:" + line
}
function codes(text, records) {
    if (text === "" || text === undefined || text === null) return []
    return String(text).split("\n").map(function(l) { return code(l, records) })
}

// ── Numbers ───────────────────────────────────────────────────────────────────────────────────────
function num(v) { return (typeof v === "number" && isFinite(v)) ? v : null }      // NaN / ±∞ → null (no reading)
function deg(s) {                                                                  // "12.3deg" | "none" → 12.3 | null
    if (s === undefined || s === "none") return null
    var m = /^(-?[0-9.]+)deg$/.exec(s)
    return m ? Number(m[1]) : s
}
function r3(v) { return Math.round(v * 1000) / 1000 }

// ── The routine's timers (TrunkRoutine.qml, by name) and the pace key of each one-shot's interval ──
var TICK_TIMERS = ["_readyTimer", "_holdTimer", "_armTimer", "_turnTimer"]
var ONE_SHOTS = { _introTimer: "introStart", _settleTimer: "settle", _demoPauseTimer: "demoPause",
                  _nextTimer: "next", _retryTimer: "retry" }
function timerName(n) { return n.replace(/^_/, "").replace(/Timer$/, "") }

// ── The run ───────────────────────────────────────────────────────────────────────────────────────
// o: { routine, pelvis, thorax (FakeImuInstance), recorder (FakeTrunkCeremony | null), guide (FakeGuide),
//      log (FakeAppLog), voice (FakeTts | null), spec }
// spec: { on: [{ at: "stage/sub", visit: n (omit = every visit), steps: [...] }],
//         turns: [steps, …] (one per armed entry, then defaultTurn unless golfer === false),
//         golfer: true }
// A step: { n: ticks (default 1; 0 = actions only), gyro (both sensors, °/s), pg / tg (one), long / pl /
//   tl (long axis from up, °), th / ph (heading, absolute; NaN = no reading), chest / hips (the turn
//   from the segment's start, + = the way the step asks; hips default chest / 2), do: action | [actions] }
// Actions: begin, reset, restore, leave (active false), enter (active true), pelvisDown / pelvisUp,
//   thoraxDown / thoraxUp, noPelvis / noThorax / bothSensors (the segments), guideReady / guideNotReady,
//   voiceIdle / voiceBusy, latency:<ms>.
var DEFAULT_TURN = [
    { n: 3, chest: 0,  gyro: 0 },  { n: 2, chest: 15, gyro: 60 }, { n: 2, chest: 45, gyro: 80 },
    { n: 2, chest: 75, gyro: 80 }, { n: 3, chest: 90, gyro: 40 }, { n: 2, chest: 60, gyro: 80 },
    { n: 2, chest: 30, gyro: 80 }, { n: 2, chest: 8,  gyro: 40 }, { n: 1, chest: 3,  gyro: 0 }]

function attachTrunk(o) {
    var r = o.routine, pel = o.pelvis, tho = o.thorax, cer = o.recorder, guide = o.guide, voice = o.voice
    var spec = o.spec || {}
    var scale = r._p.scale
    var rig = {
        events: [], clock: 0, sink: [], visits: {}, turnsUsed: 0, seg: null, queue: [], busy: false,
        finished: false, orderFault: "", pending: null, inputCount: 0, segments: o.segments,
        lastState: {}, maxInputs: o.maxInputs || 8000
    }
    var devName = {}
    devName[pel.deviceId] = "pelvis"
    devName[tho.deviceId] = "thorax"

    function ev(kind, name, value) { rig.events.push({ t: rig.clock, src: "trunk", kind: kind, name: name, value: value }) }

    // What the routine can read from each sensor now (trunkHeadingDeg is the chest's turn reading; the
    // hip's heading is never read).
    function sensor(s, withHeading) {
        var o = { gyro: num(s.gyroDps), long: num(s.longAxisFromUpDeg), link: s.imuConnected === true }
        if (withHeading) o.heading = num(Number(s.trunkHeadingDeg))
        return o
    }
    function snapshot() {
        var s = { pelvis: sensor(pel, false), thorax: sensor(tho, true) }
        if (voice) s.voice = { ready: voice.ttsReady === true, active: voice.ttsActive === true }
        return s
    }
    function world() {
        var segs = r.segments || {}
        var w = { pelvis: { present: !!segs.pelvis, link: pel.imuConnected === true,
                            solved: !!(pel.trunkCalibration && pel.trunkCalibration.valid === true) },
                  thorax: { present: !!segs.thorax, link: tho.imuConnected === true,
                            solved: !!(tho.trunkCalibration && tho.trunkCalibration.valid === true) } }
        w.recorder = cer ? { recording: cer.recording === true, step: cer.currentStep } : null
        w.guideReady = guide.ready === true
        return w
    }

    // ── Outputs: one ordered timeline from the fakes' shared sink ──
    pel.sink = rig.sink; tho.sink = rig.sink
    if (cer) cer.sink = rig.sink
    guide.sink = rig.sink
    o.log.sink = rig.sink
    if (voice) voice.called.connect(function(name, text) {
        if (name !== "prepare") rig.sink.push({ name: "voice." + name, args: [text] })
    })

    // A request's result, as the routine got it: only the fields it reads (the fakes add clocks).
    function solveResult(role) {
        var rec = (r.standTallResult || {})[role] || {}
        return { valid: rec.valid === true, failure: rec.failure === undefined ? "" : rec.failure }
    }
    function turnResult(dir) {
        var rec = (r.turnResults || {})[dir] || {}
        return { valid: rec.valid === true, cause: rec.cause === undefined ? null : rec.cause,
                 wrongWay: rec.wrongWay === true, failure: rec.failure === undefined ? "" : rec.failure }
    }
    function drain() {
        var entered = []
        while (rig.sink.length > 0) {
            var e = rig.sink.shift()
            if (e.name === "log") {
                ev("log", "line", e.args[0])
                var m = /^\[Calib\] trunk stage=(\S+)(?: step=(\S+))?/.exec(e.args[0])
                if (m) entered.push(m[1] + "/" + (m[2] || ""))
                continue
            }
            var dot = e.name.indexOf(".")
            var who = e.name.slice(0, dot), call = e.name.slice(dot + 1)
            if (who === "signal") { ev("signal", call, true); continue }
            if (devName[who] !== undefined) who = devName[who]
            var args = e.args
            if (who === "guide") {
                var g = e.args[0]
                args = g.name === "trunkPose" ? [g.hips, g.chest, g.crossed]
                     : g.name === "trunkAnimate" ? [g.hips, g.chest, g.crossed, Math.round(g.ms / scale)] : []
            } else if (who === "recorder" && call === "attachResult") {
                args = [e.args[0], e.args[1].role || ""]
            }
            ev("call", who + "." + call, args)
            // The request's result is the routine's next input.
            if (call === "calibrateTrunk") input("solve", { role: e.args[0], result: solveResult(e.args[0]) })
            if (who === "recorder" && call === "checkTurn")
                input("turnCheck", { dir: e.args[0], leftHanded: e.args[1], result: turnResult(e.args[0]) })
        }
        return entered
    }
    function stateDiff() {
        var s = { stage: r.stage, substage: r.substage, waitReason: r.waitReason, message: r.message,
                  messageKind: r.messageKind, holdProgress: r3(r.holdProgress), turnBackDue: r.turnBackDue,
                  done: r.done, failed: r.failed }
        for (var k in s) if (rig.lastState[k] !== s[k]) { ev("state", k, s[k]); rig.lastState[k] = s[k] }
    }
    function input(name, value) {
        var rec = { t: rig.clock, ev: name }
        for (var k in value) rec[k] = value[k]
        ev("input", name, rec)
        rig.inputCount += 1
    }

    // ── The scripted world ──
    function expand(steps) {
        var out = [], acts = []
        for (var i = 0; i < steps.length; ++i) {
            var st = steps[i], n = st.n === undefined ? 1 : st.n
            var d = st.do === undefined ? [] : (Array.isArray(st.do) ? st.do : [st.do])
            if (n === 0) { acts = acts.concat(d); continue }
            for (var j = 0; j < n; ++j) out.push({ set: st, do: j === 0 ? acts.concat(d) : [] })
            acts = []
        }
        if (acts.length > 0) out.push({ set: null, do: acts, last: true })
        return out
    }
    function startSegment(steps) {
        var away = r.stage === "turnAway"
        rig.seg = { steps: expand(steps), k: 0,
                    sign: (away ? -1 : 1) * (r.rightHanded ? 1 : -1),
                    baseT: tho.headingDeg, baseP: pel.headingDeg }
        applyStep()
    }
    function applyStep() {
        var sg = rig.seg
        if (!sg || sg.k >= sg.steps.length) return
        var stp = sg.steps[sg.k], s = stp.set
        if (s) {
            if (s.gyro !== undefined) { pel.gyroDps = s.gyro; tho.gyroDps = s.gyro }
            if (s.pg !== undefined) pel.gyroDps = s.pg
            if (s.tg !== undefined) tho.gyroDps = s.tg
            if (s.long !== undefined) { pel.longAxisFromUpDeg = s.long; tho.longAxisFromUpDeg = s.long }
            if (s.pl !== undefined) pel.longAxisFromUpDeg = s.pl
            if (s.tl !== undefined) tho.longAxisFromUpDeg = s.tl
            if (s.th !== undefined) tho.headingDeg = s.th
            if (s.ph !== undefined) pel.headingDeg = s.ph
            if (s.chest !== undefined) {
                tho.headingDeg = isFinite(s.chest) ? sg.baseT + sg.sign * s.chest : s.chest
                var h = s.hips !== undefined ? s.hips : s.chest / 2
                pel.headingDeg = isFinite(h) ? sg.baseP + sg.sign * h : h
            } else if (s.hips !== undefined) {
                pel.headingDeg = isFinite(s.hips) ? sg.baseP + sg.sign * s.hips : s.hips
            }
        }
        for (var i = 0; i < stp.do.length; ++i) rig.queue.push(stp.do[i])
    }
    function onEntered(key) {
        rig.visits[key] = (rig.visits[key] || 0) + 1
        var v = rig.visits[key], on = spec.on || []
        for (var i = 0; i < on.length; ++i) {
            if (on[i].at === key && (on[i].visit === undefined || on[i].visit === v)) { startSegment(on[i].steps); return }
        }
        if (/\/armed$/.test(key)) {
            var turns = spec.turns || []
            if (rig.turnsUsed < turns.length) { startSegment(turns[rig.turnsUsed]); rig.turnsUsed += 1 }
            else if (spec.golfer !== false) startSegment(DEFAULT_TURN)
        }
    }

    // After the routine has processed one input: its outputs, the world for the next tick, the actions.
    function after(isTick) {
        var entered = drain()
        stateDiff()
        var started = false
        for (var i = 0; i < entered.length; ++i) {
            var before = rig.seg
            onEntered(entered[i])
            if (rig.seg !== before) started = true
        }
        if (isTick && !started && rig.seg) { rig.seg.k += 1; applyStep() }
        rig.pending = snapshot()
        if (rig.busy) return
        rig.busy = true
        while (rig.queue.length > 0) { var a = rig.queue.shift(); perform(a) }
        rig.busy = false
        rig.pending = snapshot()
        checkFinished()
    }
    function checkFinished() {
        if (rig.queue.length > 0) return
        var terminal = r.stage === "done" || r.stage === "failed" || r.stage === "idle"
        var any = false
        for (var i = 0; i < TICK_TIMERS.length; ++i) if (r[TICK_TIMERS[i]].running) any = true
        for (var n in ONE_SHOTS) if (r[n].running) any = true
        if (terminal && !any && !guide.inFlight) rig.finished = true
        if (rig.inputCount > rig.maxInputs) { rig.orderFault = "more than " + rig.maxInputs + " inputs"; rig.finished = true }
    }
    function perform(a) {
        var w = world()
        var m = /^latency:(\d+)$/.exec(a)
        if (a === "begin" || a === "reset" || a === "restore") {
            input("command", { name: a, world: w }); r[a]()
        } else if (a === "leave" || a === "enter") {
            input("command", { name: "active", value: a === "enter", world: w }); r.active = a === "enter"
        } else if (/^(pelvis|thorax)(Down|Up)$/.test(a)) {
            var which = a.indexOf("pelvis") === 0 ? "pelvis" : "thorax", up = /Up$/.test(a)
            input("link", { sensor: which, up: up, world: w })
            ;(which === "pelvis" ? pel : tho).imuConnected = up
        } else if (a === "noPelvis" || a === "noThorax" || a === "bothSensors") {
            input("segments", { pelvis: a !== "noPelvis", thorax: a !== "noThorax" })
            r.segments = a === "noPelvis" ? { thorax: tho } : a === "noThorax" ? { pelvis: pel } : { pelvis: pel, thorax: tho }
        } else if (a === "guideReady" || a === "guideNotReady") {
            input("guide", { ready: a === "guideReady" }); guide.ready = a === "guideReady"
        } else if (a === "voiceIdle" || a === "voiceBusy") {
            input("speech", { name: a === "voiceIdle" ? "idle" : "busy" }); voice.ttsActive = a === "voiceBusy"
        } else if (m) {
            input("speech", { name: "latency", ms: Number(m[1]) }); voice.finish(Number(m[1]))
        } else {
            rig.orderFault = "unknown action " + a
            return
        }
        after(false)
    }

    // ── Hooks: the routine's timers, the guide's finish ──
    function hookTick(name) {
        r[name].triggered.connect(function() {
            rig.clock += r._p.tick
            var rec = { timer: timerName(name), dt: r._p.tick }
            var s = rig.pending || snapshot()
            for (var k in s) rec[k] = s[k]
            input("tick", rec)
            after(true)
        })
    }
    function hookOneShot(name) {
        r[name].triggered.connect(function() {
            // The routine's handler has run: its one-shot's state is behind it.
            var stale = (name === "_introTimer" && r.stage === "intro")
                     || (name === "_settleTimer" && r.substage === "settle")
                     || (name === "_demoPauseTimer" && r._anim === "pause")
                     || (name === "_nextTimer" && r.substage === "passed")
                     || (name === "_retryTimer" && r.substage === "retry")
            if (stale && rig.orderFault === "") rig.orderFault = name + ": the test's slot ran before the routine's handler"
            var dt = r._p[ONE_SHOTS[name]]
            rig.clock += dt
            input("timer", { timer: timerName(name), dt: dt })
            after(false)
        })
    }
    for (var i = 0; i < TICK_TIMERS.length; ++i) hookTick(TICK_TIMERS[i])
    for (var n in ONE_SHOTS) hookOneShot(n)
    var lastAnimMs = 0
    guide.finished.connect(function() {
        if (r._anim === "out" && rig.orderFault === "") rig.orderFault = "guide.finished: the test's slot ran before the routine's"
        for (var j = guide.log.length - 1; j >= 0; --j)
            if (guide.log[j].name === "trunkAnimate") { lastAnimMs = Math.round(guide.log[j].ms / scale); break }
        rig.clock += lastAnimMs
        input("guideFinished", { dt: lastAnimMs })
        after(false)
    })
    // Through the sink, so it sits in order with the lines and calls of the input that caused it.
    r.completed.connect(function() { rig.sink.push({ name: "signal.completed", args: [] }) })

    rig.start = function(first) {
        stateDiff()
        rig.pending = snapshot()
        rig.queue.push(first || "begin")
        after(false)
    }
    rig.inputs = function() {
        return rig.events.filter(function(e) { return e.kind === "input" }).map(function(e) { return e.value })
    }
    return rig
}

// ── The projection ────────────────────────────────────────────────────────────────────────────────
// Each entry: { t, own, … }. `own` says whose duty it is (DUTIES-TrunkRoutine.qml.md, PR-45): "P" the
// procedure (stages, verdicts, reasons, cues, progress), "rec" the recorder bracket (F4), "sensor" the
// sensor calls (the solve is P's request; the clears F5), "guide" the guide (F2, stays in QML),
// "speech" the voice (F1, stays in QML), "entry" the entry points' lines (base), "D" the driver's
// wait log (F3). C-11a can compare `own: "P"` alone.
function parseWhy(why) {
    var o = {}, m
    if (why === undefined || why === "") return o
    if (why === "upright and still") o.why = "uprightAndStill"
    else if ((m = /^pelvis=(pass|fail) thorax=(pass|fail)$/.exec(why))) o.verdict = { pelvis: m[1], thorax: m[2] }
    else if ((m = /^heading=(\S+)$/.exec(why))) o.heading = deg(m[1])
    else if ((m = /^started peak=(\S+)$/.exec(why))) { o.why = "started"; o.peak = deg(m[1]) }
    else if ((m = /^peak=(\S+) turn=(\S+)$/.exec(why))) { o.peak = deg(m[1]); o.turn = deg(m[2]) }
    else if (why === "turn=valid") o.verdict = "valid"
    else if ((m = /^turn=fail cause=(\S*)( back=standTall)?$/.exec(why))) {
        o.verdict = "fail"; o.cause = m[1]; if (m[2]) o.back = "standTall"
    } else if (why === "missing") o.why = "missing"
    else if ((m = /^(pelvis|thorax) disconnected$/.exec(why))) { o.why = "linkLost"; o.sensor = m[1] }
    else o.text = why
    return o
}
var SPEAK_SKIP = { "no voice (text-to-speech is not available)": "noVoice", "voice not ready": "notReady",
                   "voice busy": "busy" }
function parseLine(line, records) {
    var m, o
    var body = line.replace(/^\[Calib\] trunk /, "")
    if (body === line) return { own: "log", text: line }
    if ((m = /^stage=(\S+)(?: step=(\S+))?(?: (.*))?$/.exec(body))) {
        o = { own: "P", stage: m[1], sub: m[2] || "" }
        var w = parseWhy(m[3])
        for (var k in w) o[k] = w[k]
        return o
    }
    if ((m = /^wait step=(\S+) pelvis .*? thorax \S+(?: \S+)?(?: turn=(\S+) peak=(\S+))? — (.*)$/.exec(body))) {
        o = { own: "D", waitLog: m[1] }
        if (m[2] !== undefined) { o.turn = deg(m[2]); o.peak = deg(m[3]) }
        o.reasons = m[4].split("; ").map(function(l) { return code(l, records) })
        return o
    }
    if ((m = /^cue turnBack reached (\S+) at turn=(\S+) peak=(\S+)$/.exec(body)))
        return { own: "P", cue: "turnBack", why: "reached", at: deg(m[1]), turn: deg(m[2]), peak: deg(m[3]) }
    if ((m = /^cue turnBack stopped at the top at (\S+) turn=(\S+) peak=(\S+)$/.exec(body)))
        return { own: "P", cue: "turnBack", why: "stoppedTop", at: deg(m[1]), turn: deg(m[2]), peak: deg(m[3]) }
    if ((m = /^speak cue=(\S+) (.*)$/.exec(body))) {
        o = { own: "speech", cue: m[1] }
        var rest = m[2], s
        if ((s = /^skipped: (.*)$/.exec(rest))) { o.speech = "skipped"; o.reason = SPEAK_SKIP[s[1]] || ("text:" + s[1]) }
        else if ((s = /^stops the voice first \((.*)\)$/.exec(rest))) { o.speech = "stopsFirst"; o.because = s[1] === "the start line" ? "startLine" : s[1] }
        else if (rest === "waits for the voice to stop") o.speech = "waits"
        else if (rest === "dropped: the turn ended before the voice stopped") o.speech = "dropped"
        else if ((s = /^latency=(\d+)ms$/.exec(rest))) { o.speech = "latency"; o.ms = Number(s[1]) }
        else if ((s = /^"(.*)" \((prepared|not prepared — synthesising now)\)$/.exec(rest))) {
            o.speech = "said"; o.line = code(s[1]); o.prepared = s[2] === "prepared"
        } else o.text = rest
        return o
    }
    if (body === "begin" || body === "reset") return { own: "entry", entry: body }
    if ((m = /^stop reason=(\S+) stage=(\S+)(.*)$/.exec(body))) {
        o = { own: "entry", entry: "stop", reason: m[1], stage: m[2] }
        if (m[3]) o.note = m[3].trim()
        return o
    }
    if ((m = /^restore done=(\S+)(?: solves=(.*))?$/.exec(body))) {
        o = { own: "entry", entry: "restore", done: m[1] === "true" }
        if (m[2] !== undefined) o.solves = m[2] === "both" ? "both" : "notBoth"
        return o
    }
    return { own: "log", text: line }
}
var SENSOR_CALLS = { clearFunctionalCalibration: "clearFunctional", clearCalibration: "clear", calibrateTrunk: "solve",
                     confirmTrunkCalibration: "confirm" }
function project(events, tickMs) {
    var out = [], records = []
    var round = function(t) { return tickMs > 0 ? Math.round(t / tickMs) * tickMs : t }
    var lineStage = null
    for (var i = 0; i < events.length; ++i) {
        var e = events[i], t = round(e.t), o = null
        if (e.kind === "input") {
            // A record's own failure words are input; collect them so a message line can name its source.
            var res = e.value.result
            if (res && typeof res.failure === "string" && res.failure !== "") res.failure.split("\n").forEach(function(l) { records.push(l) })
            continue
        }
        if (e.kind === "log") {
            o = parseLine(e.value, records)
            if (o.stage !== undefined) lineStage = o.stage + "/" + o.sub
        } else if (e.kind === "call") {
            var dot = e.name.indexOf("."), who = e.name.slice(0, dot), call = e.name.slice(dot + 1)
            if (who === "recorder") { o = { own: "rec", rec: call }; if (e.value.length > 0) o.args = e.value }
            else if (who === "guide") { o = { own: "guide", guide: call }; if (e.value.length > 0) o.args = e.value }
            else if (who === "voice") { o = { own: "speech", voice: call }; if (e.value[0]) o.line = code(e.value[0]) }
            else { o = { own: "sensor", sensor: who, call: SENSOR_CALLS[call] || call } }
        } else if (e.kind === "signal") {
            o = { own: "P", signal: e.name }
        } else if (e.kind === "state") {
            var n = e.name, v = e.value
            if (n === "stage" || n === "substage") {
                // A stage change the routine did not log (restore, and the clear to idle): every other
                // is its stage line's.
                var cur = (lineStage || "/").split("/")
                if ((n === "stage" ? cur[0] : cur[1]) === v) continue
                o = { own: "P", state: n, value: v }
                lineStage = n === "stage" ? v + "/" + cur[1] : cur[0] + "/" + v
            }
            else if (n === "waitReason") o = { own: "P", wait: codes(v, records) }
            else if (n === "message") o = { own: "P", message: codes(v, records) }
            else if (n === "messageKind") o = { own: "P", messageKind: v }
            else if (n === "holdProgress") o = { own: "P", hold: v }
            else { o = { own: "P" }; o[n] = v }
        }
        if (o === null) continue
        var entry = { t: t }
        for (var k in o) entry[k] = o[k]
        out.push(entry)
    }
    return out
}

// ── Files ─────────────────────────────────────────────────────────────────────────────────────────
// One line per item of each array, so a fixture diff reads entry by entry.
function serialize(doc) {
    var keys = Object.keys(doc), parts = []
    for (var i = 0; i < keys.length; ++i) {
        var v = doc[keys[i]], s
        if (Array.isArray(v)) s = v.length === 0 ? "[]" : "[\n    " + v.map(function(x) { return JSON.stringify(x) }).join(",\n    ") + "\n  ]"
        else s = JSON.stringify(v)
        parts.push("  " + JSON.stringify(keys[i]) + ": " + s)
    }
    return "{\n" + parts.join(",\n") + "\n}\n"
}
// The first place two arrays of entries differ, for a failure message.
function firstDiff(a, b) {
    var n = Math.max(a.length, b.length)
    for (var i = 0; i < n; ++i) {
        var x = JSON.stringify(a[i]), y = JSON.stringify(b[i])
        if (x !== y) return "at " + i + ":\n  got      " + x + "\n  expected " + y
    }
    return ""
}
