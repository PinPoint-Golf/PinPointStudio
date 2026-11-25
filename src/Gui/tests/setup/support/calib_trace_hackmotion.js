// calib_trace_hackmotion.js — the golden trace of the HackMotion (wG3) arm routine (platform refactor C-9c;
// docs/design/rig_design.md §4.8, "The equivalence test"). The method is calib_trace.js's (C-9a) and
// calib_trace_witmotion.js's (C-9b), in its own file so their paths and fixtures stay as they were:
//
//   attachHackMotion(o)  runs HackMotionArmRoutine.qml against a FakeHmDevice in "manual" mode, with THIS FILE
//                   playing the library: each call the routine makes is answered here, in the slot after the
//                   routine's own handler, as autoScript would answer it (FakeHmDevice.qml, the same field
//                   order), unless the set scripts another answer. Nothing runs on the fakes' own timers, so every
//                   run feeds the routine the same values in the same order, whatever the wall clock did.
//                   Time is the routine's own: the sum of the nominal (unscaled) intervals it consumed (each
//                   one-shot's interval, the guide's animation, a scripted idle). A device event, a link change
//                   and a host command take no time. At most one clock runs at once (a one-shot, the guide's
//                   animation or an idle), and it is asserted (`orderFault`), so the sum is each event's time.
//                   It records the event log ({t, src, kind, name, value}, CalibDriver's shape) and from it the
//                   INPUT SET: every value the routine read, when it read it — each device event as the ordered
//                   field writes the routine reacted to (and the device's readable fields after them), each
//                   call's answer (a refusal, a phase, the presence result), the lower-arm unit's quaternion
//                   (the travel gate's two reads), the guide's ready and finish, the host's commands — as data a
//                   C++ test can feed to HackMotionArmProcedure later (C-13a).
//   project(events, tickMs)  the event log → the ordered list of stages, verdicts and reason codes, the device
//                   requests made, the guide's calls and the status column's values, each time rounded to the
//                   pace tick (100 ms). What the fixtures freeze.
//
// ⚠ It relies on the routine's handlers running BEFORE a slot connected later (Timer.onTriggered, the base's
// Connections on guide.finished): Qt calls slots in connection order. attachHackMotion asserts it on every
// one-shot and every guide finish (`orderFault`).
//
// ⚠ Numbers. The routine turns the unit's quaternion into a QML `quaternion` (single precision) before the
// travel gate's 2·acos, so a travel comes out within about 1e-4° of the scripted angle. The sets keep every
// travel at least 0.1° from the 15° gate and from a .5 the message rounds; the projection rounds the travel
// to 0.01°, and a port computing in doubles compares it at ±0.01°. A NaN reading is null in the files.

// ── Numbers and quaternions ──────────────────────────────────────────────────────────────────────────
function num(v) { return (typeof v === "number" && isFinite(v)) ? v : null }
function r2(v) { var n = num(v); return n === null ? null : Math.round(n * 100) / 100 }
function qArr(q) { return (q === null || q === undefined) ? null : [q.scalar, q.x, q.y, q.z] }
function q4(q) { var a = qArr(q); return a === null ? null : a.map(function(v) { return Math.round(v * 1e4) / 1e4 }) }
// The lower-arm unit turned `deg` about x, as [w, x, y, z] (FakeHmDevice._applyTravel's convention: the unit
// sits 10° off identity, which the routine would read as "no sample yet", and the raise adds to that). Rounded
// to 1e-9, so the files hold the same digits on every platform's sin and cos.
function qX(deg) {
    var h = deg * Math.PI / 360
    return [Math.cos(h), Math.sin(h), 0, 0].map(function(v) { return Math.round(v * 1e9) / 1e9 })
}
function armAt(travel) { return qX(10 + travel) }

// ── The library, as FakeHmDevice models it ───────────────────────────────────────────────────────────
var P = { idle: 0, awaitHorizontal: 1, markingPose0: 2, observingRaise: 3, markingPose1: 4, applying: 5,
          verifying: 6, complete: 7, aborted: 8 }
// The name a refusal carries (HmSessionWorker's, src/IMU/hm_session_worker.cpp).
var WIRE = { beginCalibration: "wr_calibration_begin", confirmHorizontal: "wr_calibration_confirm_horizontal",
             confirmRaise: "wr_calibration_confirm_raise", confirmReferencePose: "wr_calibration_confirm_reference_pose",
             abortCalibration: "wr_calibration_abort" }
