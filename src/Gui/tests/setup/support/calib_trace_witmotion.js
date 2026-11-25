// calib_trace_witmotion.js — the golden trace of the Witmotion arm routine (platform refactor C-9b;
// docs/design/rig_design.md §4.8, "The equivalence test"). The method is calib_trace.js's (C-9a), kept
// beside it in its own file so the trunk's paths stay as they were:
//
//   attachWitmotion(o)  runs WitmotionArmRoutine.qml against scripted FakeImuInstances, DRIVEN BY THE
//                   ROUTINE'S OWN TICKS: the fakes' values for a tick are set in the slot after the tick
//                   before, so every run feeds the routine the same values in the same order, whatever the
//                   wall clock did. Time is the routine's own: the sum of the nominal (unscaled) intervals
//                   it consumed (a hold timer's `tick`, a one-shot's interval, the guide's animation).
//                   It records the event log ({t, src, kind, name, value}, CalibDriver's shape) and from it
//                   the INPUT SET: every value the routine read from a fake, when it read it (the lead's
//                   rate and quaternion on every hold tick, each segment's quaternion, link and anatQuat,
//                   the guide's ready), and the result of each sensor call (the mount figures after each
//                   refineMountAboutLongAxis), as data a C++ test can feed to WitmotionArmProcedure later.
//   project(events, tickMs, scale)  the event log → the ordered list of stages, verdicts, reason codes,
//                   the requests made (set nominal, refine with φ, the clears), the guide's calls and the
//                   hold progress, each time rounded to the pace tick. What the fixtures freeze.
//
// ⚠ It relies on the routine's handlers running BEFORE a slot connected later (Timer.onTriggered, the
// base's Connections on guide.finished): Qt calls slots in connection order. attachWitmotion asserts it on
// every one-shot and every guide finish (`orderFault`).
//
// ⚠ Numbers. The routine holds quaternions as QML `quaternion` (single-precision floats), so the averaged
// quaternions, the references and φ come out of float arithmetic (φ = 40° reads 39.999998). The projection
// rounds quaternions to 1e-6, φ to 1e-4 and progress to 1e-3; a port that computes in doubles must compare
// quaternions at ±1e-5 and φ at ±1e-3, not as text. The inputs hold the doubles the fakes were given (a quaternion the routine wraps becomes a float
// when it reads it). A NaN reading is null in the files.

// ── Numbers and quaternions ──────────────────────────────────────────────────────────────────────────
function num(v) { return (typeof v === "number" && isFinite(v)) ? v : null }
function r3(v) { return Math.round(v * 1000) / 1000 }
function r4(v) { return v === null ? null : Math.round(v * 1e4) / 1e4 }
function r6(v) { return v === null ? null : Math.round(v * 1e6) / 1e6 }
function qArr(q) { return (q === null || q === undefined) ? null : [q.scalar, q.x, q.y, q.z] }
function q6(q) { var a = qArr(q); return a === null ? null : a.map(function(v) { return r6(num(v)) }) }
function q4(q) { var a = qArr(q); return a === null ? null : a.map(function(v) { return Math.round(v * 1e4) / 1e4 }) }

// A rotation of `deg` about the axis (x, y, z) (normalised here), as [w, x, y, z].
function qAxis(x, y, z, deg) {
    var n = Math.sqrt(x * x + y * y + z * z), h = deg * Math.PI / 360, s = Math.sin(h) / n
    return [Math.cos(h), s * x, s * y, s * z]
}
function qMul(a, b) {
    return [a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
            a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
            a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]]
}
function qNeg(a) { return [-a[0], -a[1], -a[2], -a[3]] }
// A held arm's small sway: sample i of `base`, turned by under 1° about x, y or z in turn (a fixed table).
var SWAY_DEG = [0.4, -0.3, 0.2, -0.5, 0.1, 0.6, -0.2]
function sway(base, i) {
    var ax = i % 3
    return qMul(base, qAxis(ax === 0 ? 1 : 0, ax === 1 ? 1 : 0, ax === 2 ? 1 : 0, SWAY_DEG[i % SWAY_DEG.length]))
}
// The rate a still arm reads (°/s), sample i: under the 15 °/s ceiling, never on it.
var STILL_RATE = [2.1, 4.8, 0.6, 7.3, 3.4, 9.9, 1.2, 5.5, 12.4, 0.0, 6.6, 3.0, 8.1, 14.2, 2.7]
function stillRate(i) { return STILL_RATE[i % STILL_RATE.length] }
// An anatomical quaternion whose abduction angle (WitmotionArmRoutine._phiFromAbduction) is `phiDeg`: a
// turn of `thetaDeg` about the axis (sin φ, 0, cos φ); `neg` gives the same rotation with a negative scalar.
function anatFor(phiDeg, thetaDeg, neg) {
    var p = phiDeg * Math.PI / 180, q = qAxis(Math.sin(p), 0, Math.cos(p), thetaDeg === undefined ? 30 : thetaDeg)
    return neg ? qNeg(q) : q
}

