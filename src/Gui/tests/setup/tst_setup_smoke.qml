// Stage 0 (part A) smoke test for the session-setup harness (H1,
// docs/design/session_wizard_refactor_design.md §7.1, §9).
//
// Written at Stage 0 against the then-current wizard to answer the §9 risk; since Stage 5c it
// proves the same of session setup (ScreenSessionSetup) and ImuCalibrationFlow over the fakes in
// fakes/: does the shell instantiate offscreen, does the 3-D guide load and animate, and do the
// two calibration chains (Witmotion, HackMotion) run to completion on fake devices.
//
// Every test asserts in cleanup() that no QML warning was emitted that it did not expect.
import QtQuick
import QtTest
import PinPointStudio
import "fakes"

// ⚠ THE ROOT IS A PLAIN Item, NOT THE TestCase: TestCase declares `visible: false`
// (QtTest/TestCase.qml), so a wizard parented under it is never visible — and the
// calibration routines gate on `flow.visible`.
Item {
    id: root

    // Never lose the window's LAST View3D (View3DKeepAlive.qml).
    View3DKeepAlive { }
    width: 1400
    height: 900

    Item { id: host; anchors.fill: parent }

    TestCase {
        id: tc
        name: "SetupSmoke"
        when: windowShown

        Component { id: imuMgrComp;    FakeImuManager {} }
        Component { id: camMgrComp;    FakeCameraManager {} }
        Component { id: athleteComp;   FakeAthlete {} }
        Component { id: liveWristComp; FakeLiveWrist {} }
        Component { id: sessionComp;   FakeSessionController {} }
        Component { id: navComp;       FakeNav {} }
        Component { id: lmComp;        FakeLaunchMonitor {} }
        Component { id: appLogComp;    FakeAppLog {} }

        property var wiz: null
        property var fx: null

        // PpCameraFrame (session setup's camera rows and Ball frame) reads these two.
        Component {
            id: shotReplayComp
            QtObject {
                property bool   active: false
                property var    streams: []
                property real   positionUs: 0
                property real   impactPositionUs: 0
                property string swingDir: ""
                property var    analysisDetail: ({})
                signal positionChanged()
                function setVideoSink(i, s) {}
            }
        }
        Component {
            id: shotProcessorComp
            QtObject {
                property bool isReplaying: false
                property var  replayAnalysisDetail: ({})
                property real replayPositionUs: 0
                signal replayPositionChanged()
            }
        }

        // Session setup, built over the fakes and opened as Main opens it (open(Wrist), then shown).
        function makeSetup() {
            var props = {}
            for (var k in fx) props[k] = fx[k]
            props.shotReplay    = createTemporaryObject(shotReplayComp, tc)
            props.shotProcessor = createTemporaryObject(shotProcessorComp, tc)
            wiz = harness.createWithContext("ScreenSessionSetup", props, host,
                                            { width: host.width, height: host.height, visible: false })
            verify(wiz !== null, "ScreenSessionSetup failed to instantiate over the fakes")
            wiz.open(SessionController.Wrist)
            wiz.visible = true
            settle()
            return wiz
        }
        // Navigation is queued (R5): wait until nothing is queued and no page is loading.
        function settle() { tryVerify(function() { return !wiz.flow.busy }, 5000, "the setup flow never settled") }
        // Walks to `target` with Continue, else Skip, recording every step visited.
        function walkTo(target) {
            var seq = [wiz.flow.current], guard = 0
            while (wiz.flow.current !== target && guard++ < 20) {
                var before = wiz.flow.current
                wiz.flow.next("done"); settle()
                if (wiz.flow.current === before) { wiz.flow.next("skipped"); settle() }
                seq.push(wiz.flow.current)
            }
            return seq
        }

        // The chain spikes' host: session setup walked to Calibrate, where the calibration flow
        // lives — it exists only on that page (R2).
        //   returns { flow, atCalibrate(), next(): Continue, onCheck() }
        function chainHost() {
            makeSetup()
            walkTo("calibrateArm")
            compare(wiz.flow.current, "calibrateArm")
            return { flow: findCalibFlow(wiz.flow.page),
                     atCalibrate: function() {},
                     next: function() { wiz.flow.next("done"); settle() },
                     onCheck: function() { return wiz.flow.current === "checkArm" } }
        }

        // ── Helpers ──────────────────────────────────────────────────────────
        function makeFakes() {
            return {
                imuManager:        createTemporaryObject(imuMgrComp, tc),
                cameraManager:     createTemporaryObject(camMgrComp, tc),
                athleteController: createTemporaryObject(athleteComp, tc),
                liveWrist:         createTemporaryObject(liveWristComp, tc),
                sessionController: createTemporaryObject(sessionComp, tc),
                navController:     createTemporaryObject(navComp, tc),
                launchMonitor:     createTemporaryObject(lmComp, tc),
                appLog:            createTemporaryObject(appLogComp, tc)
            }
        }

        // Depth-first over the visual tree (children) — ids inside the wizard are not reachable
        // from outside, so internal objects are found by the members they carry.
        function findItem(item, pred) {
            if (!item) return null
            if (pred(item)) return item
            var kids = item.children
            for (var i = 0; i < kids.length; ++i) {
                var r = findItem(kids[i], pred)
                if (r) return r
            }
            return null
        }
        function findCalibFlow(w) {
            return findItem(w, function(o) {
                return o.calibrationDone !== undefined && o.isHackMotion !== undefined
                       && o.begin !== undefined && o._guide !== undefined
            })
        }
        function findArmViz(w) {
            return findItem(w, function(o) {
                return o.imuLeadForearm !== undefined && o.upperArmKnown !== undefined
            })
        }

        function ms() { return testLog.elapsedMs() }

        // "visible" of every ancestor, innermost first — for failure messages.
        function visChain(item) {
            var out = []
            for (var p = item; p; p = p.parent)
                out.push((p.objectName || String(p).split("(")[0]) + ":" + p.visible)
            return out.join(" < ")
        }

        function init() {
            testLog.reset()
            // ⚠ OFFSCREEN-PLATFORM ONLY, AND NOT A QML WARNING. The offscreen QPA's default
            // application font family is "Sans Serif", which macOS does not have, so the
            // first Text without a font.family makes Qt's font database log this one-off
            // (category qt.qpa.fonts) notice. Scoped to that platform and to that exact text
            // so it can mask nothing else.
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            appSettings.imuRoles = ({})
            fx = makeFakes()
        }

        function cleanup() {
            if (wiz) harness.destroyNow(wiz)
            wiz = null
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        // ── 0. The warning gate itself records — otherwise every cleanup() passes vacuously ──
        function test_0_testlog_records_and_consumes() {
            console.warn("setup-smoke probe: unexpected")
            testLog.expect("^setup-smoke probe: expected$")
            console.warn("setup-smoke probe: expected")
            var w = testLog.takeWarnings()
            compare(w.length, 1, JSON.stringify(w))
            verify(w[0].indexOf("setup-smoke probe: unexpected") >= 0, w[0])
            compare(testLog.expectedCount("^setup-smoke probe: expected$"), 1)
        }

        // ── 1. Loads over the fakes ──────────────────────────────────────────
        function test_1_loads_with_no_devices() {
            var t0 = ms()
            makeSetup()
            console.info("[smoke] session setup instantiated and opened in " + Math.round(ms() - t0) + " ms")
            compare(wiz.draft.preset, 1)
            compare(wiz.flow.current, "goals")
            compare(JSON.stringify(wiz.flow.plan), JSON.stringify(["goals", "cameras", "ball", "imus", "ready"]))
            // Only the current page is alive: no calibration flow, no arm view (R2).
            verify(findCalibFlow(wiz) === null, "an ImuCalibrationFlow exists off the Calibrate page")
            verify(findArmViz(wiz) === null, "an ArmVizView exists off the Check page")
            wait(250)   // let the first frames and any deferred bindings run
        }

        // ── 2. Walks to the last step with the flow's operations only ────────
        function test_2_walks_to_ready() {
            makeSetup()
            var seq = walkTo("ready")
            var st = {}
            wiz.flow.plan.forEach(function(k) { st[k] = wiz.draft.state(k) })
            console.info("[smoke] visited " + JSON.stringify(seq) + " states " + JSON.stringify(st))
            compare(JSON.stringify(seq), JSON.stringify(["goals", "cameras", "ball", "imus", "ready"]))
            // Ball refuses "done" without a ball; the walk had to skip it. Goals, Cameras (no camera
            // to connect) and the Sensors step (no sensor in the session) accept "done".
            compare(JSON.stringify(st), JSON.stringify({ goals: "done", cameras: "done", ball: "skipped",
                                                         imus: "done", ready: "pending" }))
            wiz.flow.next("done"); settle()
            compare(wiz.flow.current, "ready", "next on the last step must be a no-op")
        }

        // ── 3. SPIKE: the Witmotion chain, two still sensors in A and B ──────
        function test_3_witmotion_chain() {
            var im = fx.imuManager
            im.addWitmotion("WT-A", "Forearm")
            im.addWitmotion("WT-B", "Hand")
            im.assign("WT-A", "A")
            im.assign("WT-B", "B")
            var a = im.connectDevice("WT-A")
            var b = im.connectDevice("WT-B")
            verify(a.imuConnected && b.imuConnected)

            var t0 = ms()
            var h = chainHost()
            console.info("[spike-wt] (a) session setup at Calibrate in " + Math.round(ms() - t0) + " ms")

            var flow = h.flow
            verify(flow !== null)
            verify(flow._guide.view !== null, "BodyVizView did not register itself with the flow")
            compare(flow.isHackMotion, false)
            verify(flow.leadImu === a, "slot A did not resolve to the WT-A fake")

            var bvv = flow._guide.view
            var finished = []
            bvv.leadArmAnimFinished.connect(function() { finished.push(Math.round(ms() - t0)) })

            tryVerify(function() { return bvv.fullyLoaded }, 30000,
                      "BodyVizView.fullyLoaded never became true (loadedCount=" + bvv.loadedCount + ")")
            var tLoaded = ms()
            console.info("[spike-wt] (b) BodyVizView.fullyLoaded after " + Math.round(tLoaded - t0)
                         + " ms (loadedCount " + bvv.loadedCount + "/" + bvv.totalSegments + ")")

            h.atCalibrate()
            var tCal = ms()
            // StackLayout applies child visibility on its next polish, not synchronously.
            tryVerify(function() { return flow.visible }, 5000,
                      "calibration flow not visible on the Calibrate step (" + visChain(flow) + ")")
            console.info("[spike-wt] flow visible " + Math.round(ms() - tCal) + " ms after the step change")

            tryVerify(function() { return flow.phase === 1 }, 30000,
                      "Witmotion chain never reached phase 1 (leadArmAnimFinished fired "
                      + finished.length + "×)")
            var tPhase1 = ms()
            console.info("[spike-wt] (c) phase 1 at +" + Math.round(tPhase1 - tCal)
                         + " ms after entering Calibrate; leadArmAnimFinished at " + JSON.stringify(finished))

            tryVerify(function() { return a.countCalls("setNominalCalibration") === 1 }, 15000,
                      "arm-down capture (setNominalCalibration) never recorded")
            var tCapture = ms()
            compare(b.countCalls("setNominalCalibration"), 1, "slot B not captured with slot A")

            tryVerify(function() { return flow.calibrationDone }, 20000,
                      "calibrationDone never became true (phase " + flow.phase + ", mountFailed "
                      + flow.mountFailed + ")")
            var tDone = ms()
            console.info("[spike-wt] (c) capture at +" + Math.round(tCapture - tCal) + " ms, done at +"
                         + Math.round(tDone - tCal) + " ms after entering Calibrate; "
                         + "leadArmAnimFinished " + finished.length + "× at " + JSON.stringify(finished))
            compare(a.countCalls("refineMountAboutLongAxis"), 1)
            compare(b.countCalls("refineMountAboutLongAxis"), 1)
            compare(finished.length, 3, "expected introUp, introDown and raise to finish once each")

            // The footer gate now lets Continue through.
            h.next()
            verify(h.onCheck(), "Continue did not reach Check")
        }

        // ── 4. SPIKE: the HackMotion chain, autoScript ───────────────────────
        function test_4_hackmotion_chain() {
            var im = fx.imuManager
            im.addHackMotion("HM-1", "wG3")
            im.assign("HM-1", "A")
            var dev = im.connectDevice("HM-1")
            verify(dev.imuConnected && dev.streaming)
            dev.mode = "strict"     // auto-advance AND refuse a duplicate marker, so one shows

            var t0 = ms()
            var h = chainHost()

            var flow = h.flow
            verify(flow !== null)
            compare(flow.isHackMotion, true)
            verify(flow.leadImu === dev.unitLowerArm, "slot A did not resolve to the lower-arm unit")

            var bvv = flow._guide.view
            var finished = []
            bvv.leadArmAnimFinished.connect(function() { finished.push(Math.round(ms() - t0)) })
            tryVerify(function() { return bvv.fullyLoaded }, 30000, "BodyVizView.fullyLoaded never true")
            console.info("[spike-hm] fullyLoaded after " + Math.round(ms() - t0) + " ms")

            h.atCalibrate()
            var tCal = ms()
            tryVerify(function() { return flow.visible }, 5000,
                      "calibration flow not visible on the Calibrate step (" + visChain(flow) + ")")

            tryVerify(function() { return flow.calibrationDone }, 30000,
                      "HackMotion chain never reached done (calls "
                      + JSON.stringify(dev.calls.map(function(c) { return c.name }))
                      + ", phases " + JSON.stringify(dev.phaseLog.map(function(p) { return p.phase }))
                      + ", refusals " + dev.refusals + ")")
            var tDone = ms()
            console.info("[spike-hm] done at +" + Math.round(tDone - tCal) + " ms after entering Calibrate; "
                         + "calls " + JSON.stringify(dev.calls.map(function(c) {
                               return c.name + "@" + Math.round(c.t) }))
                         + "; leadArmAnimFinished " + finished.length + "×")

            compare(dev.countCalls("beginCalibration"), 1)
            compare(dev.countCalls("confirmHorizontal"), 1)
            compare(dev.countCalls("confirmRaise"), 1)
            compare(dev.countCalls("confirmReferencePose"), 1)
            compare(dev.countCalls("abortCalibration"), 0)
            compare(dev.refusals, 0)
            compare(dev.calibrationState, dev.sCalibrated)
        }
    }
}
