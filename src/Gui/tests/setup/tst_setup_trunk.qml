// W2.2 — the trunk calibration routine and its session-setup pages (trunk_imu_sequence_grf_design.md
// §6.3, §6.7, §16.2; session_wizard_refactor_design.md §4.7, §4.10).
//
// Two hosts:
//   TR*  the routine alone (TrunkRoutine) over a FakeGuide, two FakeImuInstances and a
//        FakeTrunkCeremony, on a scaled clock — every path and its exact call order. The turn steps
//        end on the sensors, so a TrunkGolfer (support/) turns the fake sensors out and back
//        whenever a turn step arms; a case disables it or gives it its own movements;
//   TP*  the real shell (support/SetupDriver.qml): the Calibrate trunk and Check trunk pages, their
//        descriptors, the draft, Ready, and the arm step before them with a wG3.
//
//   TR1  pass path: the stages in order (one app-log line each, R10), the sensors cleared before the
//        hold, both solves, the guide's demonstrations, the recorder's calls EXACTLY, completed once
//   TR2  a stand-tall failure names the sensor (the record's own words) and repeats stand tall only
//   TR3  a turn failure (too little) shows its verdict, then the turn RE-ARMS by itself on stillness —
//        no demo again, no click — and passes
//   TR4  a sensor reading the turn backwards — on turn THROUGH — goes back to stand tall, then both
//        turns again; "swapped" does the same
//   TR5  the golfer turned the other way: the hand-setting hint, on either turn; the turn repeats
//   TR6  leave at every step (R3): nothing runs after stop — no timer, no device or recorder call, no
//        guide callback; the open step is closed on the recorder
//   TR7  restore() shows the finished run from the sensors and calls nothing
//   TR8  a trunk sensor that drops its link fails the run, names it, and ends the recording
//   TR9  nothing starts until the guide can be watched
//   TR10 stand tall starts hands-free: it waits at ready until BOTH sensors are upright and still,
//        names the sensor that holds it up, logs the numbers once per waitLog, then runs by itself
//   TR11 the hold bar empties on a sensor that moves or tips, and says which
//   TR12 a failed stand tall (sensors lying flat) waits at ready again — no blind re-run
//   ── The turn steps, taught facing the screen and run by the sensors (Mark, 26 Oct: "how am I
//      supposed to read and turn away!?") ──
//   TR13 a full turn away and back completes each step with no click and no timer (the timeout is out
//        of reach); the recorded window opens at ARMING — nothing is open through the demo or the
//        arming — and the stage log reads demo → arming → armed → turning → returned
//   TR14 a turn out that never comes back is ended and checked at the timeout, then re-arms there
//   TR15 a turn that never starts waits, armed, saying so on screen and in the log (rate-limited):
//        no check, no verdict — a sway under the start threshold and a lost reading change nothing
//   TR16 motion while arming does not arm (the moving sensor named), nor does a missing turn reading;
//        still again → armed
//   TP1  trunk-only session: the plan, the routine on the page, the outcome, Check's one line of
//        numbers and the live figure turning with them (W2.3), Ready's "Trunk calibration" row, the
//        recorder's calls
//   TP2  back from Check: Calibrate trunk shows complete, re-runs nothing (R8)
//   TP3  leave the page mid-step: nothing fires after the leave (the leave mark, as CalibDriver's)
//   TP4  trunk + wG3: the arm steps first; the recording begins at the arm's Calibrate step and the
//        trunk routine continues it (no second begin)
//   TP5  setup left without a session (Cancel) discards the ceremony; a new visit discards a stale one
//   TP7  leave mid-turn (turning) with Back: the open turn step is closed; Next runs it all again
//        in the same recording
//   TP6  Mark's 26 Oct path: stand tall fails (retry), Back to Sensors, Next to Calibrate trunk, the
//        sensors upright and still → the solve passes and the run completes; then Recalibrate
//        mid-hold → the same
import QtQuick
import QtTest
import PinPointStudio
import "support"
import "fakes"

