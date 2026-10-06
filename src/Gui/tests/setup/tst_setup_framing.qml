// The Framing step (pages/FramingPage.qml): applies with a connected camera that sees the golfer,
// turns pose on for its visit and puts it back, and judges the headroom above the hands at the top
// of the backswing from the live keypoints — the rule of two_camera_capture_protocol.md §2, after
// 5 Oct 2026's DTL frame put the hands at the top 4–11 % of the frame from its top edge.
//
// All access goes through support/SetupDriver.qml; keypoints are fed into the fake instance's
// poseKeypoints as the real CameraInstance publishes them ({ x, y, score }, 17 COCO points).
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
        name: "SetupFraming"
        when: windowShown

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

        // 17 keypoints: shoulders (5, 6) at `shoulderY`, wrists (9, 10) at `wristY`, both at
        // `wristScore`; every other point unseen.
        function pose(wristY, shoulderY, wristScore) {
            var out = []
            for (var i = 0; i < 17; ++i) out.push({ x: 0.5, y: 0.5, score: 0.0 })
            out[5]  = { x: 0.45, y: shoulderY, score: 0.9 }
            out[6]  = { x: 0.55, y: shoulderY, score: 0.9 }
            var ws = wristScore === undefined ? 0.9 : wristScore
            out[9]  = { x: 0.48, y: wristY, score: ws }
            out[10] = { x: 0.50, y: wristY + 0.02, score: ws }
            return out
        }
        function row(key) {
            var r = d.find(d.flow.page, function(o) { return o.objectName === "framingCamera_" + key })
            verify(r !== null, "no framing row for " + key)
            return r
        }
        // One face-on camera, connected, with a live instance; pose `poseOn` before the visit.
        function faceOnLive(poseOn) {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            return d.addCameraInstance("FO1", { ballPresent: true, isRecording: true, poseEnabled: poseOn })
        }

        // ── Applicability ────────────────────────────────────────────────────
        function test_F01_appliesOnlyWithAConnectedCamera() {
            d.addCamera("FO1", CameraInstance.FaceOn, false)
            d.open(wrist)
            verify(d.plan().indexOf("framing") < 0, "framing applies with no camera connected: " + d.plan())
            d.cams.setCameraSelected("FO1", true)
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "imus", "ready"])
            compare(d.flow.stepLabelFor("framing"), "STEP 3 OF 6 · FRAMING")
            compare(d.pip("framing").shown, true)
            // Disabled for the session: it does not record, so there is nothing to frame.
            d.cams.setSessionCameraEnabled("FO1", false)
            verify(d.plan().indexOf("framing") < 0, "framing applies to a disabled camera")
        }
        function test_F02_impactCameraAloneDoesNotApply() {
            // The impact camera films club and ball, no body: pose never runs on it.
            d.addCamera("IMP", CameraInstance.Impact, true)
            d.open(wrist)
            verify(d.plan().indexOf("framing") < 0, "framing applies to an impact camera: " + d.plan())
            d.addCamera("DTL1", CameraInstance.DownTheLine, true)
            verify(d.plan().indexOf("framing") > 0, "framing missing with a DTL camera connected")
        }

        // ── The verdict ──────────────────────────────────────────────────────
        function test_F03_verdictHoldThenOkThenTooTight() {
            var inst = faceOnLive(false)
            d.open(wrist)
            d.walkTo("framing")
            compare(d.current(), "framing")
            compare(row("FO1").verdict, "hold")
            compare(row("FO1").verdictText, "Hold the top so the camera can check")
            compare(d.hint(), "Hold the top of your backswing so the cameras can check")
            compare(d.primaryLabel(), "Continue →", "Framing never gates Continue")

            // Address: hands below the shoulders — not a top, nothing to judge.
            inst.poseKeypoints = pose(0.75, 0.40)
            compare(row("FO1").verdict, "hold")
            // A top with the wrists unsure (score 0.2 < 0.3) is not counted either.
            inst.poseKeypoints = pose(0.10, 0.60, 0.2)
            compare(row("FO1").verdict, "hold")

            // The top, hands halfway down the frame: headroom.
            inst.poseKeypoints = pose(0.50, 0.60)
            compare(row("FO1").verdict, "ok")
            compare(row("FO1").verdictText, "Headroom OK")
            compare(d.hint(), "Headroom OK")
            verify(d.appLogFake.matching("framing camera=FO1 verdict=ok", "Setup").length === 1,
                   "no app-log line for the verdict")

            // The top, hands a tenth of the frame from the top edge (5 Oct's DTL): too tight.
            inst.poseKeypoints = pose(0.10, 0.60)
            compare(row("FO1").verdict, "tight")
            compare(row("FO1").verdictText, "Too tight — tilt the camera up or move it back")
            compare(d.hint(), "Too tight — tilt the camera up or move it back")
            compare(d.primaryLabel(), "Continue →", "a tight frame still lets the golfer continue")
            d.next()
            compare(d.current(), "ball")
            compare(d.state("framing"), "done")
        }

        // ── Pose on for the visit, put back on leave ─────────────────────────
        function test_F04_leaveRestoresPoseEnabled() {
            var inst = faceOnLive(false)
            d.open(wrist)
            d.walkTo("cameras")
            compare(inst.poseEnabled, false)
            d.next()
            compare(d.current(), "framing")
            compare(inst.poseEnabled, true, "the page did not turn pose on")
            d.next()
            compare(inst.poseEnabled, false, "Continue left pose on")
            d.back()
            compare(d.current(), "framing")
            compare(inst.poseEnabled, true)
            d.back()
            compare(d.current(), "cameras")
            compare(inst.poseEnabled, false, "Back left pose on")
        }
        function test_F05_poseAlreadyOnStaysOn() {
            var inst = faceOnLive(true)
            d.open(wrist)
            d.walkTo("framing")
            compare(inst.poseEnabled, true)
            d.next()
            compare(inst.poseEnabled, true, "leave turned off a pose the Capture view had on")
        }
        function test_F06_suspendRestoresAndResumeReenables() {
            var inst = faceOnLive(false)
            d.open(wrist)
            d.walkTo("framing")
            compare(inst.poseEnabled, true)
            d.suspend()
            compare(inst.poseEnabled, false, "pose left on while setup was hidden for Settings")
            d.resume()
            compare(d.current(), "framing")
            compare(inst.poseEnabled, true, "resume did not turn pose back on")
        }
    }
}
