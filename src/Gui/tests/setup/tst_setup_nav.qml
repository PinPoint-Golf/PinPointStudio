// Stage 0 part B1 — navigation and flow (catalogue group N,
// docs/design/session_wizard_refactor_design.md §7.3).
//
// Every case asserts the catalogue's EXPECTED column (the behaviour after the refactor). The
// cases the catalogue marked X against the retired wizard carried an expectFail() until Stage 5c,
// when that wizard was deleted; session setup passes them all for real.
//
// All access to session setup goes through support/SetupDriver.qml.
import QtQuick
import QtTest
import PinPointStudio
import "support"

// ⚠ THE ROOT IS A PLAIN Item, NOT THE TestCase (see tst_setup_smoke.qml): TestCase is
// `visible: false`, and a wizard parented under it is never visible.
Item {
    id: root
    width: 1400
    height: 900

    readonly property var _testLog: testLog

    Item { id: host; anchors.fill: parent }
    Component { id: descComp; StepDescriptor {} }
    SetupDriver { id: drv; host: host; testCase: tc; testLog: root._testLog }

    TestCase {
        id: tc
        name: "SetupNav"
        when: windowShown

        // The adapter (support/SetupDriver.qml).
        readonly property var d: drv

        readonly property int wrist: SessionController.Wrist

        function init() {
            testLog.reset()
            // Offscreen-QPA font notice, not a QML warning — see tst_setup_smoke.qml.
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            d.setUp()
        }
        function cleanup() {
            d.tearDown()
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        function eyebrowNumber(label) {
            var m = /^STEP (\d*) OF (\d+)/.exec(label)
            return m ? [m[1] === "" ? -1 : parseInt(m[1]), parseInt(m[2])] : null
        }

        // ── N1 ───────────────────────────────────────────────────────────────
        function test_N01_wristOneFaceOnWitmotionPlan() {
            d.addCamera("FO1", CameraInstance.FaceOn, false)
            d.addWitmotion("WT-A", "A", false)
            d.addWitmotion("WT-B", "B", false)
            d.open(wrist)
            compare(d.plan(), ["goals", "cameras", "ball", "imus", "calibrateArm", "checkArm", "ready"])
            compare(d.current(), "goals")
            var p = d.plan()
            for (var i = 0; i < p.length; ++i) compare(d.state(p[i]), "pending", p[i])
            compare(d.stepLabel(), "STEP 1 OF 7 · GOALS")
        }

        // ── N2 ───────────────────────────────────────────────────────────────
        function test_N02_dtlInsertsTriangulate() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCamera("DTL1", CameraInstance.DownTheLine, false)
            // An arm sensor pair: the new flow takes the arm steps from the hardware (§4.12),
            // the retired wizard took them from the session type — with these the plans agreed.
            d.addWitmotion("WT-A", "A", false)
            d.addWitmotion("WT-B", "B", false)
            d.open(wrist)
            d.next()
            compare(d.current(), "cameras")
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "imus", "calibrateArm", "checkArm", "ready"])

            d.cams.setCameraSelected("DTL1", true)          // selected from the toolbar, or Connect
            compare(d.plan(), ["goals", "cameras", "framing", "triangulate", "ball", "imus", "calibrateArm", "checkArm", "ready"])
            compare(d.stepLabel(), "STEP 2 OF 9 · CAMERAS")
            // Every page's eyebrow and every pip agree on 1..9.
            var want = { "GOALS": 1, "CAMERAS": 2, "FRAMING": 3, "TRIANGULATION": 4, "BALL DETECTION": 5,
                         "MOTION SENSORS": 6, "CALIBRATE": 7, "CONFIRM TRACKING": 8, "READY": 9 }
            var labels = d.allStepLabels()
            compare(labels.length, 9, JSON.stringify(labels))
            for (var i = 0; i < labels.length; ++i) {
                var name = labels[i].split(" · ")[1]
                compare(eyebrowNumber(labels[i]), [want[name], 9], labels[i])
            }
            var plan = d.plan()
            for (var j = 0; j < plan.length; ++j) {
                var pp = d.pip(plan[j])
                verify(pp.shown, plan[j] + " pip hidden")
                if (pp.state !== "done") compare(pp.glyph, String(j + 1), plan[j] + " pip")
            }

            d.cams.setCameraSelected("DTL1", false)
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "imus", "calibrateArm", "checkArm", "ready"])
            compare(d.stepLabel(), "STEP 2 OF 8 · CAMERAS")
            verify(!d.pip("triangulate").shown, "Triangulate pip still shown")
            compare(d.pip("ball").glyph, "4")
        }

        // ── N3 ───────────────────────────────────────────────────────────────
        function test_N03_continueThroughUngatedSteps() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCameraInstance("FO1", { ballPresent: true })
            d.addWitmotion("WT-A", "A", true)
            d.addWitmotion("WT-B", "B", true)
            d.open(wrist)
            var route = [["goals", "next", "cameras"], ["cameras", "next", "framing"], ["framing", "next", "ball"],
                         ["ball", "next", "imus"],
                         ["imus", "next", "calibrateArm"], ["calibrateArm", "skip", "checkArm"],
                         ["checkArm", "next", "ready"]]
            for (var i = 0; i < route.length; ++i) {
                var r = route[i]
                compare(d.current(), r[0])
                if (r[1] === "next") d.next(); else d.skip()
                compare(d.state(r[0]), r[1] === "next" ? "done" : "skipped", r[0])
                compare(d.current(), r[2], "after " + r[1] + " on " + r[0])
            }
        }

        // ── N4 ───────────────────────────────────────────────────────────────
        function test_N04_backFromEachStep() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCameraInstance("FO1", { ballPresent: true })
            d.addWitmotion("WT-A", "A", true)
            d.addWitmotion("WT-B", "B", true)
            d.open(wrist)
            d.walkTo("ready")
            var plan = d.plan()
            for (var i = plan.length - 1; i > 0; --i) {
                compare(d.current(), plan[i])
                d.back()
                compare(d.state(plan[i]), "pending", plan[i] + " after Back")
                compare(d.current(), plan[i - 1], "Back from " + plan[i])
            }
            // Back on the first step: the footer has none; the header ‹ exits (Main.qml:641–643).
            compare(d.current(), "goals")
            verify(!d.canBack(), "footer Back shown on the first step")
            var stops = d.cams.countCalls("stopCapture"), cdis = d.cams.countCalls("disconnectAll")
            var idis = d.imu.countCalls("disconnectAll")
            verify(d.headerBack())
            compare(d.cams.countCalls("stopCapture"), stops + 1)
            compare(d.cams.countCalls("disconnectAll"), cdis + 1)
            compare(d.imu.countCalls("disconnectAll"), idis + 1)
            compare(d.nav.calls[d.nav.calls.length - 1].name, "back")
            verify(!d.wizard.visible, "wizard still showing after ‹ on the first step")
        }

        // ── N5 ───────────────────────────────────────────────────────────────
        function test_N05_skipMarksSkippedAndReadyNamesIt() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCamera("DTL1", CameraInstance.DownTheLine, true)
            d.addCamera("X1", CameraInstance.Other, false)      // keeps Cameras' Skip on offer
            d.addWitmotion("WT-A", "A", false)
            d.addWitmotion("WT-B", "B", false)
            d.open(wrist)
            d.next()
            var skips = ["cameras", "triangulate", "ball", "imus", "calibrateArm"]
            for (var i = 0; i < skips.length; ++i) {
                // Framing offers no Skip (it never gates Continue): passed with Continue.
                if (d.current() === "framing") { verify(!d.canSkip(), "Skip on Framing"); d.next() }
                compare(d.current(), skips[i])
                verify(d.canSkip(), "no Skip on " + skips[i])
                d.skip()
                compare(d.state(skips[i]), "skipped", skips[i])
                compare(d.pip(skips[i]).glyph, "⚠", skips[i] + " pip")
            }
            d.next()
            compare(d.current(), "ready")
            var texts = d.readinessIssues().map(function(x) { return x.text })
            var rows = {}
            d.summaryRows().forEach(function(r) { rows[r.label] = r })
            verify(texts.indexOf("Cameras skipped — no video will be captured this session") >= 0, JSON.stringify(texts))
            verify(texts.indexOf("Motion sensors skipped — no movement data will be captured") >= 0, JSON.stringify(texts))
            compare(rows["Cameras"].value, "Skipped — no video capture")
            compare(rows["Triangulation"].value, "Not confirmed")
            compare(rows["Triangulation"].good, false)
            compare(rows["Ball detection"].value, "Skipped")
            compare(rows["Ball detection"].good, false)
            compare(rows["Sensors"].value, "Skipped — no motion data")

            // Calibrate's consequence shows only when the IMUs were not skipped.
            d.imu.connectDevice("WT-A")
            d.imu.connectDevice("WT-B")
            d.open(wrist)
            d.walkTo("calibrateArm")
            d.skip()
            compare(d.state("calibrateArm"), "skipped")
            d.next()
            compare(d.current(), "ready")
            texts = d.readinessIssues().map(function(x) { return x.text })
            verify(texts.indexOf("Sensor position calibration not completed — return to the Calibrate step") >= 0,
                   JSON.stringify(texts))
        }

        // ── N6 ───────────────────────────────────────────────────────────────
        function test_N06_ballGate() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            var inst = d.addCameraInstance("FO1", { ballPresent: false })
            d.open(wrist)
            d.walkTo("ball")
            compare(d.current(), "ball")
            d.next()
            compare(d.current(), "ball", "Continue advanced without a ball")
            compare(d.state("ball"), "pending")
            verify(d.canSkip())
            inst.ballPresent = true
            compare(d.hint(), "Ball detected")
            d.next()
            compare(d.current(), "imus")
            compare(d.state("ball"), "done")

            d.back()
            inst.ballPresent = false
            compare(d.current(), "ball")
            d.skip()
            compare(d.current(), "imus")
            compare(d.state("ball"), "skipped")
        }

        // ── N7 ───────────────────────────────────────────────────────────────
        function test_N07_calibrateGate() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addWg3("HM-1", true)
            d.open(wrist)
            d.walkTo("calibrateArm")
            // (The guide view exists only on Calibrate, so it loads there.)
            tryVerify(function() { return d.guideReady() }, 30000, "3-D guide never loaded")
            verify(!d.calibrationDone())
            d.next()
            compare(d.current(), "calibrateArm", "Continue advanced an unfinished calibration")
            verify(d.canSkip())
            d.skip()
            compare(d.current(), "checkArm")
            compare(d.state("calibrateArm"), "skipped")
            d.back()
            compare(d.current(), "calibrateArm")
            tryVerify(function() { return d.calibrationDone() }, 30000, "HackMotion routine never completed")
            d.next()
            compare(d.current(), "checkArm")
            compare(d.state("calibrateArm"), "done")
        }

        // ── N8 ───────────────────────────────────────────────────────────────
        function test_N08_headerForwardDoesNotSkipConnect() {
            d.addCamera("FO1", CameraInstance.FaceOn, false)
            d.open(wrist)
            d.next()
            compare(d.current(), "cameras")
            compare(d.primaryLabel(), "Connect")
            d.headerForward()                // a no-op if disabled; otherwise must not advance
            compare(d.state("cameras") + "@" + d.current(), "pending@cameras")
        }

        // ── N9 ───────────────────────────────────────────────────────────────
        function test_N09_cancelAtEachStep() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addWitmotion("WT-A", "A", false)
            d.addWitmotion("WT-B", "B", false)
            d.open(wrist)
            var plan = d.plan()
            for (var i = 0; i < plan.length; ++i) {
                var key = plan[i]
                d.cams.setCameraSelected("FO1", true)
                d.session.activeClub = "7i"
                d.open(wrist)
                d.walkTo(key)
                compare(d.current(), key)
                var queueStarted = false
                if (key === "imus") {
                    compare(d.primaryLabel(), "Connect")
                    d.clickPrimary()                 // paced queue: A now, B in 2 s
                    verify(d.imuQueueActive(), "queue did not start")
                    queueStarted = true
                }
                var s0 = d.cams.countCalls("stopCapture"), c0 = d.cams.countCalls("disconnectAll")
                var i0 = d.imu.countCalls("disconnectAll"), sel0 = d.imu.countCalls("setSelected")
                d.cancel()
                compare(d.cams.countCalls("stopCapture"), s0 + 1, key + ": stopCapture")
                compare(d.cams.countCalls("disconnectAll"), c0 + 1, key + ": cameras disconnectAll")
                compare(d.imu.countCalls("disconnectAll"), i0 + 1, key + ": IMUs disconnectAll")
                verify(!d.imuQueueActive(), key + ": queue still active")
                compare(d.session.activeClub, "", key + ": activeClub")
                var last = d.nav.calls[d.nav.calls.length - 1]
                compare([last.name, last.args[0]], ["navigate", d.screenHome], key + ": navigation")
                verify(!d.wizard.visible, key + ": wizard still showing")
                if (queueStarted) {
                    wait(2600)                       // past the queue's next 2 s tick
                    compare(d.imu.countCalls("setSelected"), sel0, "the queue kept connecting after Cancel")
                }
            }
        }

        // ── N10 ──────────────────────────────────────────────────────────────
        function test_N10_startSendsGoals() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.open(wrist)
            d.walkTo("ready")
            d.start()
            var ev = d.emitted.filter(function(e) { return e.name === "sessionStartRequested" })
            compare(ev.length, 1)
            compare(ev[0].args, [wrist, ["generalAssessment"]])
            compare(d.session.calls.map(function(c) { return c.name + ":" + c.args[0] }), ["start:" + wrist])
            compare(d.cams.countCalls("startCapture"), 1)
            verify(!d.wizard.visible)

            appSettings.sessionGoalsByType = ({})
            d.open(wrist)
            d.toggleGoal("impactConditions")
            d.toggleGoal("trailWristExtension")
            compare(d.selectedGoals(), ["impactConditions", "trailWristExtension"])
            d.walkTo("ready")
            d.start()
            ev = d.emitted.filter(function(e) { return e.name === "sessionStartRequested" })
            compare(ev.length, 2)
            compare(ev[1].args, [wrist, ["impactConditions", "trailWristExtension"]])
        }

        // ── N11 ──────────────────────────────────────────────────────────────
        function test_N11_goalsSurviveSettingsTrip() {
            d.open(wrist)
            d.toggleGoal("impactConditions")
            d.toggleGoal("wristAngleTop")
            var before = d.selectedGoals()
            compare(before, ["wristAngleTop", "impactConditions"])
            d.suspend()
            d.resume()
            compare(d.selectedGoals(), before)
        }

        // ── N12 ──────────────────────────────────────────────────────────────
        function test_N12_enablementSurvivesSettingsTrip() {
            d.addCamera("FO1", CameraInstance.FaceOn, false)
            d.addCamera("FO2", CameraInstance.FaceOn, false)
            d.addWitmotion("WT-A", "A", false)
            d.addWitmotion("WT-B", "B", false)
            d.open(wrist)
            d.next()
            d.toggleCameraRow("FO2")
            compare(d.cameraEnabled("FO2"), false)
            d.walkTo("imus")
            d.toggleImuRow("B")
            compare(d.imuEnabled("WT-B"), false)
            d.suspend()
            d.resume()
            compare(d.current(), "imus")
            compare([d.cameraEnabled("FO2"), d.imuEnabled("WT-B")], [false, false])
        }

        // ── N13 ──────────────────────────────────────────────────────────────
        function test_N13_openResetsEverything() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCameraInstance("FO1", { ballPresent: false })
            d.addWitmotion("WT-A", "A", false)
            d.addWitmotion("WT-B", "B", false)
            appSettings.sessionGoalsByType = ({ "1": ["wristAngleTop"] })
            d.open(wrist)
            compare(d.selectedGoals(), ["wristAngleTop"])
            d.toggleGoal("impactConditions")
            d.walkTo("ball")
            d.clickLearn()
            verify(d.ballLearning())
            d.skip()
            compare(d.current(), "imus")
            d.clickPrimary()                         // Connect: the paced queue starts
            verify(d.imuQueueActive())
            // Leave mid-visit (rail → Settings), then a new session from Home.
            d.suspend()
            d.open(wrist)
            var sel0 = d.imu.countCalls("setSelected")
            var residue = {
                current: d.current(),
                states: d.states(),
                goals: d.selectedGoals(),
                queueActive: d.imuQueueActive(),
                ballLearning: d.ballLearning(),
                recalibrateFlag: d.recalibrateFlagSet(),
                calibrationDone: d.calibrationDone()
            }
            wait(2600)
            residue.queueConnectedAfterOpen = d.imu.countCalls("setSelected") - sel0
            var want = {
                current: "goals",
                states: { goals: "pending", cameras: "pending", framing: "pending", ball: "pending", imus: "pending",
                          calibrateArm: "pending", checkArm: "pending", ready: "pending" },
                goals: ["wristAngleTop"],
                queueActive: false,
                ballLearning: false,
                recalibrateFlag: false,
                calibrationDone: false,
                queueConnectedAfterOpen: 0
            }
            console.info("[N13] residue after open(): " + JSON.stringify(residue))
            // Today: states, current step and goals DO reset (goals to the saved set);
            // the paced queue and the Ball "Learning…" flag do not.
            compare(residue, want)
        }

        // ── N14 ──────────────────────────────────────────────────────────────
        function test_N14_deselectDtlWhileOnTriangulate() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCamera("DTL1", CameraInstance.DownTheLine, true)
            d.addWitmotion("WT-A", "A", false)               // arm steps in both shells (see N2)
            d.addWitmotion("WT-B", "B", false)
            d.open(wrist)
            d.next()
            d.next()
            d.next()
            compare(d.current(), "triangulate")
            d.cams.setCameraSelected("DTL1", false)
            compare(d.current(), "triangulate", "Triangulate no longer current")
            // The new flow PINS the current step in the plan until it is left (§4.5), so the step
            // keeps its number (the retired wizard dropped it at once: "STEP  OF 7").
            compare(d.plan(), ["goals", "cameras", "framing", "triangulate", "ball", "imus", "calibrateArm", "checkArm", "ready"])
            var hint = d.hint()
            var label = d.stepLabel()
            console.info("[N14] after deselecting DTL on Triangulate: eyebrow '" + label + "', hint '" + hint
                         + "', primary '" + d.primaryLabel() + "'")
            compare(label, "STEP 4 OF 9 · TRIANGULATION")
            compare(d.primaryLabel(), "Continue →")
            d.next()
            compare(d.current(), "ball")
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "imus", "calibrateArm", "checkArm", "ready"],
                    "Triangulate gone from the plan once left")
            compare(d.stepLabel(), "STEP 4 OF 8 · BALL DETECTION")
            verify(/no longer needed/i.test(hint), hint)
        }

        // ── N15 ──────────────────────────────────────────────────────────────
        function test_N15_recalibrateLink() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            var a = d.addWitmotion("WT-A", "A", true)
            d.addWitmotion("WT-B", "B", true)
            d.open(wrist)
            d.walkTo("calibrateArm")
            d.skip()
            compare(d.current(), "checkArm")
            var clr0 = a.countCalls("clearFunctionalCalibration")
            d.clickRecalibrate()
            compare(d.current(), "calibrateArm")
            compare(d.state("calibrateArm"), "pending")
            compare(d.state("checkArm"), "pending")
            compare(a.countCalls("clearFunctionalCalibration"), clr0 + 1, "no fresh run (no reset of slot A)")
            verify(d.calibrationRunning(), "routine not started")
            verify(!d.calibrationDone())
        }

        // ── N16–N18: the post-refactor API ───────────────────────────────────
        function test_N16_indicatorJumpToDoneStep() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCameraInstance("FO1", { ballPresent: true })
            d.open(wrist)
            d.next()                                     // Goals → Cameras
            d.next()                                     // Cameras → Framing
            d.next()                                     // Framing → Ball
            d.next()                                     // Ball → IMUs
            compare(d.current(), "imus")
            // A future pip is inert.
            var ready = d.pip("ready")
            compare(ready.state, "future")
            verify(!ready.clickable, "a future pip is clickable")
            d.clickPip("ready")
            compare(d.current(), "imus", "a future pip navigated")
            // The current pip is inert too.
            d.clickPip("imus")
            compare(d.current(), "imus")
            // A done pip jumps back, with leave("jump"); the states in between are kept.
            verify(d.pip("cameras").clickable, "a done pip is not clickable")
            var n0 = d.appLogFake.matching("op=goTo", "Setup").length
            d.clickPip("cameras")
            compare(d.current(), "cameras")
            compare(d.stepLabel(), "STEP 2 OF 6 · CAMERAS")
            compare(d.state("cameras"), "done")
            compare(d.state("ball"), "done", "a step in between lost its state")
            var lines = d.appLogFake.matching("op=goTo", "Setup").slice(n0)
            compare(lines.length, 1, lines.join("\n"))
            verify(lines[0].indexOf("op=goTo from=imus to=cameras reason=jump") >= 0, lines[0])
            compare(d.pip("cameras").state, "current")
            compare(d.pip("ball").glyph, "✓")
        }
        function test_N17_registerDummyStep() {
            // A step registered from OUTSIDE (SetupSteps.extensions) with its own page file:
            // nothing in the shell, the footer, the indicator or Ready is edited for it.
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCameraInstance("FO1", { ballPresent: true })
            d.open(wrist)
            var desc = createTemporaryObject(descComp, tc, {
                key: "dummyStep", title: "Dummy", eyebrow: "DUMMY", group: "cameras", after: "ball",
                page: Qt.resolvedUrl("support/pages/ExtraPage.qml"),
                applies: function(c, dr) { return c.faceOn.length > 0 },
                issues:  function(c, dr) { return [{ text: "Dummy needs attention", panel: -1 }] },
                summary: function(c, dr) { return { label: "Dummy", value: "ok", good: true } }
            })
            d.wizard.steps.extensions = [desc]
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "dummyStep", "imus", "ready"])
            // The indicator and the numbering.
            var pp = d.pip("dummyStep")
            verify(pp.shown, "no pip for the registered step")
            compare(pp.glyph, "5")
            compare(d.pip("imus").glyph, "6")
            compare(d.allStepLabels()[4], "STEP 5 OF 7 · DUMMY")
            // Navigation in and out, through the real footer.
            d.walkTo("dummyStep")
            compare(d.flow.page.objectName, "extraPage")
            compare(d.flow.page.stepLabel, "STEP 5 OF 7 · DUMMY")
            compare(d.pip("dummyStep").state, "current")
            d.next()
            compare(d.current(), "imus")
            compare(d.state("dummyStep"), "done")
            verify(d.pip("dummyStep").attention, "a visited step with issues has no attention ring")
            d.back()
            compare(d.current(), "dummyStep")
            d.back()
            compare(d.current(), "ball")
            // Ready's issues and summary.
            d.walkTo("ready")
            verify(d.readinessIssues().some(function(x) { return x.text === "Dummy needs attention" }),
                   JSON.stringify(d.readinessIssues()))
            verify(d.summaryRows().some(function(r) { return r.label === "Dummy" }))
            // Unregistered again: gone from the plan and the indicator, nothing else touched.
            d.wizard.steps.extensions = []
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "imus", "ready"])
            verify(!d.pip("dummyStep").shown, "the pip outlived its step")
        }
        function test_N18_trunkOnlyRolesDropArmSteps() {
            skip("covered on the engine by tst_setup_flow N18F; the trunk pages are not registered yet")
        }

        // ── N19 ──────────────────────────────────────────────────────────────
        function test_N19_doubleClickContinueAdvancesOnce() {
            d.open(wrist)
            compare(d.current(), "goals")
            compare(d.primaryLabel(), "Continue →")
            mouseDoubleClickSequence(d._primary())
            d.settle()                               // navigation is queued (R5)
            var after = d.current()
            console.info("[N19] double-click Continue on Goals → " + after + " (states "
                         + JSON.stringify(d.states()) + ")")
            compare(after, "cameras")
        }
    }
}