// ⚠ THE ROOT IS A PLAIN Item, NOT THE TestCase (see tst_setup_smoke.qml).
Item {
    id: root
    width: 1400
    height: 900

    readonly property var _testLog: testLog

    Item { id: host; anchors.fill: parent }
    SetupDriver { id: drv; host: host; testCase: tc; testLog: root._testLog }

    TestCase {
        id: tc
        name: "SetupTrunk"
        when: windowShown

        readonly property var d: drv
        readonly property int wrist: SessionController.Wrist

        Component { id: appLogComp;   FakeAppLog {} }
        Component { id: guideComp;    FakeGuide {} }
        Component { id: ceremonyComp; FakeTrunkCeremony {} }
        Component { id: instComp;     FakeImuInstance {} }
        Component { id: golferComp;   TrunkGolfer {} }

        // The scaled clock: × 0.05, the sampling tick kept at 20 ms (CalibDriver.scaledPaceExtra).
        readonly property var pace: ({ scale: 0.05, tick: 400 })
        function paced(extra) {
            var o = {}
            for (var k in pace) o[k] = pace[k]
            for (var e in (extra || {})) o[e] = extra[e]
            return o
        }

        // ── The routine host (TR*) ───────────────────────────────────────────
        property var r: null
        property var log: null
        property var guide: null
        property var cer: null
        property var pel: null
        property var tho: null
        property var golfer: null
        property int completedCount: 0
        function hostRoutine(opts) {
            opts = opts || {}
            log   = createTemporaryObject(appLogComp, tc)
            guide = createTemporaryObject(guideComp, tc)
            cer   = createTemporaryObject(ceremonyComp, tc)
            pel   = createTemporaryObject(instComp, tc, { deviceId: "WT-P", imuConnected: true })
            tho   = createTemporaryObject(instComp, tc, { deviceId: "WT-T", imuConnected: true })
            r = harness.createWithContext("TrunkRoutine", { appLog: log }, null, {
                guide: guide, segments: { pelvis: pel, thorax: tho }, trunkCeremony: cer,
                rightHanded: opts.leftHanded !== true, active: true, pace: paced(opts.pace) })
            verify(r !== null, "TrunkRoutine failed to instantiate")
            golfer = createTemporaryObject(golferComp, tc, { routine: r })
            completedCount = 0
            r.completed.connect(function() { tc.completedCount += 1 })
            return r
        }
        function stageLines() {
            return log.matching("trunk stage=", "Calib").map(function(l) { return l.replace("[Calib] trunk ", "") })
        }
        // The stage lines with every measured angle as "#" (the tick that sees a frame varies).
        function stageShape() {
            return stageLines().map(function(l) { return l.replace(/=-?[0-9.]+deg/g, "=#") })
        }
        function waitFor(cond, ms, what) {
            var t0 = Date.now()
            while (!cond() && Date.now() - t0 < ms) wait(10)
            verify(cond(), what + " (stage " + (r ? r.stage + "/" + r.substage : "-") + ")\n"
                           + (log ? log.lines.join("\n") : ""))
        }
        function waitDone() { waitFor(function() { return r.done }, 10000, "the run never completed") }
        function sensorCalls(inst) { return inst.calls.map(function(c) { return c.name }) }

        // The routine's timers and connections, by name (TrunkRoutine.qml).
        readonly property var timerNames: ["_introTimer", "_readyTimer", "_settleTimer", "_holdTimer", "_demoPauseTimer",
                                           "_armTimer", "_turnTimer", "_nextTimer", "_retryTimer"]
        readonly property var connNames:  ["_guideFinished", "_pelvisLink", "_thoraxLink"]

        function init() {
            testLog.reset()
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            d.setUp()
        }
        function cleanup() {
            if (golfer) golfer.routine = null
            if (r) harness.destroyNow(r)
            r = null
            d.tearDown()
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        readonly property var passOrder: [
            "begin",
            "stepStart:standTall", "attachResult:standTall:pelvis", "attachResult:standTall:thorax", "stepEnd:standTall",
            "stepStart:turnAway", "checkTurn:away", "stepEnd:turnAway",
            "stepStart:turnThrough", "checkTurn:through", "stepEnd:turnThrough",
            "end"]

        // ═════ The routine (TR) ═══════════════════════════════════════════════
        function test_TR1_passPath() {
            hostRoutine()
            r.begin()
            compare(r.stage, "intro")
            waitDone()
            compare(r.stage, "done")
            compare(completedCount, 1)
            compare(golfer.turns, 2, "one turn per step")
            compare(stageShape(), ["stage=intro",
                                   "stage=standTall step=ready", "stage=standTall step=settle upright and still",
                                   "stage=standTall step=hold",
                                   "stage=standTall step=passed pelvis=pass thorax=pass",
                                   "stage=turnAway step=demo", "stage=turnAway step=arming",
                                   "stage=turnAway step=armed heading=#",
                                   "stage=turnAway step=turning started peak=#",
                                   "stage=turnAway step=returned peak=# turn=#",
                                   "stage=turnAway step=passed turn=valid",
                                   "stage=turnThrough step=demo", "stage=turnThrough step=arming",
                                   "stage=turnThrough step=armed heading=#",
                                   "stage=turnThrough step=turning started peak=#",
                                   "stage=turnThrough step=returned peak=# turn=#",
                                   "stage=done turn=valid"])
            compare(cer.order(), passOrder)
            // checkTurn asked for a right-hander, both directions.
            compare(cer.calls.filter(function(c) { return c.name === "checkTurn" }).map(function(c) { return c.args }),
                    [["away", false], ["through", false]])
            // Each sensor: cleared (both calibrations) before the solve, then solved for its own role.
            ;[[pel, "pelvis"], [tho, "thorax"]].forEach(function(p) {
                var names = sensorCalls(p[0])
                var solve = names.indexOf("calibrateTrunk")
                verify(solve > 0, JSON.stringify(names))
                verify(names.slice(0, solve).indexOf("clearFunctionalCalibration") >= 0, JSON.stringify(names))
                verify(names.slice(0, solve).indexOf("clearCalibration") >= 0, JSON.stringify(names))
                compare(p[0].countCalls("calibrateTrunk"), 1)
                compare(p[0].calls[solve].args, [p[1]])
                verify(p[0].fullyCalibrated, p[1] + " not fullyCalibrated after a valid solve")
            })
            // The attached records are calibrateTrunk's own.
            var att = cer.calls.filter(function(c) { return c.name === "attachResult" })
            compare(att[0].args[1].role, "pelvis"); compare(att[0].args[1].deviceId, "WT-P")
            compare(att[1].args[1].role, "thorax"); compare(att[1].args[1].deviceId, "WT-T")
            // The guide: stand tall, then each turn demonstrated out and back (golfer-relative, + away).
            var anims = guide.log.filter(function(e) { return e.name === "trunkAnimate" })
                                 .map(function(e) { return [e.hips, e.chest, e.crossed] })
            compare(anims, [[45, 90, true], [0, 0, true], [-45, -90, true], [0, 0, true]])
            compare(guide.log.filter(function(e) { return e.name === "trunkPose" })[0].crossed, false)
            compare(r.message, "")
            compare(r.holdProgress, 1)
            verify(r.turnResults.away.valid && r.turnResults.through.valid)
        }

        function test_TR2_standTallFailureNamesTheSensorAndRepeatsStandTallOnly() {
            hostRoutine({ pace: { retry: 4000 } })            // × 0.05 = 200 ms on screen
            pel.trunkScript = [{ valid: false, stillOk: false,
                                 failure: "Hip sensor moved during stand tall — hold still and try again" }]
            r.begin()
            waitFor(function() { return r.substage === "retry" }, 5000, "no retry after the failed stand tall")
            compare(r.stage, "standTall")
            compare(r.message, "Hip sensor moved during stand tall — hold still and try again")
            compare(r.messageKind, "error")
            verify(!r.standTallResult.pelvis.valid && r.standTallResult.thorax.valid)
            waitDone()
            compare(cer.order(), ["begin",
                                  "stepStart:standTall", "attachResult:standTall:pelvis", "attachResult:standTall:thorax", "stepEnd:standTall",
                                  "stepStart:standTall", "attachResult:standTall:pelvis", "attachResult:standTall:thorax", "stepEnd:standTall",
                                  "stepStart:turnAway", "checkTurn:away", "stepEnd:turnAway",
                                  "stepStart:turnThrough", "checkTurn:through", "stepEnd:turnThrough", "end"])
            compare(pel.countCalls("calibrateTrunk"), 2)
            compare(tho.countCalls("calibrateTrunk"), 2)
            // Both sensors are cleared again before the second hold (the first may have applied one).
            var names = sensorCalls(tho)
            var second = names.lastIndexOf("calibrateTrunk"), first = names.indexOf("calibrateTrunk")
            verify(names.slice(first + 1, second).indexOf("clearFunctionalCalibration") >= 0, JSON.stringify(names))
            compare(r.message, "", "the failure outlived the stand tall that passed")
            compare(completedCount, 1)
        }

        function test_TR3_turnFailureRepeatsTheTurn() {
            hostRoutine({ pace: { retry: 4000 } })
            cer.turnScript = [{ valid: false, cause: "range",
                                failure: "Chest turned too little — turn further, as if to the top, and back" }]
            r.begin()
            waitFor(function() { return r.substage === "retry" }, 5000, "no retry after the failed turn")
            compare(r.stage, "turnAway")
            compare(r.message, "Chest turned too little — turn further, as if to the top, and back")
            compare(r.messageKind, "warn")
            // The verdict, then the turn re-arms by itself — no demo again; the verdict stays on screen.
            waitFor(function() { return r.substage === "arming" }, 5000, "the failed turn did not re-arm")
            compare(r.stage, "turnAway")
            compare(r.message, "Chest turned too little — turn further, as if to the top, and back")
            waitDone()
            var afterRetry = stageShape().slice(stageShape().indexOf("stage=turnAway step=retry turn=fail cause=range"))
            compare(afterRetry.slice(0, 6), ["stage=turnAway step=retry turn=fail cause=range",
                                             "stage=turnAway step=arming", "stage=turnAway step=armed heading=#",
                                             "stage=turnAway step=turning started peak=#",
                                             "stage=turnAway step=returned peak=# turn=#",
                                             "stage=turnAway step=passed turn=valid"])
            compare(guide.count("trunkAnimate"), 4, "the demo was replayed on the retry")
            compare(golfer.turns, 3)
            compare(cer.order(), ["begin",
                                  "stepStart:standTall", "attachResult:standTall:pelvis", "attachResult:standTall:thorax", "stepEnd:standTall",
                                  "stepStart:turnAway", "checkTurn:away", "stepEnd:turnAway",
                                  "stepStart:turnAway", "checkTurn:away", "stepEnd:turnAway",
                                  "stepStart:turnThrough", "checkTurn:through", "stepEnd:turnThrough", "end"])
            compare(pel.countCalls("calibrateTrunk"), 1, "a turn failure repeated stand tall")
        }

        function test_TR4_backwardsGoesBackToStandTall_data() {
            return [{ tag: "backwards", cause: "backwards",
                      failure: "Chest sensor reads the turn backwards — check it sits USB-down, label facing out" },
                    { tag: "swapped", cause: "swapped", failure: "Hip and chest sensors look swapped" }]
        }
        function test_TR4_backwardsGoesBackToStandTall(row) {
            hostRoutine({ pace: { retry: 4000 } })
            // Away passes; THROUGH reads backwards.
            cer.turnScript = [{}, { valid: false, cause: row.cause, failure: row.failure }]
            r.begin()
            waitFor(function() { return r.substage === "retry" }, 5000, "no retry")
            compare(r.stage, "turnThrough")
            compare(r.message, row.failure + "\nThen stand tall again: a sensor that has moved needs a fresh stand tall.")
            compare(r.messageKind, "error")
            waitFor(function() { return r.stage === "standTall" }, 5000, "not sent back to stand tall")
            waitDone()
            compare(cer.order(), ["begin",
                                  "stepStart:standTall", "attachResult:standTall:pelvis", "attachResult:standTall:thorax", "stepEnd:standTall",
                                  "stepStart:turnAway", "checkTurn:away", "stepEnd:turnAway",
                                  "stepStart:turnThrough", "checkTurn:through", "stepEnd:turnThrough",
                                  "stepStart:standTall", "attachResult:standTall:pelvis", "attachResult:standTall:thorax", "stepEnd:standTall",
                                  "stepStart:turnAway", "checkTurn:away", "stepEnd:turnAway",
                                  "stepStart:turnThrough", "checkTurn:through", "stepEnd:turnThrough", "end"])
            // The re-seated sensor's A/M went: both cleared before the second solve.
            var names = sensorCalls(pel)
            verify(names.slice(names.indexOf("calibrateTrunk") + 1, names.lastIndexOf("calibrateTrunk"))
                        .indexOf("clearFunctionalCalibration") >= 0, JSON.stringify(names))
            compare(completedCount, 1)
        }

        function test_TR5_wrongWayHintsAtTheHand_data() {
            return [{ tag: "right-handed, away", leftHanded: false, at: 0, stage: "turnAway",
                      failure: "You turned toward the target — turn away from it, as if to the top" },
                    { tag: "left-handed, through", leftHanded: true, at: 1, stage: "turnThrough",
                      failure: "You turned away from the target — turn toward it this time" }]
        }
        function test_TR5_wrongWayHintsAtTheHand(row) {
            hostRoutine({ leftHanded: row.leftHanded, pace: { retry: 4000 } })
            var script = []
            for (var i = 0; i < row.at; ++i) script.push({})
            script.push({ valid: false, cause: "wrongWay", wrongWay: true, failure: row.failure })
            cer.turnScript = script
            r.begin()
            waitFor(function() { return r.substage === "retry" }, 5000, "no retry")
            compare(r.stage, row.stage)
            var hint = row.leftHanded
                ? "Turned the way the guide showed? This golfer is set up as left-handed — if that is wrong, change it in the golfer's profile, then Recalibrate."
                : "Turned the way the guide showed? This golfer is set up as right-handed — if that is wrong, change it in the golfer's profile, then Recalibrate."
            compare(r.message, row.failure + "\n" + hint)
            compare(r.messageKind, "warn")
            waitDone()
            // The turn repeated; stand tall did not.
            compare(pel.countCalls("calibrateTrunk"), 1)
            compare(cer.calls.filter(function(c) { return c.name === "checkTurn" })
                       .map(function(c) { return c.args[1] }).every(function(lh) { return lh === row.leftHanded }), true)
        }

        // Each row leaves the routine somewhere it would otherwise stay or move on from.
        function test_TR6_leaveAtEveryStep_data() {
            return [{ tag: "intro",  stage: "intro",       sub: "",       open: "" },
                    { tag: "ready",  stage: "standTall",   sub: "ready",  open: "standTall", moving: true },
                    { tag: "settle", stage: "standTall",   sub: "settle", open: "standTall", pace: { settle: 40000 } },
                    { tag: "hold",   stage: "standTall",   sub: "hold",   open: "standTall", pace: { hold: 400000 } },
                    { tag: "demo",   stage: "turnAway",    sub: "demo",   open: "",          stall: true },
                    { tag: "arming", stage: "turnAway",    sub: "arming", open: "",          moveAtArming: true },
                    { tag: "armed",  stage: "turnAway",    sub: "armed",  open: "turnAway",  noGolfer: true },
                    { tag: "turning", stage: "turnAway",   sub: "turning", open: "turnAway", pace: { turnTimeout: 400000 },
                      turns: [[{ chest: 0, gyro: 0, ms: 20 }, { chest: 50, gyro: 80, ms: 40 }, { chest: 90, gyro: 0, ms: 0 }]] },
                    { tag: "retry",  stage: "turnAway",    sub: "retry",  open: "",          pace: { retry: 400000 },
                      script: [{ valid: false, cause: "range", failure: "Hips turned too little" }] },
                    { tag: "through demo", stage: "turnThrough", sub: "demo", open: "", stallSecond: true }]
        }
        function test_TR6_leaveAtEveryStep(row) {
            hostRoutine({ pace: row.pace || ({ introStart: 4000 }) })
            if (row.script) cer.turnScript = row.script
            if (row.moving) { pel.gyroDps = 40; tho.gyroDps = 40 }
            if (row.stall)  guide.stall = true
            if (row.noGolfer) golfer.enabled = false
            if (row.turns) golfer.scripts = row.turns
            if (row.moveAtArming)
                r.substageChanged.connect(function() { if (r.substage === "arming") tho.gyroDps = 40 })
            r.begin()
            if (row.stallSecond) {         // the second demonstration stalls, once the first is done
                waitFor(function() { return r.stage === "turnAway" && (r.substage === "armed" || r.substage === "turning") },
                        5000, "away never armed")
                guide.stall = true
            }
            waitFor(function() { return r.stage === row.stage && r.substage === row.sub }, 8000,
                    "never reached " + row.stage + "/" + row.sub)
            wait(30)                       // inside the step, not on its edge
            compare(r.stage, row.stage); compare(r.substage, row.sub)
            var cancels = guide.cancels
            r.active = false               // the host's leave: stop("inactive"), synchronously
            // The leave mark (as CalibDriver.leaveMark): everything after this is a stray.
            var mark = { cer: cer.calls.length, pel: pel.calls.length, tho: tho.calls.length,
                         guide: guide.log.length, log: log.lines.length }
            compare(r.stage, "idle")
            verify(guide.cancels > cancels, "the guide was not cancelled")
            // The open step is closed on the recorder, by the stop itself.
            if (row.open !== "") {
                compare(cer.order()[cer.calls.length - 1], "stepEnd:" + row.open)
                compare(cer.currentStep, "")
            }
            // No timer runs, no connection listens.
            for (var i = 0; i < timerNames.length; ++i)
                verify(!r[timerNames[i]].running, timerNames[i] + " still running after leave")
            for (var j = 0; j < connNames.length; ++j)
                verify(!r[connNames[j]].enabled, connNames[j] + " still enabled after leave")
            // A late guide callback and a sensor change are ignored — a still sensor, and one turning.
            guide.finished()
            pel.imuConnected = false
            pel.stopPlay(); tho.stopPlay()
            pel.gyroDps = 0; tho.gyroDps = 0
            tho.headingDeg = tho.headingDeg + 120
            wait(600)
            compare(cer.calls.length, mark.cer, "recorder called after leave: " + JSON.stringify(cer.order().slice(mark.cer)))
            compare(pel.calls.length, mark.pel, "hip sensor called after leave")
            compare(tho.calls.length, mark.tho, "chest sensor called after leave")
            compare(guide.log.length, mark.guide, "guide driven after leave")
            compare(log.lines.length, mark.log, "the routine logged after leave: " + log.lines.slice(mark.log).join(" | "))
            compare(r.stage, "idle")
            compare(completedCount, 0)
        }

        function test_TR7_restoreShowsTheFinishedRun() {
            hostRoutine()
            r.begin()
            waitDone()
            harness.destroyNow(r)
            var nCer = cer.calls.length, nPel = pel.calls.length, nTho = tho.calls.length
            r = harness.createWithContext("TrunkRoutine", { appLog: log }, null, {
                guide: guide, segments: { pelvis: pel, thorax: tho }, trunkCeremony: cer,
                active: true, pace: paced() })
            completedCount = 0
            r.completed.connect(function() { tc.completedCount += 1 })
            r.restore()
            compare(r.stage, "done")
            verify(r.done)
            compare(r.holdProgress, 1)
            compare(r.standTallResult.pelvis.role, "pelvis")
            compare(r.standTallResult.thorax.role, "thorax")
            wait(300)
            compare(cer.calls.length, nCer, "restore touched the recorder")
            compare(pel.calls.length, nPel, "restore touched the hip sensor")
            compare(tho.calls.length, nTho, "restore touched the chest sensor")
            compare(completedCount, 0, "a restore is not a completion")
            verify(log.lines[log.lines.length - 1].indexOf("restore done=true solves=both") >= 0, log.lines.join("\n"))
        }

        function test_TR8_linkDropFailsTheRun() {
            hostRoutine()
            golfer.enabled = false
            r.begin()
            waitFor(function() { return r.substage === "armed" }, 5000, "never armed the turn")
            tho.imuConnected = false
            compare(r.stage, "failed")
            verify(r.failed)
            compare(r.message, "Chest sensor lost its connection — reconnect it on the Sensors step, then Recalibrate")
            compare(cer.order().slice(-2), ["stepEnd:turnAway", "end"])
            compare(completedCount, 0)
            // A sensor that is not connected at begin fails at once, by name.
            harness.destroyNow(r)
            r = harness.createWithContext("TrunkRoutine", { appLog: log }, null, {
                guide: guide, segments: { pelvis: pel, thorax: tho }, trunkCeremony: cer, active: true, pace: paced() })
            r.begin()
            compare(r.stage, "failed")
            compare(r.message, "Chest sensor is not connected — connect it on the Sensors step, then Recalibrate")
        }

        function test_TR9_nothingStartsBeforeTheGuideIsReady() {
            hostRoutine()
            guide.ready = false
            r.begin()
            wait(500)
            compare(r.stage, "intro")
            compare(cer.order(), ["begin"])
            compare(pel.countCalls("calibrateTrunk"), 0)
            guide.ready = true
            waitDone()
        }

        function waitLines() { return log.matching("trunk wait ", "Calib") }

        function test_TR10_readyWaitsForUprightAndStill() {
            // Only one wait line however long the wait (waitLog ≫ the test), so the line can be pinned.
            hostRoutine({ pace: { waitLog: 400000 } })
            pel.longAxisFromUpDeg = 89          // the hip sensor lies on the desk
            tho.gyroDps = 30                    // the chest sensor is being put on
            r.begin()
            waitFor(function() { return r.substage === "ready" }, 5000, "never reached ready")
            wait(400)                           // 20 ready ticks: nothing moves on
            compare(r.stage, "standTall"); compare(r.substage, "ready")
            compare(r.waitReason, "Hip sensor isn't upright\nChest sensor is moving")
            compare(pel.countCalls("calibrateTrunk"), 0)
            compare(r.holdProgress, 0)
            compare(waitLines(), ["[Calib] trunk wait step=ready pelvis gyro=0.0dps long=89.0deg thorax gyro=30.0dps long=0.0deg"
                                  + " — Hip sensor isn't upright; Chest sensor is moving"])
            // Upright, but the chest still moving: the reason narrows to the chest.
            pel.longAxisFromUpDeg = 12
            waitFor(function() { return r.waitReason === "Chest sensor is moving" }, 2000, "the reason did not narrow")
            compare(r.substage, "ready")
            // Within the gates — upright inside 35°, still under 10 °/s (a person holding still): it runs.
            pel.longAxisFromUpDeg = 34; tho.gyroDps = 9
            waitDone()
            compare(stageLines().slice(0, 4), ["stage=intro", "stage=standTall step=ready",
                                               "stage=standTall step=settle upright and still", "stage=standTall step=hold"])
            compare(pel.countCalls("calibrateTrunk"), 1)
        }

        function test_TR10b_waitLogIsRateLimited() {
            hostRoutine({ pace: { waitLog: 4000 } })      // × 0.05 = 200 ms between lines
            pel.gyroDps = 20
            r.begin()
            waitFor(function() { return r.substage === "ready" }, 5000, "never reached ready")
            var t0 = Date.now()
            wait(1000)
            var n = waitLines().length, elapsed = Date.now() - t0
            verify(n >= 2 && n <= Math.ceil(elapsed / 200) + 1, n + " wait lines in " + elapsed + " ms")
            verify(waitLines()[0].indexOf("trunk wait step=ready pelvis gyro=20.0dps") >= 0, waitLines()[0])
        }

        function test_TR11_holdEmptiesOnASensorThatMovesOrTips() {
            hostRoutine({ pace: { hold: 400000 } })       // the bar never fills by itself
            r.begin()
            waitFor(function() { return r.substage === "hold" && r.holdProgress > 0 }, 5000, "the bar never filled")
            compare(r.waitReason, "")
            tho.gyroDps = 20
            waitFor(function() { return r.holdProgress === 0 }, 2000, "motion did not empty the bar")
            compare(r.waitReason, "Chest sensor is moving")
            verify(waitLines().length >= 1 && waitLines()[0].indexOf("trunk wait step=hold") >= 0, waitLines().join("\n"))
            tho.gyroDps = 0
            pel.longAxisFromUpDeg = 60
            wait(100)
            compare(r.holdProgress, 0)
            compare(r.waitReason, "Hip sensor isn't upright")
            pel.longAxisFromUpDeg = 5
            waitFor(function() { return r.holdProgress > 0 }, 2000, "the bar did not fill again")
            compare(r.waitReason, "")
            compare(r.substage, "hold")
        }

        function test_TR12_failedStandTallWaitsForUprightAgain() {
            hostRoutine({ pace: { retry: 4000 } })
            pel.trunkScript = [{ valid: false, faceOk: false, longAxisOk: false,
                                 failure: "Hip sensor is lying flat — wear it upright: USB down, label facing out" }]
            r.begin()
            waitFor(function() { return r.substage === "retry" }, 5000, "no retry after the failed stand tall")
            // Both sensors are back on the desk.
            pel.longAxisFromUpDeg = 89; tho.longAxisFromUpDeg = 91
            waitFor(function() { return r.substage === "ready" }, 5000, "the retry did not go back to ready")
            wait(400)
            compare(r.substage, "ready")
            compare(pel.countCalls("calibrateTrunk"), 1, "stand tall re-ran on sensors lying flat")
            compare(r.message, "Hip sensor is lying flat — wear it upright: USB down, label facing out")
            compare(r.waitReason, "Hip sensor isn't upright\nChest sensor isn't upright")
            pel.longAxisFromUpDeg = 3; tho.longAxisFromUpDeg = 4
            waitDone()
            compare(pel.countCalls("calibrateTrunk"), 2)
            compare(r.message, "")
        }

        // ═════ The turn steps: taught facing the screen, run by the sensors ═══════
        function turnLines(what) { return stageLines().filter(function(l) { return l.indexOf(what) >= 0 }) }

        function test_TR13_aFullTurnCompletesTheStepNoClickNoTimer() {
            hostRoutine({ pace: { turnTimeout: 40000000 } })     // × 0.05 = 2000 s: never the timeout
            // What the recorder holds open at each turn substage, as the routine enters it.
            var seen = []
            r.substageChanged.connect(function() {
                if (r.stage === "turnAway" || r.stage === "turnThrough")
                    seen.push(r.stage + "/" + r.substage + ":" + cer.currentStep)
            })
            r.begin()
            waitDone()
            compare(completedCount, 1)
            compare(golfer.turns, 2)
            // The recording opens at ARMING: nothing is open through the demo or the arming wait.
            compare(seen, ["turnAway/demo:", "turnAway/arming:", "turnAway/armed:turnAway",
                           "turnAway/turning:turnAway", "turnAway/returned:turnAway", "turnAway/passed:",
                           "turnThrough/demo:", "turnThrough/arming:", "turnThrough/armed:turnThrough",
                           "turnThrough/turning:turnThrough", "turnThrough/returned:turnThrough"])
            // stepStart came after the demonstration had finished: after the guide's last call.
            var start = cer.calls.filter(function(c) { return c.name === "stepStart" && c.args[0] === "turnAway" })[0]
            var back = guide.log.filter(function(e) { return e.name === "trunkAnimate" && e.chest === 0 })[0]
            verify(start.t >= back.t + Math.round(2500 * 0.05) - 5,
                   "stepStart at " + start.t + " before the demo's way back ended (" + back.t + ")")
            compare(turnLines("step=timeout").length, 0)
            compare(turnLines("step=returned").length, 2)
            // The live turn read the golfer: out to 90° and back near the armed heading.
            fuzzyCompare(r.turnPeakDeg, 90, 0.01)
            verify(Math.abs(r.turnDeg) < 15, "turnDeg " + r.turnDeg)
        }

        function test_TR14_aTurnThatNeverComesBackTimesOutAndIsChecked() {
            // × 0.05: a 600 ms timeout (longer than the golfer's ~0.35 s turn out and back), 200 ms retry.
            hostRoutine({ pace: { turnTimeout: 12000, retry: 4000 } })
            // Away: out to 90° and stays there, still. The check then reads too little (scripted).
            golfer.scripts = [[{ chest: 0, gyro: 0, ms: 20 }, { chest: 50, gyro: 80, ms: 40 }, { chest: 90, gyro: 0, ms: 0 }]]
            cer.turnScript = [{ valid: false, cause: "range",
                                failure: "Chest turned too little — turn further, as if to the top, and back" }]
            var t0 = 0, t1 = 0
            r.substageChanged.connect(function() {
                if (r.substage === "turning" && t0 === 0) t0 = Date.now()
                if (r.substage === "timeout") t1 = Date.now()
            })
            r.begin()
            waitFor(function() { return r.substage === "retry" }, 5000, "the turn never timed out")
            verify(t1 - t0 >= 570, "timed out after " + (t1 - t0) + " ms, not the 600 ms turnTimeout")
            var to = turnLines("stage=turnAway step=timeout")
            compare(to, ["stage=turnAway step=timeout peak=90.0deg turn=-90.0deg"])
            compare(cer.order().slice(-2), ["checkTurn:away", "stepEnd:turnAway"])
            compare(r.message, "Chest turned too little — turn further, as if to the top, and back")
            // It re-arms where the golfer stands (still), and the next turn passes.
            waitDone()
            compare(cer.count("checkTurn"), 3)
            compare(turnLines("step=returned").length, 2)
        }

        function test_TR15_aTurnThatNeverStartsWaitsSayingSo() {
            hostRoutine({ pace: { waitLog: 4000 } })     // × 0.05 = 200 ms between wait lines
            golfer.enabled = false
            r.begin()
            waitFor(function() { return r.substage === "armed" }, 5000, "never armed")
            compare(cer.currentStep, "turnAway")
            var t0 = Date.now()
            wait(700)
            compare(r.stage, "turnAway"); compare(r.substage, "armed")
            compare(r.waitReason, "Waiting for you to turn")
            compare(cer.count("checkTurn"), 0, "a turn that never started was checked")
            compare(r.message, "")
            var lines = waitLines().filter(function(l) { return l.indexOf("wait step=armed") >= 0 })
            var elapsed = Date.now() - t0
            verify(lines.length >= 2 && lines.length <= Math.ceil(elapsed / 200) + 2, lines.length + " lines in " + elapsed + " ms")
            verify(lines[0].indexOf("turn=0.0deg peak=0.0deg — Waiting for you to turn") >= 0, lines[0])
            // A sway under the start threshold changes nothing.
            tho.headingDeg = -12; tho.gyroDps = 15
            wait(200)
            compare(r.substage, "armed")
            fuzzyCompare(r.turnDeg, -12, 0.01)
            // A lost reading (a filter fault): no reading, not a turn — said so.
            tho.headingDeg = NaN
            waitFor(function() { return r.waitReason === "Chest sensor gives no turn reading — waiting for one" }, 2000,
                    "a lost reading was not said")
            compare(r.substage, "armed")
            verify(isNaN(r.turnDeg))
            compare(cer.count("checkTurn"), 0)
            // The golfer turns at last: the step runs to its end.
            tho.headingDeg = 0; tho.gyroDps = 0
            golfer.enabled = true
            golfer.turn()
            waitFor(function() { return r.stage === "turnThrough" }, 5000, "the late turn never finished the step")
            compare(cer.count("checkTurn"), 1)
        }

        function test_TR16_motionWhileArmingDoesNotArm() {
            hostRoutine({ pace: { waitLog: 400000 } })
            // The golfer is still moving when the demo ends.
            r.substageChanged.connect(function() {
                if (r.stage === "turnAway" && r.substage === "arming" && golfer.turns === 0) tho.gyroDps = 30
            })
            r.begin()
            waitFor(function() { return r.substage === "arming" }, 5000, "never reached arming")
            wait(400)                                    // 8 × the 50 ms arm, all moving
            compare(r.substage, "arming")
            compare(r.waitReason, "Chest sensor is moving")
            verify(cer.order().indexOf("stepStart:turnAway") < 0, "armed while moving: " + JSON.stringify(cer.order()))
            var wl = waitLines().filter(function(l) { return l.indexOf("wait step=arming") >= 0 })
            compare(wl.length, 1)
            verify(wl[0].indexOf("thorax gyro=30.0dps") >= 0 && wl[0].indexOf("— Chest sensor is moving") >= 0, wl[0])
            // Still, but no turn reading: still not armed.
            tho.gyroDps = 0; tho.headingDeg = NaN
            waitFor(function() { return r.waitReason === "Chest sensor gives no turn reading — waiting for one" }, 2000,
                    "a missing reading was not said")
            wait(200)
            compare(r.substage, "arming")
            // Still and read: it arms, and the golfer's turn ends the step.
            tho.headingDeg = 0
            waitFor(function() { return r.substage === "armed" || r.substage === "turning" || r.stage !== "turnAway" },
                    2000, "never armed once still")
            compare(cer.order().filter(function(o) { return o === "stepStart:turnAway" }).length, 1)
            waitDone()
        }

        // ═════ The pages (TP) ═════════════════════════════════════════════════
        function trunkSetup(withWg3) {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            if (withWg3) d.addWg3("HM-1", true)
            d.addWitmotion("WT-P", "P", true)
            d.addWitmotion("WT-T", "T", true)
            d.trunkPace = paced()
        }
        function trunkPage() {
            var p = d.flow.page
            return p !== null && d.current() === "calibrateTrunk" ? p : null
        }
        function waitPageDone() {
            tc.tryVerify(function() { var p = trunkPage(); return p !== null && p.routine.done }, 40000,
                         "the trunk routine on the page never completed")
            d.settle()
        }
        function rowNamed(rows, label) { return rows.filter(function(x) { return x.label === label })[0] }

        function test_TP1_trunkOnlySession() {
            trunkSetup(false)
            d.open(wrist)
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "imus", "calibrateTrunk", "checkTrunk", "ready"])
            d.walkTo("calibrateTrunk")
            compare(d.stepLabel(), "STEP 6 OF 8 · CALIBRATE TRUNK")
            verify(!d.headerForwardEnabled(), "Continue open before the trunk is calibrated")
            verify(d.canSkip())
            waitPageDone()
            verify(d.draft.outcome("trunk").done)
            compare(d.primaryLabel(), "Continue →")
            compare(d.hint(), "Trunk sensors calibrated")
            compare(d.ceremony.order(), passOrder)
            d.next()
            compare(d.current(), "checkTrunk")
            // Check: the one line of numbers, zeroed where the page opened; turning away reads +.
            var line = function() {
                var t = d.find(d.flow.page, function(o) { return o.objectName === "trunkReadings" })
                return t ? t.text : ""
            }
            compare(line(), "hips 0° · chest 0° · X 0°")
            var yaw = function(deg) { var h = deg * Math.PI / 360; return Qt.quaternion(Math.cos(h), 0, 0, Math.sin(h)) }
            d.imu.device("WT-P").anatQuat = yaw(-45)        // a right-hander turns away about −Z
            d.imu.device("WT-T").anatQuat = yaw(-88)
            compare(line(), "hips 45° · chest 88° · X 43°")
            verify(d.find(d.flow.page, function(o) { return o.objectName === "trunkLiveFigureSlot" }) !== null)
            // The live figure turned with them (W2.3): a right-hander's turn away turns the avatar's hips
            // −45° about scene +Y — toward its own right — and its chest −88° (viz_frame_test pins the frames).
            var fig = d.find(d.flow.page, function(o) { return o.objectName === "trunkLiveFigure" })
            verify(fig !== null, "no live trunk figure on Check")
            var yawOf = function(glb) {
                var n = harness.segmentNode(fig, glb)
                verify(n.found, glb + " not found")
                var q = n.sceneRotation
                verify(Math.abs(q.x) < 0.03 && Math.abs(q.z) < 0.03, glb + " is not a turn about the vertical: "
                       + [q.scalar, q.x, q.y, q.z].join(", "))
                return 2 * Math.atan2(q.y, q.scalar) * 180 / Math.PI
            }
            fuzzyCompare(yawOf("body_Hips.glb"), -45, 0.5)
            fuzzyCompare(yawOf("body_Spine2.glb"), -88, 1.0)
            d.next()
            compare(d.current(), "ready")
            var row = rowNamed(d.summaryRows(), "Trunk calibration")
            verify(row !== undefined, JSON.stringify(d.summaryRows()))
            compare(row.value, "Hip and chest sensors calibrated")
            verify(row.good)
            verify(d.readinessIssues().every(function(x) { return x.text.indexOf("Trunk") < 0 }), JSON.stringify(d.readinessIssues()))
            // Start keeps the ceremony for the session (the recorder writes it then).
            d.start()
            compare(d.ceremony.count("discard"), 0)
        }

        function test_TP2_backFromCheckRestoresWithoutARerun() {
            trunkSetup(false)
            d.open(wrist)
            d.walkTo("calibrateTrunk")
            waitPageDone()
            d.next()
            compare(d.current(), "checkTrunk")
            var nCer = d.ceremony.calls.length
            var nPel = d.imu.device("WT-P").calls.length
            d.back()
            compare(d.current(), "calibrateTrunk")
            var p = trunkPage()
            compare(p.routine.stage, "done")
            compare(d.primaryLabel(), "Continue →")
            wait(500)
            compare(d.ceremony.calls.length, nCer, "re-entry touched the recorder")
            compare(d.imu.device("WT-P").calls.length, nPel, "re-entry re-ran the solve or cleared it")
            // Recalibrate from Check: the outcome goes, the page begins a fresh run.
            d.next()
            var link = d.find(d.flow.page, function(o) { return o.objectName === "trunkCheckRecalibrate" })
            mouseClick(link)
            d.settle()
            compare(d.current(), "calibrateTrunk")
            compare(d.state("calibrateTrunk"), "pending")
            verify(!d.draft.outcome("trunk").done)
            waitPageDone()
            compare(d.ceremony.order().filter(function(o) { return o === "begin" }).length, 2)
        }

        function test_TP3_leaveMidStepNothingFiresAfter() {
            trunkSetup(false)
            d.open(wrist)
            // Stand tall never starts: the golfer keeps moving (it waits at ready).
            d.imu.device("WT-P").gyroDps = 40
            d.walkTo("calibrateTrunk")
            tc.tryVerify(function() { var p = trunkPage(); return p !== null && p.routine.substage === "ready" }, 40000,
                         "never reached ready")
            var p = trunkPage(), mark = null
            p.routine.activeChanged.connect(function() {
                if (!p.routine.active)
                    mark = { cer: d.ceremony.calls.length, pel: d.imu.device("WT-P").calls.length,
                             tho: d.imu.device("WT-T").calls.length }
            })
            d.back()
            compare(d.current(), "imus")
            verify(mark !== null, "the routine never went inactive")
            compare(d.ceremony.order().slice(-1), ["stepEnd:standTall"])
            d.imu.device("WT-P").gyroDps = 0
            wait(600)
            compare(d.ceremony.calls.length, mark.cer, "recorder called after leave")
            compare(d.imu.device("WT-P").calls.length, mark.pel)
            compare(d.imu.device("WT-T").calls.length, mark.tho)
            verify(!d.draft.outcome("trunk").done)
            // Back again: a fresh run in the SAME recording (it was never ended).
            d.next()
            compare(d.current(), "calibrateTrunk")
            waitPageDone()
            compare(d.ceremony.count("begin"), 1)
        }

        function test_TP4_trunkWithWg3_recordingBeginsAtTheArmStep() {
            trunkSetup(true)
            d.open(wrist)
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "imus", "calibrateArm", "checkArm",
                               "calibrateTrunk", "checkTrunk", "ready"])
            d.walkTo("calibrateArm")
            compare(d.ceremony.order(), ["begin", "stepStart:armRoutine"])
            verify(d.ceremony.recording)
            d.skip()                                         // the arm routine left: its span closes
            compare(d.current(), "checkArm")
            compare(d.ceremony.order(), ["begin", "stepStart:armRoutine", "stepEnd:armRoutine"])
            d.walkTo("calibrateTrunk")
            waitPageDone()
            compare(d.ceremony.order(), ["begin", "stepStart:armRoutine", "stepEnd:armRoutine"]
                                        .concat(passOrder.slice(1)))
        }

        function test_TP5_leavingSetupDiscardsTheCeremony() {
            trunkSetup(false)
            d.open(wrist)
            d.walkTo("calibrateTrunk")
            tc.tryVerify(function() { return d.ceremony.recording }, 5000)
            d.cancel()
            compare(d.ceremony.order().slice(-1), ["discard"])
            verify(!d.ceremony.recording && !d.ceremony.pendingWrite)
            // A visit left some other way (hidden, never cancelled) holding a ceremony: the next
            // open() drops it before anything records into it.
            d.ceremony.begin()
            var n = d.ceremony.calls.length
            d.open(wrist)
            compare(d.ceremony.order().slice(n), ["discard"])
        }

        function test_TP6_failBackNextThenRecalibrateMidHold() {
            trunkSetup(false)
            d.trunkPace = paced({ retry: 400000 })        // the failure stays on screen: Back from retry
            d.open(wrist)
            var P = d.imu.device("WT-P"), T = d.imu.device("WT-T")
            P.trunkScript = [{ valid: false, faceOk: false, longAxisOk: false,
                               failure: "Hip sensor is lying flat — wear it upright: USB down, label facing out" }]
            d.walkTo("calibrateTrunk")
            tc.tryVerify(function() { var p = trunkPage(); return p !== null && p.routine.substage === "retry" }, 40000,
                         "the stand tall never failed")
            var msg = d.find(d.flow.page, function(o) { return o.objectName === "trunkMessage" })
            compare(msg.text, "Hip sensor is lying flat — wear it upright: USB down, label facing out")
            // Back to Sensors, Next to Calibrate trunk (the sensors now worn: upright, still).
            d.back()
            compare(d.current(), "imus")
            d.next()
            compare(d.current(), "calibrateTrunk")
            waitPageDone()
            verify(d.draft.outcome("trunk").done)
            compare(P.countCalls("calibrateTrunk"), 2)
            compare(d.ceremony.order(), ["begin",
                     "stepStart:standTall", "attachResult:standTall:pelvis", "attachResult:standTall:thorax", "stepEnd:standTall",
                     "stepStart:standTall", "attachResult:standTall:pelvis", "attachResult:standTall:thorax", "stepEnd:standTall",
                     "stepStart:turnAway", "checkTurn:away", "stepEnd:turnAway",
                     "stepStart:turnThrough", "checkTurn:through", "stepEnd:turnThrough", "end"])

            // Recalibrate mid-hold: the hip sensor moves the instant the hold starts (so the bar
            // cannot fill first), the page names it, the golfer presses Recalibrate.
            var p = trunkPage()
            var nCer = d.ceremony.calls.length
            var armed = true
            p.routine.substageChanged.connect(function() {
                if (armed && p.routine.substage === "hold") { armed = false; P.gyroDps = 20 }
            })
            mouseClick(d.find(p, function(o) { return o.objectName === "trunkRecalibrate" }))
            verify(!d.draft.outcome("trunk").done, "Recalibrate kept the outcome")
            tc.tryVerify(function() { return p.routine.substage === "hold" && !armed && p.routine.waitReason !== "" }, 40000,
                         "never held with the hip sensor moving")
            var why = d.find(p, function(o) { return o.objectName === "trunkWaitReason" })
            compare(why.text, "Hip sensor is moving")
            verify(why.visible)
            compare(p.routine.holdProgress, 0)
            mouseClick(d.find(p, function(o) { return o.objectName === "trunkRecalibrate" }))
            P.gyroDps = 0
            waitPageDone()
            verify(d.draft.outcome("trunk").done)
            compare(P.countCalls("calibrateTrunk"), 3)
            // The run Recalibrate interrupted closed its open step before the new one began.
            compare(d.ceremony.order().slice(nCer), ["begin",
                     "stepStart:standTall", "stepEnd:standTall",
                     "stepStart:standTall", "attachResult:standTall:pelvis", "attachResult:standTall:thorax", "stepEnd:standTall",
                     "stepStart:turnAway", "checkTurn:away", "stepEnd:turnAway",
                     "stepStart:turnThrough", "checkTurn:through", "stepEnd:turnThrough", "end"])
        }

        function test_TP7_leaveMidTurnThenNext() {
            trunkSetup(false)
            d.trunkPace = paced({ turnTimeout: 40000000 })
            // The golfer turns away and stays there: the step is mid-turn when he presses Back.
            d.golfer.scripts = [[{ chest: 0, gyro: 0, ms: 20 }, { chest: 50, gyro: 80, ms: 40 }, { chest: 90, gyro: 0, ms: 0 }]]
            d.open(wrist)
            d.walkTo("calibrateTrunk")
            tc.tryVerify(function() { var p = trunkPage(); return p !== null && p.routine.substage === "turning" }, 40000,
                         "never turning")
            var status = d.find(d.flow.page, function(o) { return o.objectName === "trunkStatus" })
            compare(status.text, "NOW COME BACK TO FACE THE SCREEN")
            d.back()
            compare(d.current(), "imus")
            compare(d.ceremony.order().slice(-1), ["stepEnd:turnAway"])
            compare(d.ceremony.currentStep, "")
            verify(!d.draft.outcome("trunk").done)
            // Next: a fresh run in the same recording, both turns made by the (default) golfer.
            var n = d.ceremony.calls.length
            d.next()
            compare(d.current(), "calibrateTrunk")
            waitPageDone()
            compare(d.ceremony.count("begin"), 1)
            compare(d.ceremony.order().slice(n), passOrder.slice(1))
        }
    }
}