// The sensors' poses: arm hanging (A forearm, B hand, C upper arm), then at the T-pose.
var POSE = {
    down:  { a: qAxis(1, 0, 0, 90),  b: qAxis(1, 0.1, 0, 84), c: qAxis(1, -0.1, 0, 96) },
    tpose: { a: qMul(qAxis(1, 0, 0, 90), qAxis(0, 0, 1, 88)), b: qMul(qAxis(1, 0.1, 0, 84), qAxis(0, 0, 1, 86)),
             c: qMul(qAxis(1, -0.1, 0, 96), qAxis(0, 0, 1, 91)) }
}
// `count` still hold ticks at a pose: each sensor swaying, the lead's rate under the ceiling.
function still(pose, count, from) {
    var out = [], p = POSE[pose]
    for (var i = 0; i < count; ++i) {
        var k = (from || 0) + i
        out.push({ rate: stillRate(k), q: sway(p.a, k), qb: sway(p.b, k + 1), qc: sway(p.c, k + 2) })
    }
    return out
}
function defaultP1() { return still("down", 20) }
function defaultP2() { return still("tpose", 20) }

// ── The routine's timers (WitmotionArmRoutine.qml, by name) and the pace key of each one-shot ────────────
var TICK_TIMERS = ["_phase1HoldTimer", "_stabilityHoldTimer"]
var ONE_SHOTS = { _introStartTimer: "introStart", _introReadyTimer: "introReady", _phase1MinHoldTimer: "minHold",
                  _captureTransitionTimer: "captureTransition", _raiseReadyTimer: "raiseReady" }
function timerName(n) { return n.replace(/^_/, "").replace(/Timer$/, "") }
var SLOTS = ["a", "b", "c"]