// The device fields the routine reads, by the short name the files use.
var FIELD = { link: "imuConnected", streaming: "streaming", active: "calibrationActive", phase: "calibrationPhase",
              state: "calibrationState", abort: "calibrationAbortReason", samples: "presenceSamplesUsed",
              notMeasured: "presenceNotMeasured" }
// What confirmReferencePose's presence measurement writes (FakeHmDevice.confirmReferencePose, read fields only).
// "pending:<outcome>": the phase reaches COMPLETE first with no samples (H26), the measurement lands after it.
function presenceWrites(outcome) {
    if (outcome === "calibrated")   return [["samples", 64], ["notMeasured", false], ["state", 2]]
    if (outcome === "uncalibrated") return [["samples", 64], ["notMeasured", false], ["state", 1]]
    if (outcome === "notMeasured")  return [["samples", 3], ["notMeasured", true], ["state", 0]]
    return null
}

// ── The routine's timers (HackMotionArmRoutine.qml, by name), each one-shot's pace key, the armed flags ──
var ONE_SHOTS = { _hmStartTimer: "hmStart", _hmHorizontalSettleTimer: "settle", _hmRaiseConfirmTimer: "raiseSettle",
                  _hmRefSettleTimer: "refSettle", _hmPresenceWaitTimer: "presenceWait" }
var ARMED = [["horizontal", "_hmHorizontalSettleArmed"], ["raiseConfirm", "_hmRaiseConfirmArmed"],
             ["refSettle", "_hmRefSettleArmed"], ["presenceWait", "_hmPresenceWaitArmed"]]
function timerName(n) { return n.replace(/^_hm/, "").replace(/Timer$/, "").replace(/^./, function(c) { return c.toLowerCase() }) }

