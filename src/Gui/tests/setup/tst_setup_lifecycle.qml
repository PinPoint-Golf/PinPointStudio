// Stage 0 part B1 — lifecycle and resources (catalogue group L,
// docs/design/session_wizard_refactor_design.md §7.3). L5 is in another file (CH group owner).
//
// Cases assert the catalogue's EXPECTED column. All access goes through support/SetupDriver.qml.
import QtQuick
import QtTest
import PinPointStudio
import "support"

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
        name: "SetupLifecycle"
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

        // Every applicable step, each visited once (Triangulate included), sampling `probe`
        // on arrival. Returns {step: probe()}.
        function walkSampling(probe) {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCamera("DTL1", CameraInstance.DownTheLine, true)
            d.addCameraInstance("FO1", { ballPresent: true })
            d.addWitmotion("WT-A", "A", true)
            d.addWitmotion("WT-B", "B", true)
            d.open(wrist)
            var out = {}
            var plan = d.plan()
            compare(plan, ["goals", "cameras", "framing", "triangulate", "ball", "imus", "calibrateArm", "checkArm", "ready"])
            for (var i = 0; i < plan.length; ++i) {
                d.walkTo(plan[i])
                compare(d.current(), plan[i])
                out[plan[i]] = probe()
            }
            return out
        }

        // ── L1 ───────────────────────────────────────────────────────────────
        function test_L01_onePageAliveAtATime() {
            var got = walkSampling(function() { return d.pageObjects() })
            console.info("[L1] live page objects per step: " + JSON.stringify(got))
            var want = { goals: 1, cameras: 1, framing: 1, triangulate: 1, ball: 1, imus: 1, calibrateArm: 1, checkArm: 1, ready: 1 }
            compare(got, want)
        }

        // ── L2 ───────────────────────────────────────────────────────────────
        function test_L02_atMostOneView3d() {
            var got = walkSampling(function() { return d.view3dCount() })
            console.info("[L2] View3D instances per step: " + JSON.stringify(got))
            var over = Object.keys(got).filter(function(k) { return got[k] > 1 })
            compare(over, [], "steps with more than one View3D: " + JSON.stringify(got))
        }

        // ── L3 ───────────────────────────────────────────────────────────────
        function test_L03_navigationIsQueued() {
            // Through the shell: the live Goals page requests Continue through its own `flow`
            // and goes on reading itself in the same turn (what a page handler does).
            d.open(wrist)
            var page = d.flow.page
            compare(page.stepKey, "goals")
            page.flow.next("done")
            var seen = page.stepKey + ":" + page.active + ":" + page.hint
            compare(seen, "goals:true:No goals selected — defaulting to General assessment",
                    "the page was torn down inside the caller's turn")
            compare(d.current(), "goals", "navigation applied inside the caller's turn")
            compare(d.pageObjects(), 1)
            d.settle()
            compare(d.current(), "cameras")
            compare(d.state("goals"), "done")
            compare(d.pageObjects(), 1)
        }

        // ── L4 ───────────────────────────────────────────────────────────────
        // Every case in every B1 file enforces L4 in cleanup(). This case adds the routes
        // the others touch only in passing: a full footer walk with every device kind, the
        // Recalibrate link, a Settings round trip, the header arrows, Start, and Cancel —
        // checking for warnings at each stage, not only at the end.
        function test_L04_noUnexpectedWarnings() {
            function gate(stage) {
                var w = testLog.takeWarnings()
                compare(w.length, 0, stage + ": unexpected warnings (" + w.length + "):\n" + w.join("\n"))
            }
            d.addCamera("FO1", CameraInstance.FaceOn, false)
            d.addCamera("DTL1", CameraInstance.DownTheLine, false)
            d.addCamera("IMP1", CameraInstance.Impact, false)
            d.addCameraInstance("FO1", { ballPresent: true })
            d.addWitmotion("WT-C", "C", false)
            d.addWg3("HM-1", false)
            d.open(wrist)
            gate("open")
            d.toggleGoal("impactConditions")
            d.next()
            d.clickPrimary()                         // Connect cameras
            d.next()
            gate("cameras")
            d.next()                                 // Framing (live frame, pose on)
            gate("framing")
            d.next()                                 // Triangulate
            d.next()                                 // Ball (present)
            gate("triangulate+ball")
            d.clickPrimary()                         // Connect IMUs (wG3, then WT-C)
            tryVerify(function() { return d.primaryLabel() === "Continue →" }, 8000)
            d.next()
            gate("imus")
            d.skip()                                 // Calibrate
            d.clickRecalibrate()
            d.skip()
            gate("calibrate+recalibrate")
            d.suspend()
            d.resume()
            gate("settings round trip")
            d.headerBack()                           // Check → Calibrate
            d.headerForward()                        // gated on Calibrate: stays
            compare(d.current(), "calibrateArm")
            d.skip()
            d.next()
            compare(d.current(), "ready")
            gate("header arrows")
            d.start()
            gate("start")
            d.open(wrist)
            d.cancel()
            gate("cancel")
        }
    }
}