// ── The run ─────────────────────────────────────────────────────────────────────────────────────────
// o: { routine, all: { a, b, c } (FakeImuInstance; the routine's `segments` say which it is given), guide
//      (FakeGuide), log (FakeAppLog), spec }
// spec: { on: [{ at: "stage/sub", visit: n (omit = every visit), steps: [...] }],
//         p1: [steps | null, …] (one per phase-1 hold, then defaultP1), p2: likewise for phase 2,
//         mounts: [{ a: { dev, grav }, b, c } | null, …] (each phase 2's refine results; default dev 3, grav 4) }
// The keys ("stage/sub"): intro/wait (for introStart; the guide may not be ready), intro/up, intro/down,
//   intro/settle, phase1/settle (minHold), phase1/hold, captured/, raise/anim, raise/settle, phase2/, done/,
//   mountFailed/, failed/, idle/.
// A step: { n: ticks (default 1; 0 = actions only), rate (the lead's angularVelocityDps), rateB / rateC and gyro
//   (B's and C's angularVelocityDps, every unit's gyroDps: values the routine does not read), q / qb / qc (each
//   segment's quaternion, [w, x, y, z]), anat: { a, b, c } (anatQuat), do: action | [actions] }
// Actions: begin, reset, restore, stop (stop("back")), leave (active false), enter (active true),
//   leadDown / leadUp, bDown / bUp, cDown / cUp, guideReady / guideNotReady, legacy (the lead's legacy
//   two-pose calibration set, as restore() reads it), segments:<a|ab|abc|…> (the host hands other units),
//   idle:<ms> (time passes with nothing to do: no routine timer may be running), whenSettled (the rest of
//   the actions wait until no timer runs and the guide is still).
function attachWitmotion(o) {
    var r = o.routine, all = o.all, guide = o.guide
    var spec = o.spec || {}
    var scale = r._p.scale
    var rig = {
        events: [], clock: 0, sink: [], visits: {}, seg: null, queue: [], busy: false, finished: false,
        orderFault: "", pending: null, inputCount: 0, lastState: {}, lastKey: "", idle: null, deferred: null,
        p1Used: 0, p2Used: 0, animMs: 0, animWas: "", unreadSet: false, maxInputs: o.maxInputs || 8000
    }
    var devName = {}
    SLOTS.forEach(function(k) { if (all[k]) devName[all[k].deviceId] = k })

    function ev(kind, name, value) { rig.events.push({ t: rig.clock, src: "witmotion", kind: kind, name: name, value: value }) }
    function given(k) { var s = r.segments || {}; return s[k] ? s[k] : null }

    // What the routine can read from each unit it was given now: the link, the quaternion, the anatomical
    // quaternion, and the lead's rate (the only stillness signal it reads).
    function sensor(k) {
        var s = given(k)
        if (!s) return null
        var o2 = { link: s.imuConnected === true, q: [num(s.quatW), num(s.quatX), num(s.quatY), num(s.quatZ)],
                   anat: qArr(s.anatQuat) }
        if (k === "a") o2.rate = num(s.angularVelocityDps)
        return o2
    }
    // Values a set scripts to show the routine does NOT read them (B's and C's rate, every gyroDps): kept
    // apart, under `unread`, and only once a set has scripted one.
    function unread() {
        var u = {}
        SLOTS.forEach(function(k) {
            var s = given(k)
            if (!s) return
            u[k] = { gyro: num(s.gyroDps) }
            if (k !== "a") u[k].rate = num(s.angularVelocityDps)
        })
        return u
    }
    function snapshot() {
        var o2 = { a: sensor("a"), b: sensor("b"), c: sensor("c") }
        if (rig.unreadSet) o2.unread = unread()
        return o2
    }
    function world() {
        var w = {}
        SLOTS.forEach(function(k) {
            var s = given(k)
            w[k] = s ? { link: s.imuConnected === true } : null
        })
        var lead = given("a")
        if (lead) { w.a.calibrated = lead.calibrated === true; w.a.armDown = qArr(lead.calibArmDown); w.a.tPose = qArr(lead.calibArmTPose) }
        w.guideReady = guide.ready === true
        return w
    }

    // ── Outputs: one ordered timeline from the fakes' shared sink ──
    SLOTS.forEach(function(k) { if (all[k]) all[k].sink = rig.sink })
    guide.sink = rig.sink
    o.log.sink = rig.sink

    function drain() {
        while (rig.sink.length > 0) {
            var e = rig.sink.shift()
            if (e.name === "log") { ev("log", "line", e.args[0]); continue }
            var dot = e.name.indexOf(".")
            var who = e.name.slice(0, dot), call = e.name.slice(dot + 1)
            if (who === "signal") { ev("signal", call, true); continue }
            if (who === "guide") {
                var g = e.args[0], args = []
                if (g.name === "pose") args = [q4(g.arm), q4(g.fore)]
                else if (g.name === "animate") {
                    args = [q4(g.arm), Math.round(g.ms / scale)]
                    rig.animMs = Math.round(g.ms / scale); rig.animWas = r._animStage
                }
                ev("call", "guide." + g.name, args)
                continue
            }
            if (devName[who] !== undefined) who = devName[who]
            var a = e.args, out = []
            if (call === "setNominalCalibration") out = [q6(a[0]), a[1]]
            else if (call === "refineMountAboutLongAxis") out = [q6(a[0]), r4(num(a[1])), a[2]]
            ev("call", who + "." + call, out)
            // The call's result is the routine's next input: the two mount figures it reads straight after.
            if (call === "refineMountAboutLongAxis") {
                var inst = all[who]
                input("refine", { sensor: who, result: { dev: num(inst.mountDeviationDeg), grav: num(inst.mountGravityErrorDeg) } })
            }
        }
    }
    // The status view's hold bar (ArmCalibrationStatus.qml holdProgress), computed as it does.
    function statusHold() {
        if (r.calibrationDone) return 1.0
        if (r.calibPhase === 2) return r.phaseProgress
        if (r.calibPhase === 1 && !r._armDownCaptured) return Math.min(r.phase1AccumMs / r._captureHoldMs, 1.0)
        return 0
    }
    function stateDiff() {
        var s = { stage: r.stage, phase: r.calibPhase, anim: r._animStage, minHoldDone: r._phase1MinHoldDone,
                  captured: r._armDownCaptured,
                  accum1: Math.round(r.phase1AccumMs / scale), accum2: Math.round(r.stableAccumMs / scale),
                  progress: r3(r.phaseProgress), hold: r3(statusHold()),
                  failed: r.calibrationFailed, mountFailed: r.mountFailed, message: r.message,
                  messageKind: r.messageKind, done: r.calibrationDone,
                  armDown: q6(r.calibArmDownQuat), tPose: q6(r.calibArmTPoseQuat) }
        for (var k in s) {
            var v = JSON.stringify(s[k])
            if (rig.lastState[k] !== v) { ev("state", k, s[k]); rig.lastState[k] = v }
        }
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
    function setQ(inst, q) { if (inst && q) inst.setQuat(q[0], q[1], q[2], q[3]) }
    function startSegment(steps) {
        rig.seg = { steps: expand(steps), k: 0 }
        applyStep()
    }
    function applyStep() {
        var sg = rig.seg
        if (!sg || sg.k >= sg.steps.length) return
        var stp = sg.steps[sg.k], s = stp.set
        if (s) {
            if (s.rate !== undefined) all.a.angularVelocityDps = s.rate
            if (s.rateB !== undefined) { all.b.angularVelocityDps = s.rateB; rig.unreadSet = true }
            if (s.rateC !== undefined) { all.c.angularVelocityDps = s.rateC; rig.unreadSet = true }
            if (s.gyro !== undefined) { SLOTS.forEach(function(k) { all[k].gyroDps = s.gyro }); rig.unreadSet = true }
            setQ(all.a, s.q); setQ(all.b, s.qb); setQ(all.c, s.qc)
            if (s.anat) SLOTS.forEach(function(k) {
                if (s.anat[k] && all[k]) all[k].anatQuat = Qt.quaternion(s.anat[k][0], s.anat[k][1], s.anat[k][2], s.anat[k][3])
            })
        }
        for (var i = 0; i < stp.do.length; ++i) rig.queue.push(stp.do[i])
    }
    function subKey() {
        var st = r.stage, sub = ""
        if (st === "intro")
            sub = r._animStage === "introUp" ? "up" : r._animStage === "introDown" ? "down" : r._introStarted ? "settle" : "wait"
        else if (st === "phase1") sub = r._phase1MinHoldDone ? "hold" : "settle"
        else if (st === "raise") sub = r._animStage === "raise" ? "anim" : "settle"
        return st + "/" + sub
    }
    function onEntered(key) {
        rig.visits[key] = (rig.visits[key] || 0) + 1
        var v = rig.visits[key], on = spec.on || []
        if (key === "phase2/") {
            // This T-pose's refine results, as the fakes will return them.
            var m = (spec.mounts || [])[rig.p2Used] || {}
            SLOTS.forEach(function(k) {
                var x = m[k] || {}
                if (all[k]) all[k].mountResult = { anatCalibrated: true, dev: x.dev === undefined ? 3 : x.dev,
                                                   grav: x.grav === undefined ? 4 : x.grav }
            })
        }
        var steps = null
        for (var i = 0; i < on.length; ++i)
            if (on[i].at === key && (on[i].visit === undefined || on[i].visit === v)) { steps = on[i].steps; break }
        if (key === "phase1/hold") {
            var p1 = (spec.p1 || [])[rig.p1Used]; rig.p1Used += 1
            if (steps === null) steps = p1 || defaultP1()
        } else if (key === "phase2/") {
            var p2 = (spec.p2 || [])[rig.p2Used]; rig.p2Used += 1
            if (steps === null) steps = p2 || defaultP2()
        }
        if (steps !== null) startSegment(steps)
    }

    function settled() {
        if (rig.queue.length > 0 || rig.idle) return false
        for (var i = 0; i < TICK_TIMERS.length; ++i) if (r[TICK_TIMERS[i]].running) return false
        for (var n in ONE_SHOTS) if (r[n].running) return false
        return !guide.inFlight
    }
    // After the routine has processed one input: its outputs, the world for the next tick, the actions.
    function after(isTick) {
        drain()
        stateDiff()
        var key = subKey(), started = false
        if (key !== rig.lastKey) {
            rig.lastKey = key
            var before = rig.seg
            onEntered(key)
            started = rig.seg !== before
        }
        if (isTick && !started && rig.seg) { rig.seg.k += 1; applyStep() }
        rig.pending = snapshot()
        if (rig.busy) return
        rig.busy = true
        for (;;) {
            while (rig.queue.length > 0 && !rig.idle && rig.orderFault === "") perform(rig.queue.shift())
            if (rig.idle || rig.deferred === null || !settled()) break
            rig.queue = rig.deferred; rig.deferred = null
        }
        rig.busy = false
        rig.pending = snapshot()
        checkFinished()
    }
    function checkFinished() {
        if (rig.deferred === null && settled()) { ev("end", "at", rig.lastKey); rig.finished = true }
        if (rig.orderFault !== "") rig.finished = true
        if (rig.inputCount > rig.maxInputs) { rig.orderFault = "more than " + rig.maxInputs + " inputs"; rig.finished = true }
    }
    function perform(a) {
        var w = world(), m
        if (a === "begin" || a === "reset" || a === "restore") {
            input("command", { name: a, world: w }); r[a]()
        } else if (a === "stop") {
            input("command", { name: "stop", reason: "back", world: w }); r.stop("back")
        } else if (a === "leave" || a === "enter") {
            input("command", { name: "active", value: a === "enter", world: w }); r.active = a === "enter"
        } else if ((m = /^(lead|b|c)(Down|Up)$/.exec(a))) {
            var k = m[1] === "lead" ? "a" : m[1], up = m[2] === "Up"
            input("link", { sensor: k, up: up })
            all[k].imuConnected = up
        } else if (a === "guideReady" || a === "guideNotReady") {
            input("guide", { ready: a === "guideReady" }); guide.ready = a === "guideReady"
        } else if (a === "legacy") {
            var dn = qAxis(1, 0, 0, 85), tp = qMul(qAxis(1, 0, 0, 85), qAxis(0, 0, 1, 80))
            input("legacy", { sensor: "a", calibrated: true, armDown: dn, tPose: tp })
            all.a.calibArmDown = Qt.quaternion(dn[0], dn[1], dn[2], dn[3])
            all.a.calibArmTPose = Qt.quaternion(tp[0], tp[1], tp[2], tp[3])
            all.a.calibrated = true
        } else if ((m = /^segments:([abc]+)$/.exec(a))) {
            var segs = {}, held = {}
            SLOTS.forEach(function(k) { held[k] = m[1].indexOf(k) >= 0; if (held[k]) segs[k] = all[k] })
            input("segments", held)
            r.segments = segs
        } else if ((m = /^idle:(\d+)$/.exec(a))) {
            for (var i = 0; i < TICK_TIMERS.length; ++i) if (r[TICK_TIMERS[i]].running) rig.orderFault = "idle while " + TICK_TIMERS[i] + " runs"
            for (var n in ONE_SHOTS) if (r[n].running) rig.orderFault = "idle while " + n + " runs"
            if (guide.inFlight) rig.orderFault = "idle while the guide animates"
            if (rig.orderFault !== "") return
            rig.idle = { ms: Number(m[1]), until: Date.now() + Number(m[1]) * scale + 50 }
            return
        } else if (a === "whenSettled") {
            rig.deferred = rig.queue.splice(0, rig.queue.length)
            return
        } else {
            rig.orderFault = "unknown action " + a
            return
        }
        after(false)
    }
    // The test's wait loop calls this: an idle ends when its wall-clock time has passed with nothing fired.
    rig.poll = function() {
        if (!rig.idle || Date.now() < rig.idle.until) return
        var ms = rig.idle.ms
        rig.idle = null
        rig.clock += ms
        input("idle", { dt: ms })
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
            var stale = (name === "_introStartTimer" && !r._introStarted)
                     || (name === "_introReadyTimer" && r._introReadyArmed)
                     || (name === "_phase1MinHoldTimer" && r._phase1MinHoldArmed)
                     || (name === "_captureTransitionTimer" && r._captureTransitionArmed)
                     || (name === "_raiseReadyTimer" && r._raiseReadyArmed)
            if (stale && rig.orderFault === "") rig.orderFault = name + ": the test's slot ran before the routine's handler"
            var dt = r._p[ONE_SHOTS[name]]
            rig.clock += dt
            input("timer", { timer: timerName(name), dt: dt })
            after(false)
        })
    }
    for (var i = 0; i < TICK_TIMERS.length; ++i) hookTick(TICK_TIMERS[i])
    for (var n in ONE_SHOTS) hookOneShot(n)
    guide.finished.connect(function() {
        // The routine has moved on from the animation that finished (its handler ran first).
        if (r._animStage !== "" && r._animStage === rig.animWas && rig.orderFault === "")
            rig.orderFault = "guide.finished: the test's slot ran before the routine's"
        var dt = rig.animMs
        rig.clock += dt
        input("guideFinished", { dt: dt })
        after(false)
    })
    // Through the sink, so it sits in order with the lines and calls of the input that caused it.
    r.completed.connect(function() { rig.sink.push({ name: "signal.completed", args: [] }) })

    rig.start = function(first) {
        stateDiff()
        rig.lastKey = subKey()
        rig.pending = snapshot()
        rig.queue.push(first || "begin")
        after(false)
    }
    rig.inputs = function() {
        return rig.events.filter(function(e) { return e.kind === "input" }).map(function(e) { return e.value })
    }
    rig.initial = function() { return { sensors: snapshot(), world: world() } }
    return rig
}