// ── The run ─────────────────────────────────────────────────────────────────────────────────────────
// o: { routine, device (FakeHmDevice, mode "manual"), guide (FakeGuide), log (FakeAppLog), spec }
// spec: {
//   answers:  { <call>: [answer, …] } — the nth call's answer: undefined / "ok" (the library's, as autoScript),
//             "ignore" (nothing comes back), { refuse: <status> } (calibrationCallRefused)
//   presence: [outcome, …] — each accepted confirmReferencePose: "calibrated" (default), "uncalibrated",
//             "notMeasured", "silent" (nothing comes back), "pending", "pending:<outcome>"
//   raises:   [raise, …] — each OBSERVING_RAISE: a number (the forearm's travel, default 30), or
//             { start: false (the library writes no start pose), to: <deg> | "identity" | "nan" | "none" }
//   on:       [{ at: "<stage>/<sub>", visit: n | run: n, do: [action, …] }] — each entry runs once, the first
//             time its key is entered on that visit (or during that run: the count of begin commands)
// }
// The keys: idle/, hm0/, hm1/begun (beginCalibration sent), hm1/settle (AWAIT_HORIZONTAL), hm1/marking
//   (MARKING_POSE0), hm2/anim (the raise animating), hm2/settle (raise confirm armed), hm2/marking (MARKING_POSE1),
//   hm2/wait, hm3/ (APPLYING), hm4/anim (the return animating), hm4/settle, hm4/presence (awaiting the
//   measurement), hm4/wait, hm5/ (done), hmStopped/.
// Actions — the host's: begin, reset, restore, stop (stop("back")), leave (active false), enter (active true),
//   guideReady / guideNotReady, guideNull / guideBack (the host takes the guide away, and gives it back),
//   segments:full | noUnit (no lower-arm unit) | none (no device); the world's: linkDown (the link drops: the
//   transport, then the device's invalidation, as FakeHmDevice.setConnected(false)), linkUp, transportDown
//   (imuConnected alone, H29), invalidate (the device's invalidation alone, a stream restart: HmInstance::
//   invalidateCalibration), streamOff / streamOn, abort:<code> (the library aborts by itself), complete (the
//   library reaches COMPLETE by itself), completeCaller (COMPLETE carrying CALLER, H23), phase:<n>, state:<n>,
//   stateEvent (calibrationStateChanged re-emitted),
//   arm:<deg> (the forearm moves to that travel), armIdentity (the unit reads identity); the test's:
//   idle:<ms> (time passes with nothing to do: no clock may be running), whenSettled (the rest of the list
//   waits until nothing is queued, no timer runs and the guide is still).
function attachHackMotion(o) {
    var r = o.routine, dev = o.device, guide = o.guide
    var spec = o.spec || {}
    var scale = r._p.scale
    var rig = {
        events: [], clock: 0, sink: [], queue: [], busy: false, finished: false, orderFault: "",
        lastState: {}, lastKey: "", idle: null, deferred: null, visits: {}, runs: 0, used: [],
        callN: {}, raiseN: 0, presenceN: 0, libPhase: dev.calibrationPhase, animMs: 0, animWas: "",
        inputCount: 0, maxInputs: o.maxInputs || 4000
    }

    function ev(kind, name, value) { rig.events.push({ t: rig.clock, src: "hackmotion", kind: kind, name: name, value: value }) }
    function unit() { return r.segments && r.segments.lowerArm ? r.segments.lowerArm : null }

    // What the routine can read now: the device's fields, the lower-arm unit's quaternion, the guide's ready.
    function device() {
        var s = {}
        for (var k in FIELD) s[k] = dev[FIELD[k]]
        var u = unit()
        s.lowerArm = u ? [num(u.quatW), num(u.quatX), num(u.quatY), num(u.quatZ)] : null
        s.given = { device: !!(r.segments && r.segments.device), lowerArm: u !== null }
        s.guideReady = r.guide ? r.guide.ready === true : null
        return s
    }
    function input(name, value) {
        var rec = { t: rig.clock, ev: name }
        for (var k in value) rec[k] = value[k]
        ev("input", name, rec)
        rig.inputCount += 1
    }

    // ── Outputs: one ordered timeline from the fakes' shared sink ──
    dev.sink = rig.sink
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
                    args = [q4(g.arm), q4(g.fore), Math.round(g.ms / scale)]
                    rig.animMs = Math.round(g.ms / scale); rig.animWas = r._animStage
                }
                ev("call", "guide." + g.name, args)
                continue
            }
            ev("call", "device." + call, [])
            answer(call)
        }
    }

    // The library's answer to one call: queued behind what is already queued (its I/O thread runs in order).
    function lib(cause, writes) { rig.queue.push({ lib: true, cause: cause, writes: writes }) }
    function answer(call) {
        var n = rig.callN[call] = (rig.callN[call] || 0) + 1
        var a = ((spec.answers || {})[call] || [])[n - 1]
        if (a === "ignore") return
        if (a && a.refuse !== undefined) { lib(call, [["emit", "refused", a.refuse, WIRE[call]]]); return }
        if (!dev.imuConnected) { lib(call, [["emit", "refused", -12, WIRE[call]]]); return }
        var ph = rig.libPhase
        if (call === "beginCalibration") {
            if (!dev.streaming) { lib(call, [["emit", "refused", -14, WIRE[call]]]); return }
            if ([P.idle, P.complete, P.aborted].indexOf(ph) < 0) return
            rig.libPhase = P.awaitHorizontal
            lib(call, [["active", true], ["abort", 0], ["samples", 0], ["notMeasured", false],
                       ["phase", P.awaitHorizontal], ["emit", "stateChanged"]])
        } else if (call === "confirmHorizontal") {
            if (ph !== P.awaitHorizontal) return
            rig.libPhase = P.observingRaise
            var rs = (spec.raises || [])[rig.raiseN]; rig.raiseN += 1
            if (rs === undefined || rs === null) rs = 30
            if (typeof rs === "number") rs = { to: rs }
            lib(call, [["phase", P.markingPose0], ["emit", "stateChanged"]])
            var w = rs.start === false ? [] : [["lowerArm", armAt(0)]]
            lib(call, w.concat([["phase", P.observingRaise], ["emit", "stateChanged"]]))
            // The golfer's raise, as the guide paces it: where the forearm is when the routine next reads it.
            var to = rs.to === undefined ? 30 : rs.to
            if (to === "identity") rig.queue.push({ lib: true, cause: "raise", writes: [["lowerArm", [1, 0, 0, 0]]] })
            else if (to === "nan") rig.queue.push({ lib: true, cause: "raise", writes: [["lowerArm", [NaN, NaN, NaN, NaN]]] })
            else if (to !== "none") rig.queue.push({ lib: true, cause: "raise", writes: [["lowerArm", armAt(to)]] })
        } else if (call === "confirmRaise") {
            if (ph !== P.observingRaise) return
            rig.libPhase = P.verifying
            lib(call, [["phase", P.markingPose1], ["emit", "stateChanged"]])
            lib(call, [["phase", P.applying], ["emit", "stateChanged"]])
            lib(call, [["phase", P.verifying], ["emit", "stateChanged"]])
        } else if (call === "confirmReferencePose") {
            if (ph !== P.verifying) return
            var out = (spec.presence || [])[rig.presenceN]; rig.presenceN += 1
            if (out === undefined || out === null) out = "calibrated"
            if (out === "silent") return
            rig.libPhase = P.complete
            var m = /^pending(?::(\w+))?$/.exec(out)
            if (m) {
                lib(call, [["active", false], ["phase", P.complete], ["emit", "stateChanged"]])
                if (m[1]) lib("presence", presenceWrites(m[1]).concat([["emit", "stateChanged"]]))
            } else {
                lib(call, [["active", false]].concat(presenceWrites(out), [["phase", P.complete], ["emit", "stateChanged"]]))
            }
        } else if (call === "abortCalibration") {
            // A local reset in the library: nothing to abort when idle or finished (autoScript ignores it there).
            if ([P.idle, P.complete, P.aborted].indexOf(ph) >= 0) return
            var atV = ph === P.verifying
            rig.libPhase = atV ? P.complete : P.aborted
            lib(call, [["active", false], ["abort", 1], ["phase", atV ? P.complete : P.aborted], ["emit", "stateChanged"]])
        }
    }
    // The library's routine is over: whatever it had still queued never comes.
    function dropQueuedLib() { rig.queue = rig.queue.filter(function(it) { return !it.lib }) }

    function applyWrites(writes) {
        for (var i = 0; i < writes.length; ++i) {
            var w = writes[i]
            if (w[0] === "emit") {
                if (w[1] === "stateChanged") dev.calibrationStateChanged()
                else if (w[1] === "invalidated") dev.calibrationInvalidated()
                else if (w[1] === "refused") dev.calibrationCallRefused(w[2], w[3])
            } else if (w[0] === "lowerArm") {
                var u = dev.unitLowerArm
                u.setQuat(w[1][0], w[1][1], w[1][2], w[1][3])
            } else {
                dev[FIELD[w[0]]] = w[1]
            }
        }
    }
    function recorded(writes) {
        return writes.map(function(w) {
            if (w[0] === "lowerArm") return ["lowerArm", w[1].map(num)]
            return w.map(function(v) { return typeof v === "number" ? num(v) : v })
        })
    }
    function deviceEvent(cause, writes) {
        input("device", { cause: cause, set: recorded(writes) })
        applyWrites(writes)
        after()
    }

    // ── The routine's state, as P holds it and the status column reads it ──
    function messageKind() { return r.messageKind }
    // ArmCalibrationStatus.qml: the HmStepBar's four bars (shown at steps 1–5), the travel readout's tint, and
    // holdProgress (Witmotion-only: for this routine it is 1 when done, else 0). From the routine's done, where the
    // view reads its host's (which for a HackMotion also needs the device connected and CALIBRATED).
    function bar() {
        var s = r.hmStep
        if (s < 1 || s > 5) return null
        var out = []
        for (var i = 0; i < 4; ++i)
            out.push(r.calibrationDone ? "done" : s > i + 1 ? "past" : s === i + 1 ? "current" : "todo")
        return out
    }
    function travelTint() {
        var t = r._hmRaiseTravelDeg
        return isNaN(t) ? "muted" : t >= r._hmMinRaiseTravelDeg ? "good" : "warn"
    }
    function armed() {
        var out = []
        for (var i = 0; i < ARMED.length; ++i) if (r[ARMED[i][1]]) out.push(ARMED[i][0])
        return out
    }
    function stateDiff() {
        var s = { stage: r.stage, step: r.hmStep, phase: r.hmPhase, anim: r._animStage, armed: armed(),
                  awaiting: r.hmAwaitingPresence, travel: r2(r._hmRaiseTravelDeg), failed: r.calibrationFailed,
                  message: r.message, messageKind: messageKind(), invalidated: r.hmInvalidated,
                  done: r.calibrationDone, hold: r.calibrationDone ? 1 : 0, bar: bar(), travelTint: travelTint() }
        for (var k in s) {
            var v = JSON.stringify(s[k])
            if (rig.lastState[k] !== v) { ev("state", k, s[k]); rig.lastState[k] = v }
        }
    }

    // ── The scripted world ──
    function subKey() {
        var st = r.stage, sub = ""
        if (st === "hm1") sub = r.hmPhase === P.awaitHorizontal ? "settle" : r.hmPhase === P.markingPose0 ? "marking" : "begun"
        else if (st === "hm2") sub = r._animStage === "hmRaise" ? "anim" : r._hmRaiseConfirmArmed ? "settle"
                                   : r.hmPhase === P.markingPose1 ? "marking" : "wait"
        else if (st === "hm4") sub = r._animStage === "hmReturn" ? "anim" : r._hmRefSettleArmed ? "settle"
                                   : r.hmAwaitingPresence ? "presence" : "wait"
        return st + "/" + sub
    }
    function onEntered(key) {
        rig.visits[key] = (rig.visits[key] || 0) + 1
        var v = rig.visits[key], on = spec.on || []
        for (var i = 0; i < on.length; ++i) {
            var e = on[i]
            if (rig.used[i] || e.at !== key) continue
            if (e.visit !== undefined && e.visit !== v) continue
            if (e.run !== undefined && e.run !== rig.runs) continue
            rig.used[i] = true
            schedule(e.do)
            return
        }
    }
    // A list's actions run before anything already queued (the golfer acts between the library's steps); after a
    // whenSettled, the rest wait until all is still.
    function schedule(list) {
        list = Array.isArray(list) ? list : [list]
        var k = list.indexOf("whenSettled")
        var now = k < 0 ? list : list.slice(0, k), later = k < 0 ? [] : list.slice(k + 1)
        rig.queue = now.map(function(a) { return { act: a } }).concat(rig.queue)
        if (later.length > 0) rig.deferred = (rig.deferred || []).concat(later)
    }

    function runningTimers() {
        var out = []
        for (var n in ONE_SHOTS) if (r[n].running) out.push(n)
        return out
    }
    function settled() {
        return rig.queue.length === 0 && !rig.idle && runningTimers().length === 0 && !guide.inFlight
    }
    // After the routine has processed one input: its outputs, the state, the key's actions, then what is queued.
    function after() {
        drain()
        stateDiff()
        var key = subKey()
        if (key !== rig.lastKey) { rig.lastKey = key; onEntered(key) }
        if (rig.busy) return
        rig.busy = true
        for (;;) {
            while (rig.queue.length > 0 && !rig.idle && rig.orderFault === "") perform(rig.queue.shift())
            if (rig.orderFault !== "" || rig.idle || rig.deferred === null || !settled()) break
            var d = rig.deferred; rig.deferred = null
            schedule(d)
        }
        rig.busy = false
        // One clock at a time, or the order of two wall-clock events would decide the trace.
        var clocks = runningTimers()
        if (guide.inFlight) clocks.push("the guide's animation")
        if (rig.idle) clocks.push("an idle")
        if (clocks.length > 1 && rig.orderFault === "") rig.orderFault = "two clocks at once: " + clocks.join(", ")
        checkFinished()
    }
    function checkFinished() {
        if (rig.deferred === null && settled()) { ev("end", "at", rig.lastKey); rig.finished = true }
        if (rig.orderFault !== "") rig.finished = true
        if (rig.inputCount > rig.maxInputs) { rig.orderFault = "more than " + rig.maxInputs + " inputs"; rig.finished = true }
    }

    function command(name, extra, fn) {
        var rec = { name: name, device: device() }
        for (var k in extra) rec[k] = extra[k]
        input("command", rec)
        fn()
        after()
    }
    function perform(item) {
        if (item.lib) { deviceEvent(item.cause, item.writes); return }
        var a = item.act, m
        if (a === "begin" || a === "reset" || a === "restore") {
            if (a === "begin") rig.runs += 1
            command(a, {}, function() { r[a]() })
        } else if (a === "stop") {
            command("stop", { reason: "back" }, function() { r.stop("back") })
        } else if (a === "leave" || a === "enter") {
            command("active", { value: a === "enter" }, function() { r.active = a === "enter" })
        } else if (a === "guideReady" || a === "guideNotReady") {
            input("guide", { ready: a === "guideReady" }); guide.ready = a === "guideReady"; after()
        } else if (a === "guideNull" || a === "guideBack") {
            command("guide", { given: a === "guideBack" }, function() { r.guide = a === "guideBack" ? guide : null })
        } else if ((m = /^segments:(full|noUnit|none)$/.exec(a))) {
            var segs = m[1] === "full" ? { device: dev, lowerArm: dev.unitLowerArm, palm: dev.unitPalm }
                     : m[1] === "noUnit" ? { device: dev, palm: dev.unitPalm } : { lowerArm: dev.unitLowerArm, palm: dev.unitPalm }
            command("segments", { given: m[1] }, function() { r.segments = segs })
        } else if (a === "linkDown") {
            // FakeHmDevice.setConnected(false): the transport, then the device; the library's queue dies with the link.
            var wasCal = dev.calibrationState === 2 || dev.calibrationActive
            dropQueuedLib(); rig.libPhase = P.idle
            var w = [["link", false], ["streaming", false], ["active", false], ["phase", P.idle], ["state", 1],
                     ["emit", "stateChanged"]]
            if (wasCal) w.push(["emit", "invalidated"])
            deviceEvent("linkDown", w)
        } else if (a === "linkUp") {
            deviceEvent("linkUp", [["link", true], ["streaming", true]])
        } else if (a === "transportDown") {
            deviceEvent("transportDown", [["link", false], ["streaming", false]])
        } else if (a === "invalidate") {
            // HmInstance::invalidateCalibration on a stream restart (library state UNKNOWN), the link still up.
            dropQueuedLib(); rig.libPhase = P.idle
            deviceEvent("invalidate", [["active", false], ["phase", P.idle], ["state", 0], ["abort", 0], ["samples", 0],
                                       ["notMeasured", false], ["emit", "stateChanged"], ["emit", "invalidated"]])
        } else if (a === "streamOff" || a === "streamOn") {
            deviceEvent(a, [["streaming", a === "streamOn"]])
        } else if ((m = /^abort:(\d+)$/.exec(a))) {
            dropQueuedLib(); rig.libPhase = P.aborted
            deviceEvent("abort", [["active", false], ["abort", Number(m[1])], ["phase", P.aborted], ["emit", "stateChanged"]])
        } else if (a === "completeCaller" || a === "complete") {
            dropQueuedLib(); rig.libPhase = P.complete
            var cw = [["active", false]]
            if (a === "completeCaller") cw.push(["abort", 1])
            deviceEvent(a, cw.concat([["phase", P.complete], ["emit", "stateChanged"]]))
        } else if ((m = /^phase:(\d+)$/.exec(a))) {
            var p = Number(m[1])
            if (p === P.complete || p === P.aborted) dropQueuedLib()
            rig.libPhase = p
            deviceEvent("phase", [["phase", p], ["emit", "stateChanged"]])
        } else if ((m = /^state:(\d+)$/.exec(a))) {
            deviceEvent("state", [["state", Number(m[1])]])
        } else if (a === "stateEvent") {
            deviceEvent("stateEvent", [["emit", "stateChanged"]])
        } else if ((m = /^arm:(-?[\d.]+)$/.exec(a))) {
            deviceEvent("arm", [["lowerArm", armAt(Number(m[1]))]])
        } else if (a === "armIdentity") {
            deviceEvent("arm", [["lowerArm", [1, 0, 0, 0]]])
        } else if ((m = /^idle:(\d+)$/.exec(a))) {
            var busy = runningTimers()
            if (guide.inFlight) busy.push("the guide's animation")
            if (busy.length > 0) { rig.orderFault = "idle while " + busy.join(", ") + " runs"; return }
            rig.idle = { ms: Number(m[1]), until: Date.now() + Number(m[1]) * scale + 50 }
        } else {
            rig.orderFault = "unknown action " + a
        }
    }
    // The test's wait loop calls this: an idle ends when its wall-clock time has passed with nothing fired.
    rig.poll = function() {
        if (!rig.idle || Date.now() < rig.idle.until) return
        var ms = rig.idle.ms
        rig.idle = null
        rig.clock += ms
        input("idle", { dt: ms })
        after()
    }

    // ── Hooks: the routine's one-shots, the guide's finish ──
    function hookOneShot(name) {
        r[name].triggered.connect(function() {
            // The routine's handler has run: its one-shot's state is behind it.
            var stale = (name === "_hmStartTimer" && r.hmStep === 0)
                     || (name === "_hmHorizontalSettleTimer" && r._hmHorizontalSettleArmed)
                     || (name === "_hmRaiseConfirmTimer" && r._hmRaiseConfirmArmed)
                     || (name === "_hmRefSettleTimer" && r._hmRefSettleArmed)
                     || (name === "_hmPresenceWaitTimer" && r._hmPresenceWaitArmed)
            if (stale && rig.orderFault === "") rig.orderFault = name + ": the test's slot ran before the routine's handler"
            var dt = r._p[ONE_SHOTS[name]]
            rig.clock += dt
            input("timer", { timer: timerName(name), dt: dt, device: device() })
            after()
        })
    }
    for (var n in ONE_SHOTS) hookOneShot(n)
    guide.finished.connect(function() {
        // The routine has moved on from the animation that finished (its handler ran first) — when it listens.
        var listening = r.active && r._running && r.guide === guide
        if (listening && r._animStage !== "" && r._animStage === rig.animWas && rig.orderFault === "")
            rig.orderFault = "guide.finished: the test's slot ran before the routine's"
        var dt = rig.animMs
        rig.clock += dt
        input("guideFinished", { dt: dt, listening: listening, device: device() })
        after()
    })
    // Through the sink, so it sits in order with the lines and calls of the input that caused it.
    r.completed.connect(function() { rig.sink.push({ name: "signal.completed", args: [] }) })

    rig.start = function(first) {
        stateDiff()
        rig.lastKey = subKey()
        schedule(first || ["begin"])
        after()
    }
    rig.inputs = function() {
        return rig.events.filter(function(e) { return e.kind === "input" }).map(function(e) { return e.value })
    }
    rig.initial = function() { return { device: device() } }
    return rig
}

