// Stage 4 — the session-setup ENGINE on a dummy shell (docs/design/session_wizard_refactor_design.md
// §4.2–4.8, §8a stage 4): setup_flow.js, StepDescriptor, SetupSteps, SetupContext, SetupDraft,
// WizardPage and SetupFlow, over the fakes, with test pages (support/pages/) in place of the real
// ones (Stage 5).
//
// Case names carry the catalogue ID with an F suffix ("test_N01F_…") so they never collide with
// the wizard-level cases of the same ID in tst_setup_nav.qml, which keep driving today's wizard
// until Stage 5 rewrites SetupDriver.
//
// Navigation is QUEUED (R5): every case requests, then settles on `flow.busy`.
import QtQuick
import QtTest
import PinPointStudio
import "fakes"
import "support"
import "../../setup/setup_flow.js" as SF

// ⚠ THE ROOT IS A PLAIN Item, NOT THE TestCase (see tst_setup_smoke.qml): TestCase is
// `visible: false`, and a shell parented under it is never visible.
Item {
    id: root
    width: 1200
    height: 800

    Item { id: host; anchors.fill: parent }

    Component { id: imuComp;     FakeImuManager {} }
    Component { id: camComp;     FakeCameraManager {} }
    Component { id: athleteComp; FakeAthlete {} }
    Component { id: lmComp;      FakeLaunchMonitor {} }
    Component { id: logComp;     FakeAppLog {} }
    Component { id: journalComp; PageJournal {} }
    Component { id: descComp;    StepDescriptor {} }
    Component { id: catalogComp; MetricCatalog {} }
    Component { id: spyComp;     SignalSpy {} }

    TestCase {
        id: tc
        name: "SetupFlow"
        when: windowShown

        readonly property int wrist: SessionController.Wrist

        property var imu: null
        property var cams: null
        property var athlete: null
        property var lm: null
        property var log: null
        property var journal: null
        property var shell: null

        function init() {
            testLog.reset()
            // Offscreen-QPA font notice, not a QML warning — see tst_setup_smoke.qml.
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            // The REAL AppSettings (scratch QSettings) is shared by every case in this file.
            appSettings.imuRoles           = ({})
            appSettings.sessionGoalsByType = ({})
            appSettings.cameraExcluded     = []
            appSettings.imuExcluded        = []
            appSettings.cameraFixedInPlace = ({})
            imu     = imuComp.createObject(tc)
            cams    = camComp.createObject(tc)
            athlete = athleteComp.createObject(tc)
            lm      = lmComp.createObject(tc)
            log     = logComp.createObject(tc)
            journal = journalComp.createObject(tc)
            shell   = null
        }
        function cleanup() {
            if (shell) harness.destroyNow(shell)
            shell = null
            var objs = [imu, cams, athlete, lm, log, journal]
            for (var i = 0; i < objs.length; ++i) if (objs[i]) objs[i].destroy()
            imu = cams = athlete = lm = log = journal = null
            wait(0)
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        // ── Fixtures ─────────────────────────────────────────────────────────
        function makeShell(props) {
            shell = harness.createWithContext(Qt.resolvedUrl("support/FlowShell.qml"), {
                cameraManager:     cams,
                imuManager:        imu,
                athleteController: athlete,
                launchMonitor:     lm,
                appLog:            log,
                pageJournal:       journal
            }, host, props || {})
            verify(shell !== null, "FlowShell failed to instantiate")
            return shell
        }
        function flow()  { return shell.flow }
        function ctx()   { return shell.ctx }
        function draft() { return shell.draft }
        function settle() {
            tryVerify(function() { return !shell.flow.busy }, 3000, "the flow never settled")
        }
        function open(preset) {
            shell.flow.open(preset === undefined ? wrist : preset)
            settle()
        }
        function go(mark)  { shell.flow.next(mark || "done"); settle() }
        function back()    { shell.flow.back(); settle() }
        function spy(target, sig) { return createTemporaryObject(spyComp, tc, { target: target, signalName: sig }) }

        function cam(key, persp, selected) {
            cams.addCamera(key, persp, key)
            if (selected) cams.setCameraSelected(key, true)
        }
        function wit(id, slot, connected) {
            imu.addWitmotion(id, "")
            if (slot) imu.assign(id, slot)
            return connected ? imu.connectDevice(id) : null
        }
        function wg3(id, connected) {
            imu.addHackMotion(id, "")
            imu.assign(id, "A")
            return connected ? imu.connectDevice(id) : null
        }
        // A face-on camera, connected, with a live instance whose ball is present (or not).
        function faceOnWithBall(present) {
            cam("FO", CameraInstance.FaceOn, true)
            return cams.addInstance("FO", { ballPresent: present })
        }
        function setLines() { return log.matching("op=", "Setup") }

        // ═════ Pure functions (setup_flow.js) ═════════════════════════════════
        function _reg(spec) {
            // spec: [[key, appliesBool], …] → plain descriptor objects
            var out = []
            for (var i = 0; i < spec.length; ++i) {
                (function(k, a) {
                    out.push({ key: k, applies: function() { return a },
                               issues: function() { return k === "b" ? [{ text: "B wrong", panel: 4 }] : [] },
                               summary: function() { return k === "c" ? null : { label: k.toUpperCase(), value: "v", good: true } } })
                })(spec[i][0], spec[i][1])
            }
            return out
        }

        function test_SF01F_planOrderAndPinning() {
            var reg = _reg([["a", true], ["b", false], ["c", true], ["d", true]])
            compare(SF.plan(reg, null, null, ""), ["a", "c", "d"])
            // The pinned (current) step stays, in its REGISTRY place, though it no longer applies.
            compare(SF.plan(reg, null, null, "b"), ["a", "b", "c", "d"])
            compare(SF.plan([], null, null, ""), [])
        }
        function test_SF02F_nextBackNumbering() {
            var p = ["a", "c", "d"]
            compare(SF.next(p, "a"), "c")
            compare(SF.next(p, "d"), "")
            compare(SF.next(p, "zz"), "")
            compare(SF.back(p, "c"), "a")
            compare(SF.back(p, "a"), "")
            compare(SF.numbering(p), { a: 1, c: 2, d: 3 })
        }
        function test_SF03F_canGoToBackOnlyVisited() {
            var p = ["a", "b", "c", "d"]
            var st = { a: "done", b: "skipped", c: "pending" }
            verify(SF.canGoTo(p, st, "c", "a"))
            verify(SF.canGoTo(p, st, "c", "b"))
            verify(!SF.canGoTo(p, st, "c", "c"), "the current step")
            verify(!SF.canGoTo(p, st, "c", "d"), "a future step")
            verify(!SF.canGoTo(p, { a: "pending" }, "c", "a"), "never visited")
            verify(!SF.canGoTo(p, st, "c", "zz"))
        }
        function test_SF04F_progressIssuesSummaries() {
            var reg = _reg([["a", true], ["b", true], ["c", true]])
            var pr = SF.progress(["a", "b", "c"], { a: "done", b: "skipped" })
            compare(pr.done, 1); compare(pr.skipped, 1); compare(pr.passed, 2); compare(pr.total, 3)
            fuzzyCompare(pr.fraction, 2 / 3, 1e-9)
            compare(SF.issues(reg, ["a", "b", "c"], null, null), [{ key: "b", text: "B wrong", panel: 4 }])
            compare(SF.issues(reg, ["a", "c"], null, null), [], "a step outside the plan has no issues")
            var rows = SF.summaries(reg, ["a", "b", "c"], null, null)
            compare(rows.map(function(r) { return r.key }), ["a", "b"], "a null summary has no row")
        }

        // ═════ Navigation (N) ═════════════════════════════════════════════════
        function test_N01F_planForWrist() {
            cam("FO", CameraInstance.FaceOn, false)
            wit("WT-A", "A", false)
            wit("WT-B", "B", false)
            makeShell()
            open()
            compare(flow().plan, ["goals", "cameras", "ball", "imus", "calibrateArm", "checkArm", "ready"])
            compare(flow().current, "goals")
            for (var i = 0; i < flow().plan.length; ++i)
                compare(draft().state(flow().plan[i]), "pending", flow().plan[i])
            compare(flow().stepLabel, "STEP 1 OF 7 · GOALS")
            compare(flow().page.stepLabel, "STEP 1 OF 7 · GOALS")
            compare(flow().stepLabelFor("calibrateArm"), "STEP 5 OF 7 · CALIBRATE")
            compare(flow().stepLabelFor("checkArm"), "STEP 6 OF 7 · CONFIRM TRACKING")
            compare(flow().steps.map(function(s) { return s.number }), [1, 2, 3, 4, 5, 6, 7])
        }
        function test_N01F_noArmSensorNoArmSteps() {
            cam("FO", CameraInstance.FaceOn, false)
            makeShell()
            open()
            // §4.12: no arm sensor in the session → no arm Calibrate / Check, whatever the preset.
            compare(flow().plan, ["goals", "cameras", "ball", "imus", "ready"])
            compare(flow().stepLabelFor("ready"), "STEP 5 OF 5 · READY")
        }

        function test_N02F_dtlInsertsTriangulate() {
            cam("FO", CameraInstance.FaceOn, true)
            cam("DTL", CameraInstance.DownTheLine, false)
            makeShell()
            open()
            go()
            compare(flow().current, "cameras")
            compare(flow().plan, ["goals", "cameras", "ball", "imus", "ready"])
            cams.setCameraSelected("DTL", true)
            compare(flow().plan, ["goals", "cameras", "triangulate", "ball", "imus", "ready"])
            compare(flow().page.stepLabel, "STEP 2 OF 6 · CAMERAS")
            compare(flow().stepLabelFor("triangulate"), "STEP 3 OF 6 · TRIANGULATION")
            compare(flow().stepLabelFor("ready"), "STEP 6 OF 6 · READY")
            cams.setCameraSelected("DTL", false)
            compare(flow().plan, ["goals", "cameras", "ball", "imus", "ready"])
            compare(flow().page.stepLabel, "STEP 2 OF 5 · CAMERAS")
        }

        function test_N03F_continueThroughUngatedSteps() {
            faceOnWithBall(true)
            makeShell()
            open()
            var p = flow().plan
            compare(p, ["goals", "cameras", "ball", "imus", "ready"])
            for (var i = 0; i < p.length - 1; ++i) {
                compare(flow().current, p[i])
                go("done")
                compare(draft().state(p[i]), "done", p[i])
                compare(flow().current, p[i + 1])
                compare(journal.last("enter").arg, "forward")
                compare(journal.last("enter").key, p[i + 1])
            }
            go("done")                                       // nothing after Ready: refused
            compare(flow().current, "ready")
            compare(draft().state("ready"), "pending")
            compare(flow().progress.passed, 4)
        }

        function test_N04F_backFromEachStepAndExitFromFirst() {
            faceOnWithBall(true)
            makeShell()
            open()
            while (flow().current !== "ready") go("done")
            var p = flow().plan
            for (var i = p.length - 1; i > 0; --i) {
                compare(flow().current, p[i])
                back()
                compare(draft().state(p[i]), "pending", p[i] + " after back")
                compare(flow().current, p[i - 1])
                compare(journal.last("leave").arg, "back")
                compare(journal.last("enter").arg, "back")
            }
            var ex = spy(flow(), "exitRequested")
            var cancel = spy(flow(), "cancelled")
            var stop0 = cams.countCalls("stopCapture"), cd0 = cams.countCalls("disconnectAll")
            var q0 = imu.countCalls("cancelPacedConnect"), id0 = imu.countCalls("disconnectAll")
            back()                                           // ‹ on the first step
            compare(ex.count, 1)
            compare(ex.signalArguments[0][0], "back")
            compare(cancel.count, 0)
            verify(!flow().isOpen)
            compare(flow().current, "")
            compare(journal.last("leave").arg, "exit")
            compare(cams.countCalls("stopCapture"), stop0 + 1)
            compare(cams.countCalls("disconnectAll"), cd0 + 1)
            compare(imu.countCalls("cancelPacedConnect"), q0 + 1)
            compare(imu.countCalls("disconnectAll"), id0 + 1)
            tryCompare(journal, "alive", 0)
        }

        function test_N05F_skipConsequences() {
            cam("FO", CameraInstance.FaceOn, true)
            cam("DTL", CameraInstance.DownTheLine, true)
            wit("WT-A", "A", false)
            wit("WT-B", "B", false)
            makeShell()
            open()
            go("done")
            var skipped = ["cameras", "triangulate", "ball", "imus", "calibrateArm"]
            for (var i = 0; i < skipped.length; ++i) {
                compare(flow().current, skipped[i])
                go("skipped")
                compare(draft().state(skipped[i]), "skipped", skipped[i])
                compare(journal.last("leave").arg, "skip")
            }
            compare(flow().current, "checkArm")
            var texts = flow().issues.map(function(x) { return x.text })
            verify(texts.indexOf("Cameras skipped — no video will be captured this session") >= 0, JSON.stringify(texts))
            verify(texts.indexOf("Motion sensors skipped — no movement data will be captured") >= 0, JSON.stringify(texts))
            // Suppressed as today: the pair checks when Cameras was skipped, the calibration
            // reminder when the sensors step was.
            verify(texts.indexOf("Triangulation not confirmed") < 0)
            verify(texts.indexOf("Sensor position calibration not completed — return to the Calibrate step") < 0)
            var rows = {}
            flow().summaryRows.forEach(function(r) { rows[r.label] = r })
            compare(rows["Cameras"].value, "Skipped — no video capture")
            compare(rows["Ball detection"].value, "Skipped")
            compare(rows["Sensors"].value, "Skipped — no motion data")
            var st = {}
            flow().steps.forEach(function(s) { st[s.key] = s })
            compare(st.cameras.state, "skipped")
            verify(st.cameras.attention, "a skipped step with issues needs attention")
            verify(!st.ball.attention)
        }

        function test_N06F_ballGate() {
            var inst = faceOnWithBall(false)
            makeShell()
            open()
            go("done"); go("done")
            compare(flow().current, "ball")
            verify(!flow().canContinue)
            go("done")
            compare(flow().current, "ball", "Continue must not pass without a ball")
            compare(draft().state("ball"), "pending")
            go("skipped")
            compare(flow().current, "imus")
            compare(draft().state("ball"), "skipped")
            back()
            compare(flow().current, "ball")
            inst.ballPresent = true
            verify(flow().canContinue)
            go("done")
            compare(flow().current, "imus")
            compare(draft().state("ball"), "done")
        }

        function test_N07F_calibrateGate() {
            faceOnWithBall(true)
            var a = wit("WT-A", "A", true)
            var b = wit("WT-B", "B", true)
            makeShell()
            open()
            while (flow().current !== "calibrateArm") go("done")
            verify(!flow().canContinue)
            go("done")
            compare(flow().current, "calibrateArm", "Continue must not pass an uncalibrated arm")
            go("skipped")
            compare(flow().current, "checkArm")
            compare(draft().state("calibrateArm"), "skipped")
            back()
            a.anatCalibrated = true
            b.anatCalibrated = true
            draft().recordOutcome("arm", "witmotion", [a, b])
            verify(draft().outcome("arm").done)
            verify(flow().canContinue)
            go("done")
            compare(flow().current, "checkArm")
            compare(draft().state("calibrateArm"), "done")
        }

        function test_N08F_headerForwardNeverRunsPrimary() {
            cam("FO", CameraInstance.FaceOn, false)          // enabled, not connected
            makeShell()
            journal.setConfig("cameras", "canContinue", false)   // what a page offering Connect says
            open()
            go("done")
            compare(flow().current, "cameras")
            var runs = 0
            flow().page.primary = { label: "Connect", busy: false, run: function() { ++runs } }
            verify(!flow().canHeaderForward, "› must be disabled while Continue is")
            verify(flow().canHeaderBack)
            flow().next("done")                              // what › does — and all it does
            settle()
            compare(flow().current, "cameras")
            compare(draft().state("cameras"), "pending")
            compare(runs, 0, "› ran the page's primary")
            compare(cams.countCalls("setSelected"), 0)
        }

        function test_N09F_cancelAtEachStep() {
            cam("FO", CameraInstance.FaceOn, true)
            cam("DTL", CameraInstance.DownTheLine, true)
            wit("WT-A", "A", false)
            wit("WT-B", "B", false)
            makeShell()
            var cancelled = spy(flow(), "cancelled")
            var started = spy(flow(), "startRequested")
            var plan = ["goals", "cameras", "triangulate", "ball", "imus", "calibrateArm", "checkArm", "ready"]
            for (var i = 0; i < plan.length; ++i) {
                cams.setCameraSelected("FO", true)           // the last cancel disconnected them
                cams.setCameraSelected("DTL", true)
                open()
                compare(flow().plan, plan)
                while (flow().current !== plan[i]) go("skipped")
                var s0 = cams.countCalls("stopCapture"), c0 = cams.countCalls("disconnectAll")
                var q0 = imu.countCalls("cancelPacedConnect"), d0 = imu.countCalls("disconnectAll")
                flow().exit("cancel")
                settle()
                compare(cancelled.count, i + 1, plan[i])
                verify(!flow().isOpen)
                compare(flow().page, null)
                compare(journal.last("leave").arg, "exit", plan[i])
                compare(cams.countCalls("stopCapture"), s0 + 1, plan[i])
                compare(cams.countCalls("disconnectAll"), c0 + 1, plan[i])
                compare(imu.countCalls("cancelPacedConnect"), q0 + 1, plan[i])
                compare(imu.countCalls("disconnectAll"), d0 + 1, plan[i])
                tryCompare(journal, "alive", 0)
            }
            compare(started.count, 0)
        }

        function test_N10F_startHandsPresetAndGoals() {
            faceOnWithBall(true)
            makeShell()
            var started = spy(flow(), "startRequested")
            open()
            while (flow().current !== "ready") go("done")
            var s0 = cams.countCalls("stopCapture"), d0 = imu.countCalls("disconnectAll")
            flow().exit("start")
            settle()
            compare(started.count, 1)
            compare(started.signalArguments[0][0], wrist)
            compare(started.signalArguments[0][1], ["generalAssessment"], "no goals → the preset's first")
            compare(cams.countCalls("stopCapture"), s0, "Start must keep the devices")
            compare(imu.countCalls("disconnectAll"), d0)
            verify(!flow().isOpen)

            open()
            draft().toggleGoal("impactConditions")
            draft().toggleGoal("wristAngleTop")
            while (flow().current !== "ready") go("done")
            flow().exit("start")
            settle()
            compare(started.count, 2)
            compare(started.signalArguments[1][1], ["impactConditions", "wristAngleTop"])

            // Defensive, as today: a comingSoon preset is not startable.
            open(SessionController.Swing)
            flow().exit("start")
            settle()
            compare(started.count, 2)
            verify(flow().isOpen)
        }

        function test_N11F_N12F_suspendResumeKeepsTheDraft() {
            cam("FO", CameraInstance.FaceOn, false)
            wit("WT-A", "A", false)
            appSettings.sessionGoalsByType = ({ "1": ["wristAngleTop"] })
            makeShell()
            open()
            compare(draft().goals, ["wristAngleTop"], "seeded from the profile at open")
            draft().toggleGoal("impactConditions")
            ctx().setCameraEnabled("FO", false)
            ctx().setSensorEnabled("WT-A", false)
            var en0 = cams.countCalls("setSessionCameraEnabled"), ie0 = imu.countCalls("setSessionImuEnabled")
            flow().suspend()
            settle()
            verify(flow().suspended)
            compare(journal.last("leave").arg, "suspend")
            verify(!flow().page.active)
            verify(!flow().canHeaderForward && !flow().canHeaderBack)
            go("done")                                       // nothing navigates while hidden
            compare(flow().current, "goals")
            flow().resume()
            settle()
            verify(!flow().suspended)
            compare(journal.last("enter").arg, "resume")
            verify(flow().page.active)
            compare(journal.created, 1, "the page survives a suspend")
            compare(draft().goals, ["wristAngleTop", "impactConditions"])
            verify(draft().goalsInteracted)
            compare(cams.sessionCameraExcluded, ["FO"])
            compare(imu.sessionImuExcluded, ["WT-A"])
            compare(cams.countCalls("setSessionCameraEnabled"), en0, "no re-seed on resume (F4)")
            compare(imu.countCalls("setSessionImuEnabled"), ie0)
        }

        function test_N13F_openResetsTheVisit() {
            faceOnWithBall(true)
            var a = wit("WT-A", "A", false)
            wit("WT-B", "B", false)
            appSettings.sessionGoalsByType = ({ "1": ["wristAngleTop"] })
            imu.autoConnectOnSelect = true
            makeShell()
            open()
            draft().toggleGoal("impactConditions")
            go("done"); go("done"); go("done")
            compare(flow().current, "imus")
            ctx().connectSensors()                           // paced: WT-A now, WT-B in 2 s
            verify(imu.pacedConnectActive)
            draft().recordOutcome("arm", "witmotion", [a])
            var q0 = imu.countCalls("cancelPacedConnect")
            open()
            compare(flow().current, "goals")
            var p = flow().plan
            for (var i = 0; i < p.length; ++i) compare(draft().state(p[i]), "pending", p[i])
            compare(draft().goals, ["wristAngleTop"])
            verify(!draft().goalsInteracted)
            verify(!draft().outcome("arm").recorded)
            compare(imu.countCalls("cancelPacedConnect"), q0 + 1)
            verify(!imu.pacedConnectActive)
            compare(journal.last("leave").arg, "exit", "the old visit's page heard the exit")
            compare(journal.alive, 1)
        }

        function test_N14F_currentStepStopsApplying() {
            cam("FO", CameraInstance.FaceOn, true)
            cam("DTL", CameraInstance.DownTheLine, true)
            makeShell()
            journal.setConfig("triangulate", "canContinue", false)
            open()
            go("done"); go("done")
            compare(flow().current, "triangulate")
            cams.setCameraSelected("DTL", false)
            compare(flow().current, "triangulate", "the current step is pinned")
            verify(flow().noLongerNeeded)
            compare(flow().plan, ["goals", "cameras", "triangulate", "ball", "imus", "ready"])
            compare(flow().stepLabel, "STEP 3 OF 6 · TRIANGULATION", "numbering never blank")
            compare(flow().page.stepLabel, "STEP 3 OF 6 · TRIANGULATION")
            verify(flow().canContinue, "nothing left to require of a step that no longer applies")
            go("done")
            compare(flow().current, "ball")
            compare(flow().plan, ["goals", "cameras", "ball", "imus", "ready"])
            compare(flow().page.stepLabel, "STEP 3 OF 5 · BALL DETECTION")
        }

        function test_N16F_goToVisitedStepsOnly() {
            faceOnWithBall(true)
            makeShell()
            open()
            go("done"); go("skipped"); go("done")
            compare(flow().current, "imus")
            verify(flow().canGoTo("goals"))
            verify(flow().canGoTo("cameras"))
            verify(!flow().canGoTo("imus"), "the current step")
            verify(!flow().canGoTo("ready"), "a future step")
            flow().goTo("ready")
            settle()
            compare(flow().current, "imus")
            flow().goTo("cameras")
            settle()
            compare(flow().current, "cameras")
            compare(journal.trace("imus").slice(-3), ["leave:jump", "active:false", "died"])
            compare(journal.last("enter").arg, "jump")
            compare(draft().state("ball"), "done", "states in between are kept")
            compare(draft().state("cameras"), "skipped")
            compare(draft().state("imus"), "pending")
            // N15's form: back to a visited step with the states from it on reset.
            go("done"); go("done"); go("done")
            compare(flow().current, "ready")
            flow().goTo("ball", true)
            settle()
            compare(flow().current, "ball")
            compare(draft().state("ball"), "pending")
            compare(draft().state("imus"), "pending")
            compare(draft().state("cameras"), "done")
        }

        function test_N17F_extensionStepNeedsNoOtherEdit() {
            faceOnWithBall(true)
            makeShell()
            var d = createTemporaryObject(descComp, tc, {
                key: "dummyStep", title: "Dummy", eyebrow: "DUMMY", group: "cameras", after: "ball",
                page: Qt.resolvedUrl("support/pages/ExtraPage.qml"),
                applies: function(c, dr) { return c.faceOn.length > 0 },
                issues:  function(c, dr) { return [{ text: "Dummy needs attention", panel: -1 }] },
                summary: function(c, dr) { return { label: "Dummy", value: "ok", good: true } }
            })
            shell.steps.extensions = [d]
            open()
            compare(flow().plan, ["goals", "cameras", "ball", "dummyStep", "imus", "ready"])
            compare(flow().stepLabelFor("dummyStep"), "STEP 4 OF 6 · DUMMY")
            var entry = flow().steps.filter(function(s) { return s.key === "dummyStep" })[0]
            compare(entry.label, "Dummy"); compare(entry.group, "cameras"); compare(entry.number, 4)
            compare(flow().summaryRows.map(function(r) { return r.label }).indexOf("Dummy") >= 0, true)
            while (flow().current !== "dummyStep") go("done")
            compare(flow().page.objectName, "extraPage")
            compare(flow().page.stepLabel, "STEP 4 OF 6 · DUMMY")
            go("done")
            compare(flow().current, "imus")
            compare(draft().state("dummyStep"), "done")
            var e2 = flow().steps.filter(function(s) { return s.key === "dummyStep" })[0]
            verify(e2.attention, "a visited step with issues")
            compare(flow().issues.filter(function(x) { return x.key === "dummyStep" }).length, 1)
            back(); back()
            compare(flow().current, "ball")
        }

        function test_N18F_trunkRolesOnly() {
            cam("FO", CameraInstance.FaceOn, false)
            wit("WT-P", "", false)
            wit("WT-T", "", false)
            verify(imu.setRoleForDevice("WT-P", "pelvis"))
            verify(imu.setRoleForDevice("WT-T", "thorax"))
            makeShell()
            open()
            verify(ctx().groups.trunk.inSession)
            verify(ctx().groups.trunk.complete)
            verify(!ctx().groups.arm.inSession)
            compare(flow().plan, ["goals", "cameras", "ball", "imus", "ready"], "no arm steps without an arm sensor")
            // The two commented TRUNK lines of SetupSteps.qml, registered from here.
            var ct = createTemporaryObject(descComp, tc, {
                key: "calibrateTrunk", title: "Calibrate trunk", eyebrow: "CALIBRATE TRUNK", group: "sensors",
                instrumentGroup: "trunk", after: "checkArm", page: Qt.resolvedUrl("support/pages/ExtraPage.qml"),
                applies: function(c, dr) { return c.groups.trunk.inSession },
                gate:    function(c, dr) { return dr.outcome("trunk").done } })
            var ck = createTemporaryObject(descComp, tc, {
                key: "checkTrunk", title: "Check trunk", eyebrow: "CHECK TRUNK", group: "sensors",
                instrumentGroup: "trunk", after: "calibrateTrunk", page: Qt.resolvedUrl("support/pages/ExtraPage.qml"),
                applies: function(c, dr) { return c.groups.trunk.inSession } })
            shell.steps.extensions = [ct, ck]
            compare(flow().plan, ["goals", "cameras", "ball", "imus", "calibrateTrunk", "checkTrunk", "ready"])
            compare(shell.steps.instrumentGroups, ["arm", "trunk"])
        }

        function test_N19F_doubleNextAdvancesOnce() {
            makeShell()
            open()
            var n0 = setLines().length
            flow().next("done")
            flow().next("done")                              // a double-click, same turn
            compare(flow().current, "goals", "nothing applied inside the caller's turn")
            settle()
            wait(50)
            compare(flow().current, "cameras")
            compare(draft().state("goals"), "done")
            compare(draft().state("cameras"), "pending")
            var lines = setLines().slice(n0)
            compare(lines.length, 2, lines.join("\n"))
            verify(lines[0].indexOf("op=next") >= 0 && lines[0].indexOf("reason=dropped") >= 0, lines[0])
            verify(lines[1].indexOf("op=next from=goals to=cameras reason=done") >= 0, lines[1])
        }

        // ═════ Lifecycle (L) ══════════════════════════════════════════════════
        function test_L01F_exactlyOnePageAlive() {
            cam("FO", CameraInstance.FaceOn, true)
            cam("DTL", CameraInstance.DownTheLine, true)
            cams.addInstance("FO", { ballPresent: true })
            var a = wit("WT-A", "A", true)
            var b = wit("WT-B", "B", true)
            a.anatCalibrated = true; b.anatCalibrated = true
            makeShell()
            open()
            compare(journal.alive, 1)
            draft().recordOutcome("arm", "witmotion", [a, b])
            while (flow().current !== "ready") {
                go("done")
                compare(journal.alive, 1, flow().current)
            }
            back(); back()
            compare(journal.alive, 1)
            flow().goTo("cameras")
            settle()
            compare(journal.alive, 1)
            compare(journal.maxAlive, 1, "two pages were alive at once")
            compare(journal.created, 11)
            flow().exit("cancel")
            settle()
            tryCompare(journal, "alive", 0)
            compare(journal.maxAlive, 1)
        }

        function test_L03F_pageNavigatesFromItsOwnHandler() {
            makeShell()
            open()
            var page = flow().page
            var r = page.selfAdvance()                       // next() then reads itself
            compare(r, "alive:goals:true", "the page was torn down inside its own handler")
            compare(journal.alive, 1)
            compare(journal.count("died"), 0, "destroyed in the caller's turn")
            compare(flow().current, "goals")
            settle()
            compare(flow().current, "cameras")
            compare(journal.trace("goals"), ["born", "active:true", "enter:forward", "leave:forward", "active:false", "died"])
        }

        function test_LCF_lifecycleOrdering() {
            makeShell()
            open()
            compare(journal.trace("goals"), ["born", "active:true", "enter:forward"])
            go("done")
            compare(journal.trace("goals"), ["born", "active:true", "enter:forward", "leave:forward", "active:false", "died"])
            compare(journal.trace("cameras"), ["born", "active:true", "enter:forward"])
            // The next page is created only after the old one is GONE (R2): its birth follows the
            // old page's death in the sequence.
            var died = -1, born = -1
            journal.events.forEach(function(e) {
                if (e.key === "goals" && e.ev === "died") died = e.seq
                if (e.key === "cameras" && e.ev === "born") born = e.seq
            })
            verify(died > 0 && born > died, "cameras born " + born + " before goals died " + died)
            back()
            compare(journal.trace("cameras").slice(3), ["leave:back", "active:false", "died"])
        }

        // ═════ Outcomes ═══════════════════════════════════════════════════════
        function test_OVF_outcomeValidityHackMotion() {
            var dev = wg3("HM1", true)
            makeShell()
            open()
            verify(!draft().outcome("arm").done)
            dev.calibrationState = dev.sCalibrated
            draft().recordOutcome("arm", "hackmotion", [dev])
            verify(draft().outcome("arm").done)
            compare(ctx().setupFacts.imuRoles.slice().sort(), ["leadForearm", "leadHand"])
            verify(ctx().setupFacts.hackMotion)
            verify(ctx().roles.leadForearm.calibratedValid)
            imu.disconnectDevice("HM1")                      // the link drops
            verify(!draft().outcome("arm").done, "a dropped link voids the outcome")
            verify(draft().outcome("arm").recorded)
            compare(ctx().setupFacts.imuRoles, [])
            verify(!ctx().setupFacts.hackMotion)
        }
        function test_OVF_outcomeValidityWitmotion() {
            var a = wit("WT-A", "A", true)
            var b = wit("WT-B", "B", true)
            makeShell()
            open()
            a.anatCalibrated = true; b.anatCalibrated = true
            draft().recordOutcome("arm", "witmotion", [a, b])
            verify(draft().outcome("arm").done)
            compare(ctx().setupFacts.imuRoles.slice().sort(), ["leadForearm", "leadHand"])
            verify(!ctx().setupFacts.hackMotion)
            b.mountDeviationDeg = 20                         // over the 15° mount gate
            verify(!draft().outcome("arm").done)
            compare(ctx().setupFacts.imuRoles, [])
            b.mountDeviationDeg = 3
            verify(draft().outcome("arm").done)
            draft().clearOutcome("arm")
            verify(!draft().outcome("arm").recorded)
        }

        // ═════ R10: one app-log line per operation ════════════════════════════
        function test_R10F_oneLogLinePerOperation() {
            faceOnWithBall(false)
            makeShell()
            var ops = [
                function() { flow().open(wrist) },
                function() { flow().next("done") },
                function() { flow().next("done") },
                function() { flow().next("done") },      // refused: no ball
                function() { flow().back() },
                function() { flow().goTo("goals") },
                function() { flow().suspend() },
                function() { flow().resume() },
                function() { flow().exit("cancel") }
            ]
            var want = ["op=open", "op=next from=goals to=cameras", "op=next from=cameras to=ball",
                        "refused=gate", "op=back from=ball to=cameras", "op=goTo from=cameras to=goals",
                        "op=suspend", "op=resume", "op=exit from=goals to=- reason=cancel"]
            for (var i = 0; i < ops.length; ++i) {
                var n0 = setLines().length
                ops[i]()
                settle()
                var lines = setLines().slice(n0)
                compare(lines.length, 1, "op " + i + ":\n" + lines.join("\n"))
                verify(lines[0].indexOf(want[i]) >= 0, want[i] + " in " + lines[0])
                verify(/^\[Setup\] op=\w+ from=\S+ to=\S+ reason=\S+.* plan=\S*$/.test(lines[0]), lines[0])
            }
            compare(log.entries.filter(function(e) { return e.level !== "info" }).length, 0)
        }

        // ═════ SetupContext derivations ═══════════════════════════════════════
        function test_CTXF_cameras_data() {
            return [
                { tag: "none",  setup: [] },
                { tag: "FO",    setup: [["FO", CameraInstance.FaceOn, false]] },
                { tag: "FO+DTL", setup: [["FO", CameraInstance.FaceOn, true], ["DTL", CameraInstance.DownTheLine, true],
                                         ["IMP", CameraInstance.Impact, false]] }
            ]
        }
        function test_CTXF_cameras(data) {
            for (var i = 0; i < data.setup.length; ++i) cam(data.setup[i][0], data.setup[i][1], data.setup[i][2])
            makeShell()
            var c = ctx()
            if (data.tag === "none") {
                compare(c.faceOn.length, 0); compare(c.dtl.length, 0); compare(c.others.length, 0)
                verify(!c.camsAllConnected); verify(!c.anyCameraToConnect); verify(!c.anyCameraEnabled)
                verify(!c.hasFaceOnAndDtlSelected); compare(c.ballInstance, null); verify(!c.ballPresent)
                verify(!c.setupFacts.faceOn)
            } else if (data.tag === "FO") {
                compare(c.faceOn.length, 1); verify(c.anyCameraToConnect); verify(!c.camsAllConnected)
                verify(!c.faceOnConnected)
                c.connectCameras()
                compare(cams.countCalls("setSelected"), 1); compare(cams.countCalls("startAll"), 1)
                verify(c.camsAllConnected); verify(!c.anyCameraToConnect); compare(c.connectedCameraCount, 1)
                verify(c.faceOnConnected); verify(!c.hasFaceOnAndDtlSelected)
                var inst = cams.addInstance("FO", { ballPresent: false })
                compare(c.ballInstance, inst); verify(!c.ballPresent)
                c.ensureBallRoi()
                compare(cams.countCalls("setBallRoi"), 1)
                inst.ballPresent = true
                verify(c.ballPresent)
                c.setCameraEnabled("FO", false)
                verify(!c.anyCameraEnabled); verify(!c.camsAllConnected)
            } else {
                compare(c.faceOn.length, 1); compare(c.dtl.length, 1); compare(c.others.length, 1)
                verify(c.hasFaceOnAndDtlSelected)
                verify(!c.camsAllConnected, "the impact camera is enabled and not connected")
                c.setCameraEnabled("IMP", false)
                verify(c.camsAllConnected)
                verify(c.setupFacts.faceOn); verify(c.setupFacts.dtl)
                verify(!c.anyFixedCamera)
                appSettings.cameraFixedInPlace = ({ DTL: true })
                verify(c.anyFixedCamera)
            }
        }

        function test_CTXF_sensors_data() {
            return [
                { tag: "none",            wit: [],                                 hm: false, stray: false,
                  inSession: false, complete: false, routine: "", missing: ["leadForearm", "leadHand"],
                  devices: 0, unassigned: 0, toConnect: 0, armDevices: 0 },
                { tag: "2 Witmotion",     wit: [["WT-A", "A"], ["WT-B", "B"]],     hm: false, stray: false,
                  inSession: true, complete: true, routine: "witmotion", missing: [],
                  devices: 2, unassigned: 0, toConnect: 2, armDevices: 2 },
                { tag: "3 Witmotion",     wit: [["WT-A", "A"], ["WT-B", "B"], ["WT-C", "C"]], hm: false, stray: false,
                  inSession: true, complete: true, routine: "witmotion", missing: [],
                  devices: 3, unassigned: 0, toConnect: 3, armDevices: 3 },
                { tag: "wG3",             wit: [],                                 hm: true,  stray: false,
                  inSession: true, complete: true, routine: "hackmotion", missing: [],
                  devices: 1, unassigned: 0, toConnect: 1, armDevices: 1 },
                { tag: "wG3 + stray",     wit: [],                                 hm: true,  stray: true,
                  inSession: true, complete: true, routine: "hackmotion", missing: [],
                  devices: 2, unassigned: 1, toConnect: 1, armDevices: 1 },
                { tag: "forearm only",    wit: [["WT-A", "A"]],                    hm: false, stray: false,
                  inSession: true, complete: false, routine: "witmotion", missing: ["leadHand"],
                  devices: 1, unassigned: 0, toConnect: 1, armDevices: 1 }
            ]
        }
        function test_CTXF_sensors(data) {
            for (var i = 0; i < data.wit.length; ++i) wit(data.wit[i][0], data.wit[i][1], false)
            if (data.hm) wg3("HM1", false)
            if (data.stray) wit("WT-X", "", false)
            makeShell()
            var c = ctx()
            compare(c.devices.length, data.devices)
            compare(c.groups.arm.inSession, data.inSession)
            compare(c.groups.arm.complete, data.complete)
            compare(c.groups.arm.routine, data.routine)
            compare(c.groups.arm.missingRoles, data.missing)
            compare(c.groups.arm.devices.length, data.armDevices)
            verify(!c.groups.trunk.inSession)
            compare(c.unassignedDevices.length, data.unassigned)
            compare(c.devicesToConnect.length, data.toConnect)
            verify(!c.allInSessionConnected)
            if (data.stray) compare(c.unassignedDevices[0].id, "WT-X")
            if (data.hm) {
                compare(c.roles.leadForearm.deviceId, "HM1")
                compare(c.roles.leadHand.deviceId, "HM1")
                compare(c.roles.leadForearm.unitLabel, "Lower arm")
                compare(c.roles.leadHand.unitLabel, "Palm")
                var hmDev = c.devices.filter(function(d) { return d.id === "HM1" })[0]
                compare(hmDev.role, "leadForearm")
                compare(hmDev.roleLabel, "Lead forearm + hand")
                compare(hmDev.group, "arm")
            }
            if (data.wit.length === 3) compare(c.roles.leadUpperArm.deviceId, "WT-C")
            // Connect what is in the session: everything in it connects, the stray stays out.
            c.connectSensors()
            var call = imu.callsNamed("connectPaced")
            if (data.toConnect === 0) {
                compare(call.length, 0)
            } else {
                compare(call.length, 1)
                compare(call[0].args[0].length, data.toConnect, "one entry per peripheral")
                compare(call[0].args[1], 2000)
                tryVerify(function() { return c.allInSessionConnected }, 3000 * data.toConnect)
                compare(c.devicesToConnect.length, 0)
            }
            if (data.stray) verify(!c.devices.filter(function(d) { return d.id === "WT-X" })[0].connected)
        }

        function test_CTXF_wg3ConnectsOnce() {
            wg3("HM1", false)
            makeShell()
            var c = ctx()
            compare(c.groups.arm.roles, ["leadForearm", "leadHand"])
            compare(c.devicesToConnect.map(function(d) { return d.id }), ["HM1"], "a wG3 is ONE device")
            c.connectSensors()
            compare(imu.countCalls("setSelected"), 1)
            verify(c.allInSessionConnected)
        }

        function test_CTXF_setupFactsThroughTheCatalogue() {
            cam("FO", CameraInstance.FaceOn, true)
            var dev = wg3("HM1", true)
            lm.configured = true
            makeShell()
            open()
            dev.calibrationState = dev.sCalibrated
            draft().recordOutcome("arm", "hackmotion", [dev])
            var f = ctx().setupFacts
            compare(f.faceOn, true); compare(f.dtl, false); compare(f.hackMotion, true); compare(f.launchMonitor, true)
            var catalog = createTemporaryObject(catalogComp, tc)
            var groups = catalog.setupSummary(f)
            verify(groups.length > 0, "setupSummary returned no groups")
            var measured = 0
            for (var i = 0; i < groups.length; ++i) {
                verify(groups[i].group !== undefined && groups[i].group !== "")
                measured += groups[i].measured
            }
            verify(measured > 0, "nothing measured with a face-on camera and a calibrated wG3")
            // The same setup with the wG3's link gone measures less.
            imu.disconnectDevice("HM1")
            var after = catalog.setupSummary(ctx().setupFacts)
            var measured2 = 0
            for (var j = 0; j < after.length; ++j) measured2 += after[j].measured
            verify(measured2 < measured, measured2 + " vs " + measured)
        }
    }
}