// ── The projection ────────────────────────────────────────────────────────────────────────────────
// Each entry: { t, own, … }. `own` says whose duty it is (DUTIES-WitmotionArmRoutine.qml.md): "P" the
// procedure (stages, verdicts, reasons, progress, the averaged quaternions), "sensor" the sensor calls (set
// nominal and refine are P's requests; the clears F5), "guide" the guide (F2, stays in QML), "entry" the
// entry points' lines (base), "end" where the run came to rest. C-12a compares `own: "P"` and the sensor
// requests.
var SEGMENT_WORDS = { "forearm": "forearm", "hand": "hand", "upper arm": "upperArm" }
function segCodes(list, sep) {
    if (list === "") return []
    return list.split(sep).map(function(w) { return SEGMENT_WORDS[w] !== undefined ? SEGMENT_WORDS[w] : "text:" + w })
}
function messageCode(text) {
    if (text === "" || text === undefined || text === null) return null
    var m = /^Sensor mounted incorrectly \((.*)\) — re-seat per the strap guide and tap Recalibrate\.$/.exec(text)
    if (m) return { code: "mountFailed", sensors: segCodes(m[1], ", ") }
    return "text:" + text
}
function parseLine(line) {
    var m, body = line.replace(/^\[Calib\] witmotion /, "")
    if (body === line) return { own: "log", text: line }
    if ((m = /^stage=(\S+)$/.exec(body))) return { own: "P", stage: m[1] }
    if ((m = /^phase2 captured mount=pass$/.exec(body))) return { own: "P", mount: "pass" }
    if ((m = /^phase2 captured mount=fail ?(.*)$/.exec(body))) return { own: "P", mount: "fail", sensors: segCodes(m[1], ",") }
    if (body === "lead disconnected") return { own: "P", why: "linkLost", sensor: "a" }
    if (body === "begin" || body === "reset") return { own: "entry", entry: body }
    if ((m = /^stop reason=(\S+) stage=(\S+)(.*)$/.exec(body))) {
        var o = { own: "entry", entry: "stop", reason: m[1], stage: m[2] }
        if (m[3]) o.note = m[3].trim()
        return o
    }
    if ((m = /^restore done=(\S+)(.*)$/.exec(body))) {
        var o2 = { own: "entry", entry: "restore", done: m[1] === "true" }
        if (m[2]) o2.note = m[2].trim()
        return o2
    }
    return { own: "log", text: line }
}
var SENSOR_CALLS = { clearCalibration: "clear", clearFunctionalCalibration: "clearFunctional",
                     setNominalCalibration: "setNominal", refineMountAboutLongAxis: "refine" }
