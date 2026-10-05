// Stage 0 part B1 — devices (catalogue group D,
// docs/design/session_wizard_refactor_design.md §7.3).
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
        name: "SetupDevices"
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

        // Production calls of `name` only (the wizard's open-time enablement re-seed calls
        // setSessionCameraEnabled/setSessionImuEnabled for every device; those are excluded
        // by asking for a specific name).
        function callNames(mgr, names) {
            return mgr.calls.filter(function(c) { return names.indexOf(c.name) >= 0 })
                            .map(function(c) { return c.name + "(" + c.args.join(",") + ")" })
        }

        // ── D1 ───────────────────────────────────────────────────────────────
        function test_D01_camerasConnect() {
            d.addCamera("FO1", CameraInstance.FaceOn, false)
            d.addCamera("DTL1", CameraInstance.DownTheLine, false)
            d.open(wrist)
            d.next()
            compare(d.current(), "cameras")
            compare(d.primaryLabel(), "Connect")
            d.clickPrimary()
            compare(callNames(d.cams, ["setSelected", "startAll"]),
                    ["setSelected(0,true)", "setSelected(1,true)", "startAll()"])
            compare(d.current(), "cameras", "Connect advanced the step")
            compare(d.primaryLabel(), "Continue →")
        }

        // ── D2 ───────────────────────────────────────────────────────────────
        function test_D02_cameraToggleOff() {
            d.addCamera("FO1", CameraInstance.FaceOn, false)
            d.addCamera("FO2", CameraInstance.FaceOn, false)
            d.open(wrist)
            d.next()
            var before = d.cams.countCalls("setSessionCameraEnabled")
            d.toggleCameraRow("FO2")
            compare(d.cams.countCalls("setSessionCameraEnabled"), before + 1)
            var last = d.cams.calls[d.cams.calls.length - 1]
            compare([last.name, last.args[0], last.args[1]], ["setSessionCameraEnabled", "FO2", false])
            var row = d.cameraRows().filter(function(r) { return r.label.indexOf("FO2") >= 0 })[0]
            verify(row.disabled, "FO2 row not dimmed")
            compare(row.sub, "FO2 · DISABLED — WON'T CONNECT")
            compare(row.toggleOn, false)
            compare(d.primaryLabel(), "Connect")
            d.clickPrimary()
            compare(callNames(d.cams, ["setSelected", "startAll"]), ["setSelected(0,true)", "startAll()"])
        }

        // ── D3 ───────────────────────────────────────────────────────────────
        function test_D03_imuPacedConnect() {
            d.addWitmotion("WT-A", "A", false)
            d.addWitmotion("WT-B", "B", false)
            d.addWitmotion("WT-C", "C", false)
            d.open(wrist)
            d.walkTo("imus")
            compare(d.primaryLabel(), "Connect")
            // ⚠ QML Timers run on the scene graph's animation clock. Under the offscreen QPA
            // that clock advances one vsync per frame while frames come faster than vsync, so
            // QML time runs ~1.33× wall time (a 2000 ms Timer fires after ~1500 ms) unless the
            // run sets QSG_USE_SIMPLE_ANIMATION_DRIVER=1 (then it is exact). The pace is
            // therefore checked against a REFERENCE 2000 ms QML Timer started with the click:
            // on a real-time clock that is t ≈ 0, 2, 4 s, as the catalogue states.
            var ref = []
            var refTimer = Qt.createQmlObject("import QtQuick; Timer { interval: 2000; repeat: true }", tc)
            refTimer.triggered.connect(function() { ref.push(Date.now()) })
            var t0 = Date.now()
            refTimer.start()
            d.clickPrimary()
            compare(d.primaryLabel(), "Connecting…")
            verify(d.connectingFrameRunning(), "traveling-light frame not running at the start")
            wait(1000)
            compare(d.primaryLabel(), "Connecting…")
            verify(d.connectingFrameRunning(), "traveling-light frame stopped mid-run")
            // Between the 2nd and 3rd connect (A and B up, C still queued).
            tryVerify(function() { return d.imu.countCalls("setSelected") === 2 }, 6000, "2nd connect never came")
            var midLabel = d.primaryLabel(), midFrame = d.connectingFrameRunning()
            tryVerify(function() { return d.imu.countCalls("setSelected") === 3 && ref.length >= 2 }, 10000,
                      "queue did not select all three sensors")
            refTimer.destroy()
            var sel = d.imu.callsNamed("setSelected")
            var offsets = sel.map(function(c) { return c.t - t0 })
            var want = [0, ref[0] - t0, ref[1] - t0]
            console.info("[D3] setSelected at " + JSON.stringify(offsets) + " ms after the click; reference "
                         + "2000 ms QML ticks at " + JSON.stringify(want.slice(1)) + " ms wall (QML time runs "
                         + (4000 / want[2]).toFixed(2) + "× wall time in this run)")
            for (var i = 0; i < 3; ++i)
                verify(Math.abs(offsets[i] - want[i]) <= 300,
                       "setSelected #" + i + " at " + offsets[i] + " ms, want " + want[i] + " ± 300")
            compare(sel.map(function(c) { return c.args[0] }), [0, 1, 2])
            tryVerify(function() { return d.primaryLabel() === "Continue →" }, 3000,
                      "primary did not return to Continue (reads '" + d.primaryLabel() + "')")
            verify(!d.connectingFrameRunning(), "traveling-light frame still running after the queue")
            // "Connecting…" for the whole run. Not a listed finding: once the REQUIRED slots
            // are up, canConnect is forced false by `_connecting`, so imuConnectMode ends and
            // the primary offers Continue while the optional C is still queued (the frame
            // keeps running).
            console.info("[D3] between the 2nd and 3rd connect: primary '" + midLabel + "', frame running " + midFrame)
            verify(midFrame, "traveling-light frame not running between the 2nd and 3rd connect")
            compare(midLabel, "Connecting…")
        }

        // ── D4 ───────────────────────────────────────────────────────────────
        function test_D04_wg3OneDeviceOneRow() {
            d.addWg3("HM-1", false)
            d.open(wrist)
            d.walkTo("imus")
            var rows = d.imuRows()
            // One row per SENSOR (§4.12): a wG3 is one peripheral with one mount, the pair.
            compare(rows.length, 1, JSON.stringify(rows))
            compare(rows[0].label, "Lead forearm + hand — HackMotion wG3 HM-1")
            compare(rows[0].sub, "HackMotion wG3 HM-1 — PRESS CONNECT")
            d.clickPrimary()
            wait(2500)                              // longer than one queue tick
            compare(callNames(d.imu, ["setSelected"]), ["setSelected(0,true)"])
            rows = d.imuRows()
            compare(rows.length, 1)
            compare(rows[0].sub, "HackMotion wG3 HM-1 · BLE")       // no device id (Stage 5d)
            verify(rows[0].ok)
            compare(rows[0].chip, "Connected")
            compare(d.primaryLabel(), "Continue →")
        }

        // ── D5 ───────────────────────────────────────────────────────────────
        function test_D05_queueOutlivesBackCancelStopsIt() {
            d.addWitmotion("WT-A", "A", false)
            d.addWitmotion("WT-B", "B", false)
            d.addWitmotion("WT-C", "C", false)
            d.open(wrist)
            d.walkTo("imus")
            d.clickPrimary()
            compare(d.imu.countCalls("setSelected"), 1)
            d.back()
            compare(d.current(), "ball")
            tryVerify(function() { return d.imu.countCalls("setSelected") === 3 }, 8000,
                      "Back from IMUs stopped the queue")
            compare(d.imu.instances.length, 3)

            // Cancel mid-queue.
            d.imu.disconnectAll()
            d.open(wrist)
            d.walkTo("imus")
            var n0 = d.imu.countCalls("setSelected")
            d.clickPrimary()
            compare(d.imu.countCalls("setSelected"), n0 + 1)
            d.cancel()
            wait(4500)                                  // two queue ticks
            compare(d.imu.countCalls("setSelected"), n0 + 1, "the queue kept connecting after Cancel")
            verify(!d.imuQueueActive())
        }

        // ── D6 ───────────────────────────────────────────────────────────────
        function test_D06_assignedSensorNotFound() {
            d.imu.assign("WT-X", "A")                   // placement names a sensor that is not in the list
            d.addWitmotion("WT-B", "B", false)
            d.open(wrist)
            d.walkTo("imus")
            // The mount whose sensor is not found is its own row (§4.12, D6 reworded from slot
            // to mount), in its fail state, after the found sensors' rows.
            var rows = d.imuRows()
            console.info("[D6] rows: " + JSON.stringify(rows))
            var gone = rows.filter(function(r) { return r.absent })
            compare(gone.length, 1, JSON.stringify(rows))
            compare(gone[0].label, "Lead forearm — sensor (not found)")
            compare(gone[0].sub, "POWER IT ON AND SCAN")
            verify(!gone[0].ok && !gone[0].warn && !gone[0].disabled, JSON.stringify(gone[0]))
            compare(d.hint(), "Sensors assigned — tap Connect to pair them")
            d.clickPrimary()                        // connects WT-B, the one sensor found
            tryVerify(function() { return d.primaryLabel() === "Continue →" }, 3000, d.primaryLabel())
            compare(d.hint(), "A sensor was not found — power it on and Scan, or skip")
            d.next()
            d.walkTo("ready")
            var iss = d.readinessIssues()
            var hit2 = iss.filter(function(x) { return x.text === "Lead forearm — sensor not found. Power it on and Scan." })
            compare(hit2.length, 1, JSON.stringify(iss))
            compare(hit2[0].panel, d.wizard.settingsPanelImus)
            // Reported once: not again as "not assigned" (Stage 0's R1 C08).
            compare(iss.filter(function(x) { return x.text.indexOf("Lead forearm") >= 0 }).length, 1, JSON.stringify(iss))
        }

        // ── D7 ───────────────────────────────────────────────────────────────
        function test_D07_scanButton() {
            d.open(wrist)
            d.walkTo("imus")
            compare(d.scanLabel(), "Scan")
            d.imu.imuScanActive = true
            compare(d.scanLabel(), "Scanning…")
            d.imu.imuScanActive = false
            compare(d.scanLabel(), "Scan")
            d.clickScan()
            compare(d.imu.countCalls("rescanImu"), 1)
        }
    }
}
