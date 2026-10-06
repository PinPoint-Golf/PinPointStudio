// Stage 0 part B1 — the Ready page (catalogue group R,
// docs/design/session_wizard_refactor_design.md §7.3).
//
// R1 and R3 are CHARACTERISATION: twelve hardware/skip combinations, each walked through the
// footer (the header › only where noted), and the Ready page's readiness issues (R1), its "What
// is set up" rows and its "What this session will record" rows (R3) compared with a literal
// golden table. The first table was captured from the retired wizard on 5 Oct 2026; it encoded
// that wizard's per-session-type "required slot" wording and went with it (Stage 5c — archived
// in design §11). This one was re-baselined on purpose against session setup (Stages 5b, 5c),
// read row by row; a row that still reads wrongly to a golfer is marked "⚠ READS WRONGLY?" so
// that changing it is a decision, not an accident.
//
// All access goes through support/SetupDriver.qml.
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
        name: "SetupReady"
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

        // ── The twelve combinations ──────────────────────────────────────────
        // cams:   none | fo | fo+dtl | fo+dtl-fixed (both marked fixed in place)
        // camAct: continue | connect (Connect, then Continue) | skip
        // imus:   none | wt2 (A,B) | wt3 (A,B,C) | wg3 (A+B) | absentA (A assigned to a sensor
        //         that is not in the device list, B present)
        // imuAct: connect (Connect, wait for the queue, Continue) | skip | header (connect what
        //         can be, then the header › — the footer offers only Skip here)
        // calib:  done (the real routine) | skip | drop (done, then the lead sensor's link
        //         drops on Check, so the calibration is NOT valid at Ready)
        // ball:   present (a face-on instance reporting ballPresent; Continue) | skip
        readonly property var combos: [
            { id: "C01", cams: "none",         camAct: "continue", imus: "none",    imuAct: "skip",    calib: "skip", ball: "skip" },
            { id: "C02", cams: "fo",           camAct: "connect",  imus: "wt2",     imuAct: "connect", calib: "done", ball: "present" },
            { id: "C03", cams: "fo+dtl",       camAct: "connect",  imus: "wt2",     imuAct: "connect", calib: "skip", ball: "present" },
            { id: "C04", cams: "fo",           camAct: "skip",     imus: "wt3",     imuAct: "connect", calib: "skip", ball: "skip" },
            { id: "C05", cams: "fo",           camAct: "connect",  imus: "wt2",     imuAct: "skip",    calib: "skip", ball: "skip" },
            { id: "C06", cams: "fo",           camAct: "connect",  imus: "wg3",     imuAct: "connect", calib: "done", ball: "present" },
            { id: "C07", cams: "fo",           camAct: "connect",  imus: "wg3",     imuAct: "connect", calib: "drop", ball: "present" },
            { id: "C08", cams: "fo",           camAct: "connect",  imus: "absentA", imuAct: "header",  calib: "skip", ball: "skip" },
            { id: "C09", cams: "fo+dtl",       camAct: "connect",  imus: "none",    imuAct: "skip",    calib: "skip", ball: "skip" },
            { id: "C10", cams: "fo",           camAct: "connect",  imus: "none",    imuAct: "skip",    calib: "skip", ball: "present" },
            { id: "C11", cams: "none",         camAct: "continue", imus: "wt2",     imuAct: "connect", calib: "skip", ball: "skip" },
            { id: "C12", cams: "fo+dtl-fixed", camAct: "connect",  imus: "wg3",     imuAct: "connect", calib: "skip", ball: "present" }
        ]

        // ── Golden table (session setup; re-baselined on purpose at Stages 5b and 5c) ──
        // `rows` ends with the launch monitor; `capability` is "What this session will record".
        readonly property var golden: ({
            // No arm steps: no arm sensor is in the session (§4.12). Continue on Cameras with no camera: Ready names the missing face-on view.
            "C01": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "Face-on camera not assigned", panel: 3 },
                    { text: "Motion sensors skipped — no movement data will be captured", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "Face-on camera not assigned", good: false },
                    { label: "Ball detection", value: "Skipped", good: false },
                    { label: "Sensors", value: "Skipped — no motion data", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Not recorded · no wrist sensor · no face-on camera",
                    "Body turn and posture — Not recorded · no trunk sensors · no face-on camera",
                    "Setup and stance — Not recorded · no face-on camera · no down-the-line camera",
                    "Sequence and tempo — Not recorded · no face-on camera · no wrist sensor",
                    "Club — Not recorded · no launch monitor · no face-on camera",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            "C02": {
                heading: "You're good to go", primary: "▶  Start session",
                issues: [],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "1 camera connected", good: true },
                    { label: "Ball detection", value: "Ball detected", good: true },
                    { label: "Sensors", value: "2 sensors assigned", good: true },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Partly, 6 of 11 · no upper-arm sensor",
                    "Body turn and posture — Partly, 8 of 15 · no trunk sensors · no down-the-line camera",
                    "Setup and stance — Partly, 18 of 22 · no down-the-line camera",
                    "Sequence and tempo — Recorded, 6 of 9 estimated · no trunk sensors · no down-the-line camera",
                    "Club — Partly, 9 of 21 · no launch monitor · no down-the-line camera · estimated from the camera",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            // (F11, kept as today — D7) the two stereo issues come from the stubs: every non-fixed two-camera setup is "Not quite ready".
            "C03": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "Stereo calibration not confirmed — use Recalibrate in the triangulation step", panel: -1 },
                    { text: "Triangulation not confirmed", panel: -1 },
                    { text: "Sensor position calibration not completed — return to the Calibrate step", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "2 cameras connected", good: true },
                    { label: "Triangulation", value: "Not confirmed", good: false },
                    { label: "Ball detection", value: "Ball detected", good: true },
                    { label: "Sensors", value: "2 sensors assigned · not calibrated", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Partly, 1 of 11 · wrist sensor not calibrated",
                    "Body turn and posture — Recorded, 5 of 15 estimated · no trunk sensors",
                    "Setup and stance — Measured",
                    "Sequence and tempo — Recorded, 7 of 9 estimated · wrist sensor not calibrated · no trunk sensors",
                    "Club — Partly, 12 of 21 · no launch monitor · estimated from the cameras",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            // Cameras skipped: the camera exists, so the reasons say "face-on camera not connected".
            "C04": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "Cameras skipped — no video will be captured this session", panel: -1 },
                    { text: "Sensor position calibration not completed — return to the Calibrate step", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "Skipped — no video capture", good: false },
                    { label: "Ball detection", value: "Skipped", good: false },
                    { label: "Sensors", value: "3 sensors assigned · not calibrated", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Not recorded · wrist sensor not calibrated · face-on camera not connected",
                    "Body turn and posture — Not recorded · no trunk sensors · face-on camera not connected",
                    "Setup and stance — Not recorded · face-on camera not connected · no down-the-line camera",
                    "Sequence and tempo — Not recorded · face-on camera not connected · wrist sensor not calibrated",
                    "Club — Not recorded · no launch monitor · face-on camera not connected",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            "C05": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "Motion sensors skipped — no movement data will be captured", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "1 camera connected", good: true },
                    { label: "Ball detection", value: "Skipped", good: false },
                    { label: "Sensors", value: "Skipped — no motion data", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Partly, 1 of 11 · wrist sensor not calibrated",
                    "Body turn and posture — Partly, 8 of 15 · no trunk sensors · no down-the-line camera",
                    "Setup and stance — Partly, 18 of 22 · no down-the-line camera",
                    "Sequence and tempo — Recorded, 7 of 9 estimated · wrist sensor not calibrated · no trunk sensors",
                    "Club — Partly, 9 of 21 · no launch monitor · no down-the-line camera · estimated from the camera",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            "C06": {
                heading: "You're good to go", primary: "▶  Start session",
                issues: [],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "1 camera connected", good: true },
                    { label: "Ball detection", value: "Ball detected", good: true },
                    { label: "Sensors", value: "2 sensors assigned", good: true },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Partly, 9 of 11 · no upper-arm sensor",
                    "Body turn and posture — Partly, 8 of 15 · no trunk sensors · no down-the-line camera",
                    "Setup and stance — Partly, 18 of 22 · no down-the-line camera",
                    "Sequence and tempo — Recorded, 6 of 9 estimated · no trunk sensors · no down-the-line camera",
                    "Club — Partly, 9 of 21 · no launch monitor · no down-the-line camera · estimated from the camera",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            // The wG3's link dropped on Check: the issue names it and the Sensors row is no longer green.
            "C07": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "HackMotion wG3 HM-1 not connected — press Connect in the sensors step", panel: -1 },
                    { text: "Sensor position calibration not completed — return to the Calibrate step", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "1 camera connected", good: true },
                    { label: "Ball detection", value: "Ball detected", good: true },
                    { label: "Sensors", value: "2 sensors assigned · not connected · not calibrated", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Partly, 1 of 11 · wrist sensor not calibrated",
                    "Body turn and posture — Partly, 8 of 15 · no trunk sensors · no down-the-line camera",
                    "Setup and stance — Partly, 18 of 22 · no down-the-line camera",
                    "Sequence and tempo — Recorded, 7 of 9 estimated · wrist sensor not calibrated · no trunk sensors",
                    "Club — Partly, 9 of 21 · no launch monitor · no down-the-line camera · estimated from the camera",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            // The absent forearm sensor is reported once — nothing is remembered about WT-X, so it is "sensor"
            // (never a raw id, Stage 5d); the Sensors row says "not found".
            "C08": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "Lead forearm — sensor not found. Power it on and Scan.", panel: 4 },
                    { text: "Sensor position calibration not completed — return to the Calibrate step", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "1 camera connected", good: true },
                    { label: "Ball detection", value: "Skipped", good: false },
                    { label: "Sensors", value: "Some sensors not found", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Partly, 1 of 11 · wrist sensor not calibrated",
                    "Body turn and posture — Partly, 8 of 15 · no trunk sensors · no down-the-line camera",
                    "Setup and stance — Partly, 18 of 22 · no down-the-line camera",
                    "Sequence and tempo — Recorded, 7 of 9 estimated · wrist sensor not calibrated · no trunk sensors",
                    "Club — Partly, 9 of 21 · no launch monitor · no down-the-line camera · estimated from the camera",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            // (F11) as C03. No arm steps: no arm sensor.
            "C09": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "Stereo calibration not confirmed — use Recalibrate in the triangulation step", panel: -1 },
                    { text: "Triangulation not confirmed", panel: -1 },
                    { text: "Motion sensors skipped — no movement data will be captured", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "2 cameras connected", good: true },
                    { label: "Triangulation", value: "Not confirmed", good: false },
                    { label: "Ball detection", value: "Skipped", good: false },
                    { label: "Sensors", value: "Skipped — no motion data", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Partly, 1 of 11 · no wrist sensor",
                    "Body turn and posture — Recorded, 5 of 15 estimated · no trunk sensors",
                    "Setup and stance — Measured",
                    "Sequence and tempo — Recorded, 7 of 9 estimated · no wrist sensor · no trunk sensors",
                    "Club — Partly, 12 of 21 · no launch monitor · estimated from the cameras",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            // No arm steps: no arm sensor.
            "C10": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "Motion sensors skipped — no movement data will be captured", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "1 camera connected", good: true },
                    { label: "Ball detection", value: "Ball detected", good: true },
                    { label: "Sensors", value: "Skipped — no motion data", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Partly, 1 of 11 · no wrist sensor",
                    "Body turn and posture — Partly, 8 of 15 · no trunk sensors · no down-the-line camera",
                    "Setup and stance — Partly, 18 of 22 · no down-the-line camera",
                    "Sequence and tempo — Recorded, 7 of 9 estimated · no wrist sensor · no trunk sensors",
                    "Club — Partly, 9 of 21 · no launch monitor · no down-the-line camera · estimated from the camera",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            "C11": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "Face-on camera not assigned", panel: 3 },
                    { text: "Sensor position calibration not completed — return to the Calibrate step", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "Face-on camera not assigned", good: false },
                    { label: "Ball detection", value: "Skipped", good: false },
                    { label: "Sensors", value: "2 sensors assigned · not calibrated", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Not recorded · wrist sensor not calibrated · no face-on camera",
                    "Body turn and posture — Not recorded · no trunk sensors · no face-on camera",
                    "Setup and stance — Not recorded · no face-on camera · no down-the-line camera",
                    "Sequence and tempo — Not recorded · no face-on camera · wrist sensor not calibrated",
                    "Club — Not recorded · no launch monitor · no face-on camera",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            },
            "C12": {
                heading: "Not quite ready", primary: "▶  Start anyway",
                issues: [
                    { text: "Sensor position calibration not completed — return to the Calibrate step", panel: -1 }
                ],
                rows: [
                    { label: "Goals", value: "None — defaulting to General assessment", good: true },
                    { label: "Cameras", value: "2 cameras connected", good: true },
                    { label: "Triangulation", value: "Optional — cameras fixed in place", good: true },
                    { label: "Ball detection", value: "Ball detected", good: true },
                    { label: "Sensors", value: "2 sensors assigned · not calibrated", good: false },
                    { label: "Launch monitor", value: "None configured", good: false, tone: "neutral" }
                ],
                capability: [
                    "Wrist — Partly, 1 of 11 · wrist sensor not calibrated",
                    "Body turn and posture — Recorded, 5 of 15 estimated · no trunk sensors",
                    "Setup and stance — Measured",
                    "Sequence and tempo — Recorded, 7 of 9 estimated · wrist sensor not calibrated · no trunk sensors",
                    "Club — Partly, 12 of 21 · no launch monitor · estimated from the cameras",
                    "Ball and strike — Not recorded · no launch monitor"
                ]
            }
        })

        function comboData() {
            return combos.map(function(c) {
                return { tag: c.id + " " + [c.cams, c.camAct, c.imus, c.imuAct, c.calib, c.ball].join("/"), combo: c }
            })
        }

        function setUpHardware(c) {
            if (c.cams !== "none")      d.addCamera("FO1", CameraInstance.FaceOn, false)
            if (c.cams.indexOf("dtl") >= 0) d.addCamera("DTL1", CameraInstance.DownTheLine, false)
            if (c.cams === "fo+dtl-fixed") appSettings.cameraFixedInPlace = ({ "FO1": true, "DTL1": true })
            if (c.ball === "present")   d.addCameraInstance("FO1", { ballPresent: true })
            if (c.imus === "wt2" || c.imus === "wt3") {
                d.addWitmotion("WT-A", "A", false)
                d.addWitmotion("WT-B", "B", false)
                if (c.imus === "wt3") d.addWitmotion("WT-C", "C", false)
            } else if (c.imus === "wg3") {
                d.addWg3("HM-1", false)
            } else if (c.imus === "absentA") {
                d.imu.assign("WT-X", "A")
                d.addWitmotion("WT-B", "B", false)
            }
        }

        function waitQueueIdle() {
            tryVerify(function() { return !d.imuQueueActive() && !d.imu.anyConnecting }, 10000, "IMU queue never idled")
        }

        // Walks the combination to Ready through the footer; returns what Ready shows.
        function runCombo(c) {
            setUpHardware(c)
            d.open(wrist)
            d.next()                                                        // Goals
            compare(d.current(), "cameras")
            if (c.camAct === "skip") d.skip()
            else {
                if (c.camAct === "connect") { compare(d.primaryLabel(), "Connect"); d.clickPrimary() }
                d.next()
            }
            // Framing follows a connected camera, whatever Cameras was left with; never gated.
            if (d.current() === "framing") d.next()
            if (d.current() === "triangulate") d.next()
            compare(d.current(), "ball")
            if (c.ball === "present") d.next(); else d.skip()
            compare(d.current(), "imus")
            if (c.imuAct === "skip") d.skip()
            else {
                if (d.primaryLabel() === "Connect" && !d.primaryDimmed()) { d.clickPrimary(); waitQueueIdle() }
                if (c.imuAct === "header") verify(d.headerForward(), "header › disabled")
                else d.next()
            }
            // Arm steps only when an arm sensor is in the session (§4.12).
            if (c.imus !== "none") {
                compare(d.current(), "calibrateArm")
                if (c.calib === "skip") d.skip()
                else {
                    tryVerify(function() { return d.calibrationDone() }, 45000, "calibration never completed")
                    d.next()
                }
                compare(d.current(), "checkArm")
                if (c.calib === "drop") {
                    d.imu.disconnectDevice("HM-1")
                    verify(!d.calibrationDone(), "the link drop did not invalidate the calibration")
                }
                d.next()
            }
            compare(d.current(), "ready")
            var got = { issues: d.readinessIssues(), rows: d.summaryRows(), heading: d.readyHeading(),
                        primary: d.primaryLabel() }
            got.capability = d.capabilityRows().map(function(r) { return r.text })
            return got
        }

        function checkGolden(tag, part, got) {
            var g = golden[tag.split(" ")[0]]
            if (g === undefined) {
                console.info("[GOLDEN] " + tag.split(" ")[0] + " " + JSON.stringify(got))
                fail("no golden row for " + tag)
            }
            if (g[part] === undefined) console.info("[GOLDEN] " + tag.split(" ")[0] + " " + part + " " + JSON.stringify(got[part]))
            compare(got[part], g[part], tag + " " + part)
        }

        // ── R1 ───────────────────────────────────────────────────────────────
        function test_R01_readinessIssuesGolden_data() { return comboData() }
        function test_R01_readinessIssuesGolden(data) {
            var got = runCombo(data.combo)
            checkGolden(data.tag, "issues", got)
            checkGolden(data.tag, "heading", got)
        }

        // ── R2 ───────────────────────────────────────────────────────────────
        function test_R02_readyVersusNot() {
            setUpHardware({ cams: "fo", ball: "present", imus: "wg3" })
            d.open(wrist)
            d.next()
            d.clickPrimary()                         // Connect cameras
            d.walkTo("imus")
            d.clickPrimary()                         // Connect the wG3
            waitQueueIdle()
            d.next()
            tryVerify(function() { return d.calibrationDone() }, 45000, "calibration never completed")
            d.walkTo("ready")
            compare(d.readinessIssues(), [])
            compare(d.primaryLabel(), "▶  Start session")
            compare(d.primaryTone(), "good")
            compare(d.readyHeading(), "You're good to go")
            compare(d.readyNotice(), "Everything's set up and ready to go. Step up when you like — Pinpoint will start capturing the moment you take your address.")

            d.cams.setCameraSelected("FO1", false)   // the face-on camera drops
            compare(d.readinessIssues().length, 1)
            compare(d.primaryLabel(), "▶  Start anyway")
            compare(d.primaryTone(), "warn")
            compare(d.readyHeading(), "Not quite ready")
            compare(d.readyNotice(), "Starting with an incomplete setup is fine — partial data is often still useful. For the full picture though, it's worth coming back once the hardware is sorted. Your results will thank you for it.")
        }

        // ── R3 ───────────────────────────────────────────────────────────────
        function test_R03_summaryRowsGolden_data() { return comboData() }
        function test_R03_summaryRowsGolden(data) {
            var got = runCombo(data.combo)
            checkGolden(data.tag, "rows", got)
            checkGolden(data.tag, "primary", got)
            // The closing page's "What this session will record".
            checkGolden(data.tag, "capability", got)
        }
    }
}