// The device's state before the run (a set's `device`), and the segments the host hands the routine.
function prepareDevice(dev, init) {
    init = init || {}
    for (var k in init) {
        if (k === "lowerArm") dev.unitLowerArm.setQuat(init[k][0], init[k][1], init[k][2], init[k][3])
        else dev[FIELD[k]] = init[k]
    }
}
function segmentsFor(dev, which) {
    if (which === "noUnit") return { device: dev, palm: dev.unitPalm }
    if (which === "none") return { lowerArm: dev.unitLowerArm, palm: dev.unitPalm }
    return { device: dev, lowerArm: dev.unitLowerArm, palm: dev.unitPalm }
}

// ── The projection ────────────────────────────────────────────────────────────────────────────────
// Each entry: { t, own, … }. `own` says whose duty it is (DUTIES-HackMotionArmRoutine.qml.md): "P" the procedure
// (stages, the markers' log lines, stops, verdicts, reason codes, the status column's values), "device" the five
// device calls (P's requests, D performs them), "guide" the guide (F2, stays in QML), "entry" the entry points'
// lines (the base), "end" where the run came to rest. C-13a compares `own: "P"` and the device requests.
var MESSAGES = {
    "The wrist sensor is not connected. Connect it on the Sensors step, then tap Recalibrate.": "notConnected",
    "The wrist sensor is connected but not streaming, and the sensor has to WATCH the raise — it cannot calibrate from two still poses. Wait for the stream, then tap Recalibrate.": "notStreaming",
    "Presence check declined. The sensor applied its transform already and nothing reverses that — the calibration was NOT undone, it is simply unverified. Re-run to check it.": "declinedByStop",
    "Calibration cancelled before the sensor applied anything.": "cancelledByStop",
    "The sensor finished before the reference pose could be taken, so nothing checked the calibration. Tap Recalibrate.": "finishedBeforeReference",
    "Presence check declined. The sensor's transform is applied and nothing reverses that — it is unverified, not undone. Re-run to check it.": "presenceDeclined",
    "Too few readings arrived at the reference pose to check the calibration, so it is NOT confirmed — the sensor applied its transform, but nothing verified it. Hold the first position still and tap Recalibrate.": "presenceNotMeasured",
    "The check at the reference pose did not confirm a calibration on the sensor. Re-seat nothing — just hold the first position still and tap Recalibrate.": "checkNotConfirmed",
    "The sensor never reported a check at the reference pose, so the calibration is NOT confirmed. Hold the first position still and tap Recalibrate.": "noCheckReported",
    "The sensor's calibration is gone — a dropped link destroys it (measured 0.70° → 18.80° at the same pose with the strap untouched). It cannot be resumed or restored. Re-run the routine once the sensor is back.": "calibrationGone",
    "That took too long between the two positions — the sensor needs one continuous raise. Tap Recalibrate and follow the guide.": "abort:raiseTooSlow",
    "The sensor's data stream stopped part-way through, so the raise could not be watched. Tap Recalibrate once data is flowing.": "abort:streamLost",
    "The sensor's link dropped part-way through. Reconnect it, then run the routine again — a dropped link destroys any calibration.": "abort:linkLost",
    "The sensor did not answer in time, so no calibration was applied. Tap Recalibrate.": "abort:noResult",
    "Calibration cancelled.": "abort:caller",
    "The calibration routine stopped before it finished. Tap Recalibrate.": "abort:none"
}
function messageCode(text) {
    if (text === "" || text === undefined || text === null) return null
    if (MESSAGES[text] !== undefined) return MESSAGES[text]
    var m
    if ((m = /^Your forearm only moved (-?\d+)° — the sensor needs about (\d+)° to work out which way your wrist bends, and it cannot tell on its own that the movement was missing\. Nothing was applied\. Follow the guide and tap Recalibrate\.$/.exec(text)))
        return { code: "shortRaise", moved: Number(m[1]), needs: Number(m[2]) }
    if ((m = /^The sensor stopped streaming, so it cannot watch the raise \((\S+) was refused\)\. Tap Recalibrate once data is flowing\.$/.exec(text)))
        return { code: "refused", status: "noStream", call: m[1] }
    if ((m = /^The sensor is busy retrieving swing data \((\S+) was refused\) — try again in a moment\.$/.exec(text)))
        return { code: "refused", status: "busy", call: m[1] }
    if ((m = /^The sensor's link went down \((\S+) was refused\)\.$/.exec(text)))
        return { code: "refused", status: "linkDown", call: m[1] }
    if ((m = /^The sensor was not in a state to accept (\S+)\. Tap Recalibrate to start the routine from the beginning\.$/.exec(text)))
        return { code: "refused", status: "invalidState", call: m[1] }
    if ((m = /^The sensor refused (\S+) \(status (-?\d+)\)\.$/.exec(text)))
        return { code: "refused", status: Number(m[2]), call: m[1] }
    return "text:" + text
}
function parseLine(line) {
    var m, body = line.replace(/^\[Calib\] hackmotion /, "")
    if (body === line) return { own: "log", text: line }
    if ((m = /^stage=(\S+)$/.exec(body))) return { own: "P", stage: m[1] }
    if (body === "beginCalibration" || body === "confirmHorizontal" || body === "confirmReferencePose")
        return { own: "P", marker: body }
    if ((m = /^confirmRaise travel=(\S+)$/.exec(body)))
        return { own: "P", marker: "confirmRaise", travel: m[1] === "unmeasured" ? null : Number(m[1]) }
    if ((m = /^abortCalibration travel=(\S+)$/.exec(body)))
        return { own: "P", marker: "abortCalibration", why: "shortRaise", travel: Number(m[1]) }
    if (body === "abortCalibration") return { own: "P", marker: "abortCalibration" }
    if (body === "abortCalibration at=verifying") return { own: "P", marker: "abortCalibration", at: "verifying" }
    if (body === "aborted reason=caller after our own stop — message kept") return { own: "P", echo: "callerAfterOwnStop" }
    if ((m = /^stopped kind=(\S+)$/.exec(body))) return { own: "P", stopped: m[1] }
    if ((m = /^refused call=(\S+) status=(-?\d+)$/.exec(body))) return { own: "P", refused: m[1], status: Number(m[2]) }
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
            else o = { own: "device", call: call }
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
