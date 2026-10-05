// Stage 0 (part B2) characterisation tests — calibration, Witmotion routine: catalogue group
// CW (docs/design/session_wizard_refactor_design.md §7.3), written at Stage 0 against the
// then-current wizard; since Stage 5c against session setup + ImuCalibrationFlow over the fakes.
//
// Conventions (§7.3, and the Stage 0 brief):
//   - each function is named with its catalogue ID; "at each stage" cases are _data() rows;
//   - each case asserts the catalogue's EXPECTED column (the after-refactor behaviour);
//   - a case predicted to fail today because of finding Fn passes everything up to the
//     defect, then puts expectFail() immediately before the ONE assertion that encodes it.
//     XFAIL today; XPASS (a failure) once fixed, which forces the marker's removal;
//   - the "after leave, nothing happens" windows are computed per row from the remaining
//     one-shot chain in design §2.4 (`pace` below), plus `margin` — never one giant constant;
//   - everything that knows today's internals is in support/CalibDriver.qml.
//
// Stage 2 gave the routines a `pace`: CW1 and CW2 (which assert the real durations and holds)
// run at the real pace with the real 3-D guide, ~24 s per full run; every other case runs the
// same wizard and guide with the routines' clock scaled by `scaled` (×0.05, the hold timers'
// sampling tick kept at 20 ms — CalibDriver.scaledPaceExtra; each stillness hold lengthened to
// 800 ms — `longHold`, Stage 5d), so a full run takes ~3 s.
// Routine-level cases (CWr*) drive a WitmotionArmRoutine alone over a FakeGuide.
//
// The wizard-level cases run against session setup (ScreenSessionSetup) since Stage 5b; the
// retired wizard and its expected-failure markers were deleted at Stage 5c.
import QtQuick
import QtTest
import PinPointStudio
import "support"