function project(events, tickMs) {
    var out = [], lineStage = null
    var round = function(t) { return tickMs > 0 ? Math.round(t / tickMs) * tickMs : t }
    for (var i = 0; i < events.length; ++i) {
        var e = events[i], t = round(e.t), o = null
        if (e.kind === "input") continue
        if (e.kind === "log") {
            o = parseLine(e.value)
            if (o.own === "P" && o.stage !== undefined) lineStage = o.stage
        } else if (e.kind === "call") {
            var dot = e.name.indexOf("."), who = e.name.slice(0, dot), call = e.name.slice(dot + 1)
            if (who === "guide") { o = { own: "guide", guide: call }; if (e.value.length > 0) o.args = e.value }
            else { o = { own: "sensor", sensor: who, call: SENSOR_CALLS[call] || call }; if (e.value.length > 0) o.args = e.value }
        } else if (e.kind === "signal") {
            o = { own: "P", signal: e.name }
        } else if (e.kind === "end") {
            o = { own: "end", at: e.value }
        } else if (e.kind === "state") {
            var n = e.name, v = e.value
            if (n === "stage") {
                // Every stage change is the routine's stage line; one it did not log would show here.
                if (lineStage === v) continue
                o = { own: "P", state: "stage", value: v }
                lineStage = v
            }
            else if (n === "message") o = { own: "P", message: messageCode(v) }
            else { o = { own: "P" }; o[n] = v }
        }
        if (o === null) continue
        var entry = { t: t }
        for (var k in o) entry[k] = o[k]
        out.push(entry)
    }
    return out
}