// ⚠ The root is a plain Item, not the TestCase: TestCase is `visible: false`, and the
// calibration routines gate on `flow.visible` (see tst_setup_smoke.qml).
Item {
    id: root
    width: 1400
    height: 900

    CalibDriver { id: drv; anchors.fill: parent; tc: tcase }

    TestCase {
        id: tcase
        name: "SetupCalibWitmotion"
        when: windowShown

        // ── Pacing of today's chain (ImuCalibrationFlow.qml, design §2.4) ─────
        readonly property var pace: ({
            introStart: 3000,        // l.975
            introAnim: 3000,         // l.984, l.1003 (up, then down)
            introReady: 2000,        // l.1031
            minHold: 2000,           // l.1046
            hold: 2000,              // d._captureHoldMs, both stillness holds
            tick: 100,               // the hold timers' interval
            captureTransition: 800,  // l.1109
            raiseAnim: 1500,         // l.1115
            raiseReady: 2000         // l.1123
        })
        readonly property int margin: 1000
        readonly property real frameMs: 1000 / 60

        // The scaled clock (every case but CW1/CW2): the same table × `scaled`, and the
        // driver's sampling tick.
        readonly property real scaled: 0.05
        // The stillness hold on the scaled clock, before scaling: 16 000 ms → 800 ms (Stage 5d).
        // Every scaled case runs with it (CalibDriver.holdPace) so that "a hold is accumulating"
        // is a window of ~600 ms a poll cannot step over; everything else stays at × 0.05.
        readonly property int longHold: 16000
        readonly property var sp: ({
            introStart: pace.introStart * scaled, introAnim: pace.introAnim * scaled,
            introReady: pace.introReady * scaled, minHold: pace.minHold * scaled,
            hold: longHold * scaled, tick: 20,
            captureTransition: pace.captureTransition * scaled, raiseAnim: pace.raiseAnim * scaled,
            raiseReady: pace.raiseReady * scaled
        })
        // A hold samples every `tick` but the ticks land on animation frames, so a scaled
        // hold takes up to ~1.7× its nominal length; the windows allow for it.
        readonly property int smargin: 400
        function open(setup, opts) {
            opts = opts || {}
            opts.scale = scaled
            return drv.openAtCalibrate(setup, opts)
        }

        function tol(nominal) { return 0.15 * nominal + frameMs }
        function near(actual, nominal, what) {
            verify(Math.abs(actual - nominal) <= tol(nominal),
                   what + ": " + Math.round(actual) + " ms, expected " + nominal + " ±"
                   + Math.round(tol(nominal)) + " ms")
        }
        function quatNear(a, b) {
            var dot = Math.abs(a.scalar*b.scalar + a.x*b.x + a.y*b.y + a.z*b.z)
            return dot > 0.9999
        }

        function init() {
            testLog.reset()
            // Offscreen QPA's "Sans Serif" font-alias notice — see tst_setup_smoke.qml.
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            appSettings.imuRoles = ({})
            drv.holdPace = longHold          // teardown() resets it; CW1/CW2 run at scale 1, untouched
        }
        function cleanup() {
            drv.teardown()
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        // ── Reaching a stage ─────────────────────────────────────────────────
        // Leaves the routine at `stage`, mid-flight:
        //   intro   — the intro guide (up) is animating; one-shots ahead of it
        //   phase1  — the arm-down stillness hold is accumulating (≥ a quarter of it)
        //   raise   — the raise guide is animating
        //   phase2  — the T-pose stillness hold is accumulating (≥ a quarter of it)
        // On a failure to reach, the whole run so far is printed as a timeline (the routine's
        // app-log lines and the driver's state / timer events), so a flaky miss can be read.
        function dumpTimeline(why) {
            var t0 = drv.openedAt
            var log = drv.fx ? drv.fx.appLog.entries : []
            // The app log holds the routine's and the setup flow's lines AND the driver's events
            // (CalibDriver._ev, tag "Drv") in the order they happened.
            console.info("[timeline " + why + "] " + log.length + " entries")
            for (var k = 0; k < log.length; ++k)
                console.info("[timeline] #" + k + " +" + Math.round(drv.toElapsed(log[k].t) - t0) + " " + log[k].line)
        }
        function reach(stage) {
            try { _reach(stage) } catch (e) { dumpTimeline("reach " + stage); throw e }
        }
        // Polls by hand rather than with tryVerify: tryVerify's message is built when it is
        // CALLED, so it reported the stage at the start of the wait ("intro"), not at the timeout
        // (Stage 5d — the first flaky CW6 report read as a routine back at intro; it was done).
        function _waitFor(cond, ms, what) {
            var t0 = drv.now()
            while (!cond() && drv.now() - t0 < ms) wait(20)
            verify(cond(), what + " (stage now " + drv.stage() + ", animStage '" + drv.animStage()
                           + "', after " + Math.round(drv.now() - t0) + " ms)")
        }
        function _reach(stage) {
            if (stage === "intro")
                _waitFor(function() { return drv.animStage() === "introUp" }, 10000, "intro never started")
            else if (stage === "phase1")
                _waitFor(function() { return drv.stage() === "phase1" && drv.minHoldDone()
                                             && drv.phase1AccumMs() >= sp.hold / 4 }, 20000,
                         "phase-1 hold never accumulated")
            else if (stage === "raise")
                _waitFor(function() { return drv.animStage() === "raise" }, 25000,
                         "raise animation never started")
            else if (stage === "phase2")
                _waitFor(function() { return drv.stage() === "phase2" && drv.stableAccumMs() >= sp.hold / 4 }, 30000,
                         "phase-2 hold never accumulated")
            else
                fail("unknown stage " + stage)
            compare(drv.stage(), stage === "intro" ? "intro" : stage)
        }
        // How long after leaving at `stage` every one-shot still in the chain (§2.4) would
        // have fired, had the step not been left: the rest of the chain up to the next
        // stillness hold, plus that hold (in case it is not paused), plus the margin.
        function leaveWindow(stage) {
            if (stage === "intro")  return sp.introAnim + sp.introAnim + sp.introReady + sp.minHold + 2 * sp.hold + smargin
            if (stage === "phase1") return 2 * sp.hold + smargin
            if (stage === "raise")  return sp.raiseAnim + sp.raiseReady + 2 * sp.hold + smargin
            if (stage === "phase2") return 2 * sp.hold + smargin
            return 0
        }
        // intro and raise leave with one-shots in flight (the guide chain); phase1 and phase2
        // with a stillness hold accumulating. (Before Stage 2's pace the phase-2 row, and the
        // phase-1 row for Skip, were dropped for suite time; they are back.)
        function leaveRows() {
            return [ { tag: "intro", stage: "intro" }, { tag: "phase1", stage: "phase1" },
                     { tag: "raise", stage: "raise" }, { tag: "phase2", stage: "phase2" } ]
        }

        // The shared body of CW6 / CW9: leave Calibrate mid-run at `row.stage` by `how`, then
        // watch for the row's window. Expected: nothing fires, nothing is called, no
        // completed(), the guide stops, the stage is "idle".
        function leaveAndWatch(row, how, finding) {
            open("witmotion2")
            reach(row.stage)
            var m = drv.mark()
            var stageAtLeave = drv.stage()
            drv.leaveMark = null
            if (how === "back") {
                drv.leaveBack()
                compare(drv.wiz.currentStep, drv.wiz.stepImus, "Back did not go to IMUs")
            } else {
                drv.leaveSkip()
                compare(drv.wiz.currentStep, drv.wiz.stepConfirm, "Skip did not go to Check")
                compare(drv.wiz.stepStates[drv.wiz.stepCalibrate], "skipped")
            }
            tryVerify(function() { return !drv.flow.visible }, 2000, "flow still visible after leaving")
            // "After leave" starts when the routine was stopped (the flow went inactive), not
            // when the leave was requested: navigation is queued (R5), and a hold tick in that gap
            // is the routine still running ON the page, as it should (Stage 5d, CW6 phase2).
            var ml = drv.leaveMark
            verify(ml !== null, "the calibration flow never went inactive")
            var win = leaveWindow(row.stage)
            wait(win)

            var calls  = drv.callsSince(ml)
            var timers = drv.eventsSince(ml, "wiz", "timer")
            var states = drv.eventsSince(m, "wiz", "state")
            var guide  = drv.eventsSince(m, "wiz", "guide")
            var comp   = drv.completedSince(m)
            var stageNow = drv.stage()
            var anim   = drv.guideAnimating()
            var tings  = drv.tingsSince(m)
            var detail = how + " at " + stageAtLeave + ", watched " + win + " ms: calls "
                         + drv.fmtCalls(calls) + "; timers " + drv.fmtEvents(timers, m.t)
                         + "; states " + drv.fmtEvents(states, m.t) + "; guide "
                         + drv.fmtEvents(guide, m.t) + "; completed " + comp
                         + "; stage now " + stageNow + "; guide animating " + anim
                         + "; tings after leave " + tings
                         + "; running timers " + JSON.stringify(drv.runningTimers())
            console.info("[" + finding + "] " + detail)

            var clean = calls.length === 0 && timers.length === 0 && comp === 0 && tings === 0
                        && stageNow === "idle" && !anim
            if (!clean) dumpTimeline(finding + " " + row.tag)
            verify(clean, detail)
        }

        // ── 0. The harness clock ─────────────────────────────────────────────
        // ⚠ A QML Timer runs on the ANIMATION clock (QQmlTimer is a QPauseAnimationJob), and
        // under the offscreen QPA the default threaded render loop's animation driver steps
        // that clock by an assumed 16.67 ms "vsync" per rendered frame, which offscreen is
        // not paced to. Measured on 5 Oct: the 100 ms hold timers ticked every ~75 ms while
        // anything animated, and a 2000 ms one-shot started as the guide finished fired after
        // ~3900 ms. Every pacing assertion here (CW1, CW2, CH1) and every "nothing fires for
        // T ms" window would then measure the harness, not the wizard. QSG_RENDER_LOOP=basic
        // (or QSG_USE_SIMPLE_ANIMATION_DRIVER=1, which ctest now sets) brings both within a few ms of wall clock.
        // This case fails loudly when the clock is off, rather than letting the cases below
        // fail or pass for the wrong reason.
        Item {
            id: clockProbe
            width: 20; height: 20
            Rectangle { id: spinner; width: 10; height: 10; color: "grey" }
            Timer { id: tick100; interval: 100; repeat: true; property int n: 0; onTriggered: n++ }
            Timer { id: shot2000; interval: 2000; property real tFired: -1; onTriggered: tFired = drv.now() }
            NumberAnimation { id: spin; target: spinner; property: "x"; from: 0; to: 10
                              duration: 1500; running: false }
        }
        function test_0_harnessClock() {
            spinner.parent = root      // visible, so the animation drives frames
            tick100.n = 0
            spin.start(); tick100.start()
            var t0 = drv.now()
            tryVerify(function() { return !spin.running }, 5000)
            tick100.stop()
            var wall = drv.now() - t0
            var ticks = tick100.n
            shot2000.tFired = -1
            var t1 = drv.now()
            shot2000.start()
            tryVerify(function() { return shot2000.tFired > 0 }, 6000)
            var oneShot = shot2000.tFired - t1
            spinner.parent = clockProbe
            console.info("[clock] 100 ms repeat: " + ticks + " ticks in " + Math.round(wall)
                         + " ms of animation; 2000 ms one-shot after the animation: "
                         + Math.round(oneShot) + " ms")
            verify(Math.abs(ticks - wall / 100) <= 2,
                   "QML Timers do not follow wall clock under this render loop: " + ticks
                   + " ticks of 100 ms in " + Math.round(wall) + " ms. Run with QSG_RENDER_LOOP=basic.")
            verify(Math.abs(oneShot - 2000) <= tol(2000),
                   "a 2000 ms one-shot took " + Math.round(oneShot) + " ms. Run with QSG_RENDER_LOOP=basic.")
        }

        // ── CW1 ─ happy path, order and pacing ───────────────────────────────
        function test_CW01_happyPath() {
            drv.openAtCalibrate("witmotion3", { holdGuide: true })
            var A = drv.imus.A, B = drv.imus.B, C = drv.imus.C
            var dd = drv.d()

            // Nothing starts until the guide is ready — for longer than the intro delay.
            var m0 = drv.mark()
            wait(pace.introStart + 600)
            compare(drv.eventsSince(m0, "wiz", "timer").length, 0, "a timer fired before the guide was ready")
            compare(drv.eventsSince(m0, "wiz", "guide").length, 0, "the guide moved before it was ready")
            compare(drv.callsSince(m0).length, 0)
            compare(drv.stage(), "intro")

            var m = drv.mark()
            drv.releaseGuide()
            var tRel = drv.now()
            tryVerify(function() { return drv.done() }, 45000,
                      "never done (stage " + drv.stage() + ")")
            wait(1000)      // anything after done would show here
            compare(drv.completedSince(m), 1, "completed() not exactly once")
            compare(drv.tingsSince(m), 1, "not exactly one ting for one completed calibration")
            compare(drv.tingCount(), 1, "the flow's TingPlayer played other than once")
            compare(drv.stage(), "done")

            var ev = drv.eventsSince(m, "wiz")
            console.info("[CW1] events " + drv.fmtEvents(ev, tRel))
            function evt(kind, name, nth) {
                var k = 0
                for (var i = 0; i < ev.length; ++i)
                    if (ev[i].kind === kind && ev[i].name === name && k++ === (nth || 0)) return ev[i]
                return null
            }

            // Guide targets in order: T-pose, down, T-pose.
            var targets = ev.filter(function(e) { return e.kind === "guide" && e.name === "armTarget" })
            compare(targets.length, 3, "guide targets " + drv.fmtEvents(targets, tRel))
            verify(quatNear(targets[0].value, dd.tPoseQuat), "1st target is not the T-pose")
            verify(quatNear(targets[1].value, dd.leadArmDownQuat), "2nd target is not arm-down")
            verify(quatNear(targets[2].value, dd.tPoseQuat), "3rd target is not the T-pose")
            var fins = ev.filter(function(e) { return e.kind === "guide" && e.name === "finished" })
            compare(fins.length, 3, "guide finished " + drv.fmtEvents(fins, tRel))

            // Pacing.
            near(evt("timer", "introStart").t - tRel, pace.introStart, "intro delay after guide ready")
            near(fins[0].t - targets[0].t, pace.introAnim, "intro up")
            near(fins[1].t - targets[1].t, pace.introAnim, "intro down")
            near(evt("timer", "introReady").t - fins[1].t, pace.introReady, "settle before phase 1")
            near(evt("timer", "phase1MinHold").t - evt("timer", "introReady").t, pace.minHold, "phase-1 min hold")
            var capA = drv.firstCall("WT-A", "setNominalCalibration")
            verify(capA !== null)
            var tCap = drv.toElapsed(capA.t)
            near(tCap - evt("timer", "phase1MinHold").t, pace.hold, "arm-down capture after 2 s still")
            near(evt("timer", "captureTransition").t - tCap, pace.captureTransition, "capture → raise")
            near(fins[2].t - targets[2].t, pace.raiseAnim, "raise")
            near(evt("timer", "raiseReady").t - fins[2].t, pace.raiseReady, "settle before phase 2")
            var refA = drv.firstCall("WT-A", "refineMountAboutLongAxis")
            verify(refA !== null)
            near(drv.toElapsed(refA.t) - evt("timer", "raiseReady").t, pace.hold, "T-pose capture after 2 s still")

            // setNominalCalibration on A, B, C with each sensor's averaged (constant) quat.
            var insts = { A: A, B: B, C: C }
            for (var s in insts) {
                var inst = insts[s]
                compare(inst.countCalls("setNominalCalibration"), 1, s + " setNominalCalibration")
                compare(inst.countCalls("refineMountAboutLongAxis"), 1, s + " refineMountAboutLongAxis")
                var q = null
                for (var i = 0; i < inst.calls.length; ++i)
                    if (inst.calls[i].name === "setNominalCalibration") q = inst.calls[i].args[0]
                verify(quatNear(q, Qt.quaternion(inst.quatW, inst.quatX, inst.quatY, inst.quatZ)),
                       s + " captured " + q + ", sensor reads " + inst.quatW + "," + inst.quatX)
            }
            compare(drv.wiz.currentStep, drv.wiz.stepCalibrate)
            verify(drv.continueToCheck(), "Continue blocked after done")
        }

        // ── CW2 ─ motion during each hold resets it ──────────────────────────
        // Both holds in one run (the phase-2 hold is only reachable through the phase-1 one).
        function motionResetsHold(phaseStage, acc, capture) {
            var A = drv.imus.A
            tryVerify(function() { return drv.stage() === phaseStage && drv.d()[acc] >= 1000 }, 35000,
                      phaseStage + " hold never reached 1000 ms (stage " + drv.stage() + ")")
            A.angularVelocityDps = 40          // > 15 °/s
            tryVerify(function() { return drv.d()[acc] === 0 }, 500, phaseStage + ": the hold did not reset on motion")
            wait(400)
            compare(drv.d()[acc], 0, phaseStage + ": the hold accumulated while moving")
            compare(A.countCalls(capture), 0, phaseStage + ": captured while moving")
            A.angularVelocityDps = 2           // still again
            var tStill = drv.now()
            tryVerify(function() { return A.countCalls(capture) === 1 }, 5000, phaseStage + ": never captured")
            var tCap = drv.toElapsed(drv.firstCall("WT-A", capture).t)
            console.info("[CW2 " + phaseStage + "] capture " + Math.round(tCap - tStill) + " ms after stillness")
            // The floor is the hold less ONE sampling tick (stillness can land just before a
            // tick) less a frame of timer jitter: 20 ticks summed came in at 1895 ms once in
            // eight runs. A hold that was actually cut short misses by hundreds of ms, not five.
            verify(tCap - tStill >= pace.hold - pace.tick - 20,
                   phaseStage + ": captured " + Math.round(tCap - tStill) + " ms after stillness, < 2 s")
            verify(tCap - tStill <= pace.hold + tol(pace.hold) + pace.tick,
                   phaseStage + ": captured " + Math.round(tCap - tStill) + " ms after stillness")
        }
        function test_CW02_motionResetsHold() {
            drv.openAtCalibrate("witmotion2")
            motionResetsHold("phase1", "phase1AccumMs", "setNominalCalibration")
            motionResetsHold("phase2", "stableAccumMs", "refineMountAboutLongAxis")
        }

        // ── CW3 ─ mount fails on the hand ────────────────────────────────────
        function test_CW03_mountFailsOnHand() {
            open("witmotion3")
            drv.imus.B.mountResult = { anatCalibrated: true, dev: 22.0, grav: 4.0 }   // > 15°
            tryVerify(function() { return drv.stage() === "mountFailed" }, 45000,
                      "never mountFailed (stage " + drv.stage() + ")")
            verify(!drv.done())
            var msg = drv.message()
            console.info("[CW3] message: " + msg)
            verify(msg.indexOf("hand") >= 0, "message does not name the hand: " + msg)
            verify(msg.indexOf("forearm") < 0 && msg.indexOf("upper arm") < 0, "names a passing sensor: " + msg)
            compare(drv.messageKind(), "error")
            verify(drv.textShown(msg), "the message is not on screen")
            drv.wiz.goNext("done")
            compare(drv.wiz.currentStep, drv.wiz.stepCalibrate, "Continue went past a failed mount")

            var m = drv.mark()
            drv.clickFlowRecalibrate()
            var segs = ["WT-A", "WT-B", "WT-C"]
            for (var i = 0; i < segs.length; ++i) {
                compare(drv.callsSince(m, "clearCalibration").filter(function(c) { return c.device === segs[i] }).length,
                        1, segs[i] + " clearCalibration")
                compare(drv.callsSince(m, "clearFunctionalCalibration").filter(function(c) { return c.device === segs[i] }).length,
                        1, segs[i] + " clearFunctionalCalibration")
            }
            compare(drv.stage(), "intro")
            compare(drv.message(), "")
            tryVerify(function() { return drv.animStage() === "introUp" }, sp.introStart + tol(sp.introStart) + 500,
                      "a fresh run did not begin")
        }

        // ── CW4 ─ lead disconnects in phase 1 or 2 ───────────────────────────
        // (The phase-2 row was dropped at Stage 0 for suite time; it is back on the scaled clock.)
        function test_CW04_leadDisconnects_data() {
            return [ { tag: "phase1", stage: "phase1" }, { tag: "phase2", stage: "phase2" } ]
        }
        function test_CW04_leadDisconnects(row) {
            open("witmotion2")
            reach(row.stage)
            drv.fx.imuManager.disconnectDevice("WT-A")
            tryVerify(function() { return drv.failed() }, 1000, "calibrationFailed not set")
            verify(!drv.done())
            console.info("[CW4 " + row.tag + "] badge '" + drv.badgeText() + "', stage " + drv.stage()
                         + ", running timers " + JSON.stringify(drv.runningTimers()))
            // (Observed 5 Oct and fixed at Stage 5b: the shared StatusBadge tested `_calibrating`
            // (calibPhase ≥ 1) BEFORE `_failed`, so it kept reading "Calibrating"; and the hold
            // timer kept running on the dead sensor. The expected-failure marker went with it.)
            compare(drv.badgeText(), "Failed")
            compare(drv.stage(), "failed")
            var running = drv.runningTimers()
            verify(running.indexOf("phase1Hold") < 0 && running.indexOf("stabilityHold") < 0,
                   "a hold timer still runs on the dead sensor: " + JSON.stringify(running))
        }

        // ── CW5 ─ a stalled guide holds the chain ────────────────────────────
        function test_CW05_guideStallHoldsChain_data() {
            return [ { tag: "introUp",   anim: "introUp",   reachMs: 10000,
                       window: sp.introAnim + sp.introAnim + sp.introReady + smargin },
                     // (introDown was dropped at Stage 0 for suite time; back on the scaled clock.)
                     { tag: "introDown", anim: "introDown", reachMs: 10000,
                       window: sp.introAnim + sp.introReady + sp.minHold + smargin },
                     { tag: "raise",     anim: "raise",     reachMs: 25000,
                       window: sp.raiseAnim + sp.raiseReady + 2 * sp.hold + smargin } ]
        }
        function test_CW05_guideStallHoldsChain(row) {
            drv.stall(row.anim)
            open("witmotion2")
            tryVerify(function() { return drv.animStage() === row.anim }, row.reachMs,
                      row.anim + " never started")
            var m = drv.mark()
            var st = drv.stage()
            wait(row.window)
            var detail = "timers " + drv.fmtEvents(drv.eventsSince(m, "wiz", "timer"), m.t)
                         + " guide " + drv.fmtEvents(drv.eventsSince(m, "wiz", "guide"), m.t)
                         + " calls " + drv.fmtCalls(drv.callsSince(m))
            compare(drv.animStage(), row.anim, detail)
            compare(drv.stage(), st, detail)
            verify(drv.guideAnimating(), "the guide is not in flight")
            compare(drv.eventsSince(m, "wiz", "guide").filter(function(e) { return e.name === "finished" }).length, 0, detail)
            compare(drv.eventsSince(m, "wiz", "timer").length, 0, detail)
            compare(drv.callsSince(m).length, 0, detail)
        }

        // ── CW6 ─ Back at each stage ─────────────────────────────────────────
        function test_CW06_backAtEachStage_data() { return leaveRows() }
        function test_CW06_backAtEachStage(row) { leaveAndWatch(row, "back", "CW6") }

        // ── CW7 ─ complete → Check → Back keeps the calibration ──────────────
        function test_CW07_backFromCheckShowsComplete() {
            open("witmotion2")
            tryVerify(function() { return drv.done() }, 45000, "never done (stage " + drv.stage() + ")")
            verify(drv.continueToCheck(), "Continue did not reach Check")
            var m = drv.mark()
            drv.backFromCheck()
            compare(drv.wiz.currentStep, drv.wiz.stepCalibrate)
            var clears = drv.callsSince(m).filter(function(c) {
                return c.name === "clearCalibration" || c.name === "clearFunctionalCalibration" })
            console.info("[CW7/F1] after Back from Check: calls " + drv.fmtCalls(drv.callsSince(m))
                         + "; done " + drv.done() + "; stage " + drv.stage()
                         + "; A.calibrated " + drv.imus.A.calibrated + ", A.anatCalibrated "
                         + drv.imus.A.anatCalibrated)
            compare(clears.length, 0, "clear* calls: " + drv.fmtCalls(clears))
            verify(drv.done(), "does not show complete")
            // Showing complete is not completing: no second ting, no second completed().
            compare(drv.tingsSince(m), 0, "a ting on Back from Check")
            compare(drv.completedSince(m), 0, "completed() again on Back from Check")
            compare(drv.callsSince(m, "setNominalCalibration").length, 0)
            verify(drv.continueToCheck(), "Continue blocked")
        }

        // ── CW8 ─ Skip → Check → Back starts afresh ──────────────────────────
        function test_CW08_skipThenBackRunsFresh() {
            open("witmotion2")
            drv.leaveSkip()
            compare(drv.wiz.currentStep, drv.wiz.stepConfirm)
            var m = drv.mark()
            drv.backFromCheck()
            compare(drv.wiz.currentStep, drv.wiz.stepCalibrate)
            verify(!drv.done())
            compare(drv.stage(), "intro")
            compare(drv.completedSince(m), 0)
            tryVerify(function() { return drv.animStage() === "introUp" }, sp.introStart + tol(sp.introStart) + 500,
                      "no fresh run began")
            compare(drv.callsSince(m, "setNominalCalibration").length, 0)
        }

        // ── CW9 ─ Skip mid-run ───────────────────────────────────────────────
        function test_CW09_skipAtEachStage_data() { return leaveRows() }
        function test_CW09_skipAtEachStage(row) { leaveAndWatch(row, "skip", "CW9") }

        // ── CW10 ─ Settings round trip mid-run ───────────────────────────────
        function test_CW10_settingsRoundTrip_data() {
            // raise: a one-shot was in flight at the suspend. phase1: a hold was accumulating
            // (dropped at Stage 0 for suite time, when it showed a stray capture after resume;
            // back on the scaled clock).
            return [ { tag: "phase1", stage: "phase1" }, { tag: "raise", stage: "raise" } ]
        }
        function test_CW10_settingsRoundTrip(row) {
            open("witmotion2")
            reach(row.stage)
            var m = drv.mark()
            var stageAtSuspend = drv.stage()
            drv.leaveMark = null
            drv.suspend()
            tryVerify(function() { return !drv.flow.visible }, 2000)
            var ml = drv.leaveMark                    // see leaveAndWatch: from the stop, not the request
            verify(ml !== null, "the calibration flow never went inactive")
            var win = leaveWindow(row.stage)
            wait(win)
            var sCalls  = drv.callsSince(ml)
            var sTimers = drv.eventsSince(ml, "wiz", "timer")
            var sStates = drv.eventsSince(m, "wiz", "state")
            var stageAtResume = drv.stage()

            var m2 = drv.mark()
            drv.resume()
            tryVerify(function() { return drv.flow.visible }, 2000)
            var stageAfterResume = drv.stage()
            // A stray capture is one from the ABANDONED hold, so it would land before the fresh
            // run's own phase-1 hold has even begun: watch until the fresh run is in phase 1
            // (Stage 5d — with the 800 ms hold a fixed window let the fresh run's own, legitimate
            // capture land inside it).
            tryVerify(function() { return drv.stage() === "phase1" }, 5000, "the fresh run never reached phase 1")
            var rCaptures = drv.callsSince(m2).filter(function(c) {
                return c.name === "setNominalCalibration" || c.name === "refineMountAboutLongAxis" })
            var detail = "suspended at " + stageAtSuspend + " for " + win + " ms: calls "
                         + drv.fmtCalls(sCalls) + "; timers " + drv.fmtEvents(sTimers, m.t)
                         + "; states " + drv.fmtEvents(sStates, m.t) + "; stage at resume "
                         + stageAtResume + ", right after resume " + stageAfterResume
                         + "; captures after resume " + drv.fmtCalls(rCaptures)
                         + "; stage now " + drv.stage() + "; completed " + drv.completedSince(m)
            console.info("[CW10] " + detail)

            var stoppedOnSuspend = sCalls.length === 0 && sTimers.length === 0
            var restartedOnResume = stageAfterResume === "intro"
            var noStray = rCaptures.length === 0
            verify(stoppedOnSuspend && restartedOnResume && noStray, detail)
        }

        // ── CW11 ─ the toolbar host switches layoutMode ──────────────────────
        function test_CW11_layoutSwitchFollowsGuide_data() {
            return [ { tag: "idle",     run: false },
                     { tag: "midIntro", run: true  } ]
        }
        function test_CW11_layoutSwitchFollowsGuide(row) {
            var f = drv.hostStandalone("witmotion2", "compact", { scale: scaled })
            var v0 = f._guide.view
            verify(v0 !== null)
            compare(drv.guideViews(f).length, 1)
            if (row.run) {
                f.begin()
                tryVerify(function() { return drv.animStage() === "introUp" }, 10000, "intro never started")
            }
            var m = drv.mark()
            f.layoutMode = "full"
            var views = drv.guideViews(f)
            console.info("[CW11 " + row.tag + "] after switch: views " + views.length
                         + ", the guide view is new " + (f._guide.view !== v0) + ", it is the live view "
                         + (views.length === 1 && f._guide.view === views[0]))
            compare(views.length, 1, "one guide view after the switch")
            verify(f._guide.view === views[0], "the guide does not reference the new view")
            if (row.run) {
                // Let the chain run past the point the old view would have finished; any call
                // on a destroyed view surfaces as a warning in cleanup().
                drv._waitGuideLoaded(f)
                wait(sp.introAnim + smargin)
                var after = drv.eventsSince(m)
                console.info("[CW11 midIntro] after the switch: " + drv.fmtEvents(after, m.t)
                             + "; stage " + drv.stage() + ", animStage '" + drv.animStage()
                             + "', guide animating " + drv.guideAnimating())
                // F13 (fixed at Stage 5b): the new view's load used to restart the intro timer, so
                // the intro replayed over the running chain. Now: no second intro, and the chain
                // goes on from where it was (the motion in flight is reported complete once the new
                // view, built at its target, can be watched).
                compare(after.filter(function(e) { return e.kind === "timer" && e.name === "introStart" }).length, 0,
                        "the intro replayed after the layout switch: " + drv.fmtEvents(after, m.t))
                compare(after.filter(function(e) { return e.kind === "state" && e.name === "animStage"
                                                          && e.value === "introUp" }).length, 0,
                        "introUp started again: " + drv.fmtEvents(after, m.t))
                tryVerify(function() { return drv.stage() !== "intro" || drv.animStage() === "introDown"
                                              || drv.animStage() === "" }, 5000,
                          "the chain stalled after the switch (animStage '" + drv.animStage() + "')")
            }
        }

        // ── CW12 ─ slot A changes vendor mid-run ─────────────────────────────
        function test_CW12_slotAChangesVendor_data() {
            // (wt2hm_phase1 was dropped at Stage 0 for suite time; back on the scaled clock.)
            return [ { tag: "wt2hm_phase1", from: "witmotion2", stage: "phase1" },
                     { tag: "wt2hm_raise",  from: "witmotion2", stage: "raise"  },
                     { tag: "hm2wt_done",   from: "hackmotion", stage: "done"   } ]
        }
        function test_CW12_slotAChangesVendor(row) {
            var im
            if (row.from === "witmotion2") {
                // The spare wG3 is "manual" (records calls, never advances): on the scaled clock
                // the HackMotion routine would otherwise COMPLETE inside the window below, and
                // `!done` is about the Witmotion routine not advancing, not about the wG3.
                open("witmotion2", { spareHm: true, hmMode: "manual" })
                im = drv.fx.imuManager
                reach(row.stage)
                var m = drv.mark()
                im.assign("WT-A", ""); im.assign("WT-B", ""); im.assign("HM-1", "A")
                verify(drv.isHackMotion(), "slot A did not become a HackMotion")
                verify(!drv.done())
                compare(drv.phase(), 0, "the Witmotion phase survived the swap")
                // The Witmotion one-shots still in flight would land within this window.
                wait(sp.raiseAnim + sp.raiseReady + 2 * sp.hold + smargin)
                var wtCalls = drv.callsSince(m).filter(function(c) { return c.device !== "HM-1" })
                var detail = "after swap: calls " + drv.fmtCalls(drv.callsSince(m)) + "; states "
                             + drv.fmtEvents(drv.eventsSince(m, "wiz", "state"), m.t) + "; stage "
                             + drv.stage() + ", phase " + drv.phase() + ", mountFailed "
                             + drv.flow.mountFailed + ", done " + drv.done()
                console.info("[CW12 " + row.tag + "] " + detail)
                compare(wtCalls.length, 0, "Witmotion calls after the swap: " + detail)
                // (Stage 0 found the swap left the Witmotion one-shots running — the expected
                // failure below until Stage 2, whose flow stops the old routine on a vendor
                // change and destroys it; the marker was removed then.) What was observed:
                // ⚠ NOT PREDICTED (Base P). Observed 5 Oct: onIsHackMotionChanged zeroes calibPhase
                // but stops none of the Witmotion one-shots (l.404–409), so a raiseReadyTimer in
                // flight sets calibPhase = 2 under the HackMotion routine; stabilityHoldTimer has
                // no vendor gate (l.905) and runs its mount gate over the HmUnit, which has no
                // imuConnected → no segments → mountFailed with an empty name list.
                compare(drv.phase(), 0, "the Witmotion routine advanced after the swap: " + detail)
                verify(!drv.flow.mountFailed, detail)
                verify(!drv.done(), detail)
                compare(drv.hm.countCalls("beginCalibration"), 1, "the HackMotion routine did not begin: " + detail)
            } else {
                open("hackmotion", { spareWitmotion: true })
                im = drv.fx.imuManager
                tryVerify(function() { return drv.done() }, 30000, "HackMotion never done")
                var m2 = drv.mark()
                // (Until Stage 2 this swap produced one production warning, expected here:
                // ImuCalibrationFlow.qml:861 "Detected function onImuConnectedChanged in
                // Connections element" — the Witmotion Connections briefly targeted the HmUnit.
                // The flow now hands the Witmotion routine only Witmotion units, so any warning
                // here fails the case.)
                im.assign("HM-1", ""); im.assign("WT-X", "A")
                verify(!drv.isHackMotion(), "slot A did not become a Witmotion")
                verify(!drv.done(), "a HackMotion done survived into the Witmotion routine")
                compare(drv.stage(), "intro")
                tryVerify(function() { return drv.animStage() === "introUp" }, sp.introStart + tol(sp.introStart) + 500,
                          "the Witmotion routine did not begin")
                compare(drv.completedSince(m2), 0)
                compare(drv.callsSince(m2).filter(function(c) { return c.device === "HM-1" }).length, 0,
                        drv.fmtCalls(drv.callsSince(m2)))
            }
        }

        // ── CW13 ─ one ting per completed calibration, whatever happens after ─
        // The user heard repeated tinging. Completes once, waits 10 s, then drives everything
        // that could flip calibrationDone off and on again: a Settings round trip, a guide
        // leadArmAnimFinished() arriving late, and Back from Check (which shows complete and
        // must not ting; the retired wizard re-ran the routine there — F1).
        function test_CW13_noRepeatedTing() {
            open("witmotion2")
            var m = drv.mark()
            tryVerify(function() { return drv.done() }, 45000, "never done (stage " + drv.stage() + ")")
            compare(drv.tingsSince(m), 1)
            wait(10000)
            compare(drv.tingsSince(m), 1, "a ting in the 10 s after completion")
            drv.suspend(); wait(300); drv.resume(); wait(300)
            compare(drv.tingsSince(m), 1, "a ting on the Settings round trip")
            drv.flow._guide.view.leadArmAnimFinished()           // a late guide completion
            wait(300)
            compare(drv.tingsSince(m), 1, "a ting on a late leadArmAnimFinished")
            verify(drv.continueToCheck())
            drv.backFromCheck()
            wait(sp.introStart + 500)
            console.info("[CW13] tings " + drv.tingsSince(m) + ", completions " + drv.completedSince(m)
                         + ", stage after Back from Check " + drv.stage())
            compare(drv.tingsSince(m), drv.completedSince(m), "tings ≠ completions")
            compare(drv.tingsSince(m), 1)
        }

        // ══ Routine-level cases (Stage 2): a WitmotionArmRoutine alone, over a FakeGuide, on
        // the scaled clock. They pin the routine's own contract — what stop(), `active` and a
        // stalled guide do — which the wizard cases above reach only through a host.

        // Waits for the routine `r` to be at `stage` (as reach() does for the wizard).
        function rReach(r, stage) {
            if (stage === "intro")
                tryVerify(function() { return r._animStage === "introUp" }, 5000, "intro never started")
            else if (stage === "phase1")
                tryVerify(function() { return r.stage === "phase1" && r._phase1MinHoldDone
                                              && r.phase1AccumMs >= sp.hold / 4 }, 5000,
                          "phase-1 hold never accumulated (stage " + r.stage + ")")
            else if (stage === "raise")
                tryVerify(function() { return r._animStage === "raise" }, 5000, "raise never started")
            else if (stage === "phase2")
                tryVerify(function() { return r.stage === "phase2" && r.stableAccumMs >= sp.hold / 4 }, 5000,
                          "phase-2 hold never accumulated (stage " + r.stage + ")")
            compare(r.stage, stage)
        }

        // CWr1 — a full run: the guide is asked for T-pose, down, T-pose; done once; the
        // timers are on the scaled clock; every stage is one app-log line.
        function test_CWr1_fullRun() {
            var r = drv.hostRoutine("witmotion", "witmotion3")
            var g = drv.fakeGuide
            var completions = 0
            r.completed.connect(function() { completions++ })
            compare(r.stage, "idle")
            r.begin()
            compare(r.stage, "intro")
            tryVerify(function() { return r.done }, 10000, "never done (stage " + r.stage + ")")
            wait(200)
            compare(completions, 1)
            compare(r.stage, "done")
            var anims = g.log.filter(function(e) { return e.name === "animate" })
            compare(anims.length, 3, JSON.stringify(anims))
            verify(quatNear(anims[0].arm, r.tPoseQuat) && quatNear(anims[1].arm, r.leadArmDownQuat)
                   && quatNear(anims[2].arm, r.tPoseQuat), "guide targets out of order")
            compare(anims[0].ms, sp.introAnim); compare(anims[1].ms, sp.introAnim)
            compare(anims[2].ms, sp.raiseAnim)
            compare(drv.imus.C.countCalls("setNominalCalibration"), 1)
            compare(drv.imus.C.countCalls("refineMountAboutLongAxis"), 1)
            var log = drv.fx.appLog
            compare(log.matching("witmotion begin", "Calib").length, 1)
            var stages = ["intro", "phase1", "captured", "raise", "phase2", "done"]
            for (var i = 0; i < stages.length; ++i)
                compare(log.matching("witmotion stage=" + stages[i] + "", "Calib").length, 1,
                        "stage " + stages[i] + " — " + JSON.stringify(log.lines))
        }

        // CWr2 — the introDown guide stalls (CW5's dropped row): the chain holds.
        function test_CWr2_stallHoldsIntroDown() {
            var r = drv.hostRoutine("witmotion", "witmotion2")
            var g = drv.fakeGuide
            r.begin()
            rReach(r, "intro")
            g.stall = true
            tryVerify(function() { return r._animStage === "introDown" }, 2000, "introDown never started")
            var m = drv.mark()
            wait(sp.introAnim + sp.introReady + sp.minHold + smargin)
            compare(r._animStage, "introDown")
            compare(r.stage, "intro")
            verify(g.inFlight, "the guide is not in flight")
            compare(drv.eventsSince(m, "routine", "timer").length, 0)
            compare(drv.callsSince(m).length, 0)
        }

        // CWr3 — stop(reason) or `active` false at each stage (CW6/CW9's dropped rows, at the
        // routine): nothing fires, nothing is called, the guide is cancelled, the stage is idle.
        function test_CWr3_stopAtEachStage_data() {
            var out = [], stages = ["intro", "phase1", "raise", "phase2"]
            for (var i = 0; i < stages.length; ++i) {
                out.push({ tag: "stop_" + stages[i], stage: stages[i], how: "stop" })
                out.push({ tag: "inactive_" + stages[i], stage: stages[i], how: "inactive" })
            }
            return out
        }
        function test_CWr3_stopAtEachStage(row) {
            var r = drv.hostRoutine("witmotion", "witmotion2")
            var g = drv.fakeGuide
            var completions = 0
            r.completed.connect(function() { completions++ })
            r.begin()
            rReach(r, row.stage)
            var m = drv.mark()
            var cancels = g.cancels
            if (row.how === "stop") r.stop("back")
            else r.active = false
            compare(r.stage, "idle")
            verify(g.cancels > cancels, "the guide was not cancelled")
            verify(!g.inFlight)
            wait(leaveWindow(row.stage))
            var detail = "timers " + drv.fmtEvents(drv.eventsSince(m, "routine", "timer"), m.t)
                         + " calls " + drv.fmtCalls(drv.callsSince(m))
            compare(drv.eventsSince(m, "routine", "timer").length, 0, detail)
            compare(drv.callsSince(m).length, 0, detail)
            compare(completions, 0)
            compare(r.stage, "idle")
            compare(drv.fx.appLog.matching("stop reason=" + (row.how === "stop" ? "back" : "inactive")
                                           + " stage=" + row.stage, "Calib").length, 1,
                    JSON.stringify(drv.fx.appLog.lines))
        }

        // CWr4 — `active` false in phase 1, then true and begin() again (CW10's dropped row, at
        // the routine): a fresh run, and no capture from the abandoned hold.
        function test_CWr4_inactiveThenFreshBegin() {
            var r = drv.hostRoutine("witmotion", "witmotion2")
            r.begin()
            rReach(r, "phase1")
            r.active = false
            wait(2 * sp.hold + smargin)
            var m = drv.mark()
            r.active = true
            compare(r.stage, "idle", "re-activation alone must not resume the run")
            r.begin()
            compare(r.stage, "intro")
            // No capture from the abandoned hold: none before the fresh run is itself in phase 1
            // (Stage 5d — see CW10).
            tryVerify(function() { return r.stage === "phase1" }, 5000, "the fresh run never reached phase 1")
            compare(drv.callsSince(m, "setNominalCalibration").length, 0, drv.fmtCalls(drv.callsSince(m)))
            tryVerify(function() { return r.done }, 10000, "the fresh run never completed")
        }

        // CW14 — the toolbar popup on Witmotion sensors: closing it mid-run is leaving (nothing
        // fires or is called, no ting); reopening in calibrate mode runs FRESH; closing after
        // completion and reopening re-runs nothing and does not ting.
        function test_CW14_toolbarPopupCloseIsLeave() {
            drv.hostToolbar("witmotion2", { scale: scaled })
            drv.openToolbar()
            drv.panel.mode = "calibrate"
            verify(drv.flow.active, "the panel flow is not active in calibrate mode")
            tryVerify(function() { return drv.animStage() === "raise" }, 10000, "no raise (" + drv.stage() + ")")
            var m = drv.mark()
            drv.closeToolbar()
            verify(!drv.panel.visible, "closing the popup did not hide its contentItem")
            verify(!drv.flow.active, "the flow is still active with the popup closed")
            compare(drv.stage(), "idle")
            verify(!drv.guideAnimating(), "the guide is still animating")
            wait(leaveWindow("raise"))
            compare(drv.callsSince(m).length, 0, drv.fmtCalls(drv.callsSince(m)))
            compare(drv.eventsSince(m, "panel", "timer").length, 0)
            compare(drv.tingsSince(m, "panel"), 0)

            var m2 = drv.mark()
            drv.openToolbar()
            compare(drv.stage(), "intro", "reopening did not start a fresh run")
            compare(drv.callsSince(m2, "clearCalibration").length, 2)
            tryVerify(function() { return drv.done() }, 10000, "the fresh run never completed (" + drv.stage() + ")")
            wait(200)
            compare(drv.tingsSince(m2, "panel"), 1)
            compare(drv.panel.mode, "list")

            var m3 = drv.mark()
            drv.closeToolbar()
            drv.openToolbar()
            wait(sp.introStart + smargin)
            compare(drv.callsSince(m3).length, 0, drv.fmtCalls(drv.callsSince(m3)))
            compare(drv.tingsSince(m3, "panel"), 0)
            verify(drv.done())
        }
    }
}
