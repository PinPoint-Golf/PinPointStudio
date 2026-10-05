// Stage 5b — the Sensors page's mount question and the arm steps it brings (design §4.12, §4.13),
// D3 on session setup, and the calibration outcome as Check, the indicator and Recalibrate see it.
// Session setup only (support/SetupDriver.qml): none of this exists in the retired wizard.
//
//   MQ1  a found sensor with no mount: the question, the offered mounts, no arm steps
//   MQ2  choosing: setRoleForDevice once; the row names the mount; the arm steps appear
//   MQ3  a mount another sensor holds is greyed and names its holder
//   MQ4  a refused pick leaves the mount as it was and says who holds it, in the page
//   MQ5  a wG3 is offered only "Lead forearm + hand"
//   MQ6  disabling the sensor drops the arm steps
//   MQ7  a remembered mount is confirmed, not asked; "change" reopens the question
//   MQ8  a mount whose sensor is not found: its row, and Scan
//   NA1  no arm sensor: no Calibrate / Check steps, the hint, Continue allowed
//   D3N  the primary never reads Continue while a sensor is queued or still connecting
//   OC1  a link drop after the calibration is done voids it; the indicator flags Calibrate
//   OC2  Recalibrate from Check after a completed calibration: outcome dropped, a fresh run
//   AN1–AN4  an absent sensor is named from what is remembered, never by its raw "{…}" id: the
//        coach's alias; the description behind the seeded alias; "HackMotion wG3" from its unit
//        keys; "sensor" (Stage 5d — the real app showed "{767a6a67-…} not found")
//   AN5  a holder whose alias is the seeded "<description> <id>" is named by its description
//   AN6  no shown string on the Sensors page or on Ready carries a "{…}" id
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
        name: "SetupSensors"
        when: windowShown

        readonly property var d: drv
        readonly property int wrist: SessionController.Wrist

        function init() {
            testLog.reset()
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            d.setUp()
        }
        function cleanup() {
            d.tearDown()
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        readonly property string uuid: "{767a6a67-14b5-1705-bb58-9ebea6e89014}"
        readonly property var uuidRe: /\{[0-9a-f-]{36}\}/i
        // Every string the current page shows: visible items' `text`.
        function shownTexts() {
            var out = []
            d.findAll(d.flow.page, function(o) {
                if (o.visible && typeof o.text === "string" && o.text !== "") out.push(o.text)
                return false
            })
            return out
        }
        function assertNoRawIds(where) {
            var ts = shownTexts()
            verify(ts.length > 5, where + ": the page shows almost nothing (" + ts.length + " strings)")
            for (var i = 0; i < ts.length; ++i)
                verify(!uuidRe.test(ts[i]), where + ": a raw device id in \"" + ts[i] + "\"")
        }
        // An absent wG3 (switched off: NOT in the device list) holding the arm by its unit keys.
        function absentWg3(aliasValue) {
            var roles = {}
            roles[uuid + "#lowerArm"] = "leadForearm"
            roles[uuid + "#palm"] = "leadHand"
            appSettings.imuRoles = roles
            var al = {}
            if (aliasValue !== undefined) al["HackMotion wG3|" + uuid] = aliasValue
            appSettings.imuAlias = al
        }
        function absentRowLabel() {
            var rows = d.imuRows().filter(function(r) { return r.absent })
            compare(rows.length, 1, JSON.stringify(d.imuRows()))
            return rows[0].label
        }

        function hasArmSteps() {
            var p = d.plan()
            return p.indexOf("calibrateArm") >= 0 && p.indexOf("checkArm") >= 0
        }
        function setRoleCalls() { return d.imu.callsNamed("setRoleForDevice") }

        // ── MQ1 ─────────────────────────────────────────────────────────────
        function test_MQ1_unmountedSensorIsAsked() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addWitmotion("WT-N", "", true)                 // found, connected, no mount
            d.open(wrist)
            compare(d.plan(), ["goals", "cameras", "ball", "imus", "ready"], "found is not wanted (§4.12)")
            d.walkTo("imus")
            verify(d.mountQuestionShown("WT-N"), "no mount question for a found sensor with none")
            var opts = d.mountOptions("WT-N")
            compare(opts.map(function(o) { return o.text }), ["Lead forearm", "Lead hand", "Lead upper arm"])
            verify(opts.every(function(o) { return !o.taken }))
            var row = d.imuRows()[0]
            compare(row.label, "WT901BLE WT-N")
            compare(row.sub, "NO MOUNT — IT SITS OUT THIS SESSION")
            compare(d.hint(), "No sensors in this session — wrist angles will not be measured")
            compare(d.primaryLabel(), "Continue →")
            verify(!d.pip("calibrateArm").shown, "a Calibrate pip with no arm sensor")
        }

        // ── MQ2 ─────────────────────────────────────────────────────────────
        function test_MQ2_choosingAMountBringsTheArmSteps() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addWitmotion("WT-N", "", true)
            d.open(wrist)
            d.walkTo("imus")
            var n0 = setRoleCalls().length
            d.chooseMount("WT-N", "leadForearm")
            var calls = setRoleCalls().slice(n0)
            compare(calls.length, 1, JSON.stringify(calls))
            compare(calls[0].args, ["WT-N", "leadForearm"])
            compare(appSettings.imuRoles["WT-N"], "leadForearm", "the mount is a setting: it sticks")
            verify(!d.mountQuestionShown("WT-N"), "the question is still open after a pick")
            compare(d.imuRows()[0].label, "Lead forearm — WT901BLE WT-N")
            compare(d.plan(), ["goals", "cameras", "ball", "imus", "calibrateArm", "checkArm", "ready"])
            verify(d.pip("calibrateArm").shown && d.pip("checkArm").shown, "the arm steps' pips did not appear")
            compare(d.stepLabel(), "STEP 4 OF 7 · MOTION SENSORS")
            // Not the minimum set (leadForearm + leadHand): the closing page will say so.
            verify(d.readinessIssues().some(function(x) { return x.text === "Lead hand not assigned" }),
                   JSON.stringify(d.readinessIssues()))
        }

        // ── MQ3 ─────────────────────────────────────────────────────────────
        function test_MQ3_takenMountIsGreyedAndNamesItsHolder() {
            d.addWitmotion("WT-A", "A", true)
            d.imu._find("WT-A").alias = "Forearm strap"
            d.imu._touchDevices()
            d.addWitmotion("WT-N", "", true)
            d.open(wrist)
            d.walkTo("imus")
            verify(d.mountQuestionShown("WT-N"))
            var opts = d.mountOptions("WT-N")
            var fa = opts.filter(function(o) { return o.role === "leadForearm" })[0]
            verify(fa.taken, "a held mount is offered as free")
            compare(fa.text, "Lead forearm — held by Forearm strap")
            verify(opts.filter(function(o) { return o.role === "leadHand" })[0].taken === false)
            // Greyed means not choosable: a click does nothing.
            var n0 = setRoleCalls().length
            var item = d.find(d._mountColumn("WT-N"), function(o) { return o.objectName === "mountOption" && o.role === "leadForearm" })
            mouseClick(item)
            d.settle()
            compare(setRoleCalls().length, n0, "a greyed mount was written")
            // A disabled holder does not block (the manager's own rule): no longer greyed.
            d.toggleImuRow("A")
            verify(!d.mountOptions("WT-N").filter(function(o) { return o.role === "leadForearm" })[0].taken)
        }

        // ── MQ4 ─────────────────────────────────────────────────────────────
        function test_MQ4_refusedPickSnapsBack() {
            d.addWitmotion("WT-A", "A", true)
            d.addWitmotion("WT-N", "", true)
            d.open(wrist)
            d.walkTo("imus")
            // The pick the page would make if its greying were stale (the holder became present and
            // enabled between the draw and the click): the manager refuses it.
            var n0 = setRoleCalls().length
            d.flow.page.chooseMount("WT-N", "leadForearm")
            compare(setRoleCalls().length, n0 + 1)
            compare(d.mountRefusal("WT-N"), "Lead forearm is held by WT901BLE WT-A. Unassign or disable it first.")
            compare(d.imu.roleForDevice("WT-N"), "", "the refused mount was written")
            compare(appSettings.imuRoles["WT-A"], "leadForearm", "the holder lost its mount")
            verify(d.mountQuestionShown("WT-N"), "the question closed on a refusal")
            compare(d.imuRows()[1].label, "WT901BLE WT-N")
            // The next pick clears it.
            d.chooseMount("WT-N", "leadHand")
            compare(d.mountRefusal("WT-N"), "")
            compare(d.imu.roleForDevice("WT-N"), "leadHand")
        }

        // ── MQ5 ─────────────────────────────────────────────────────────────
        function test_MQ5_wg3OffersOnlyItsPair() {
            d.imu.addHackMotion("HM-1", "")
            d.imu.connectDevice("HM-1")
            d.open(wrist)
            compare(d.plan(), ["goals", "cameras", "ball", "imus", "ready"])
            d.walkTo("imus")
            verify(d.mountQuestionShown("HM-1"))
            var opts = d.mountOptions("HM-1")
            compare(opts.length, 1, JSON.stringify(opts))
            compare(opts[0].text, "Lead forearm + hand")
            d.chooseMount("HM-1", "leadForearm")
            compare(d.imu.roleForDevice("HM-1"), "leadForearm")
            compare(appSettings.imuRoles["HM-1#palm"], "leadHand", "the pair was not written as a pair")
            compare(d.imuRows()[0].label, "Lead forearm + hand — HackMotion wG3 HM-1")
            verify(hasArmSteps())
        }

        // ── MQ6 ─────────────────────────────────────────────────────────────
        function test_MQ6_disablingTheSensorDropsTheArmSteps() {
            d.addWg3("HM-1", true)
            d.open(wrist)
            verify(hasArmSteps())
            d.walkTo("imus")
            d.toggleImuRow("A")
            compare(d.imuEnabled("HM-1"), false)
            verify(!hasArmSteps(), "arm steps with the only arm sensor disabled: " + JSON.stringify(d.plan()))
            verify(!d.pip("calibrateArm").shown)
            var row = d.imuRows()[0]
            verify(row.disabled)
            compare(row.sub, "HackMotion wG3 HM-1 · DISABLED — WON'T CONNECT")
            verify(!d.mountQuestionShown("HM-1"), "a disabled sensor is asked for a mount")
            compare(d.hint(), "No sensors in this session — wrist angles will not be measured")
            d.toggleImuRow("A")
            verify(hasArmSteps())
        }

        // ── MQ7 ─────────────────────────────────────────────────────────────
        function test_MQ7_rememberedMountIsConfirmedNotAsked() {
            d.addWitmotion("WT-A", "A", true)
            d.addWitmotion("WT-B", "B", true)
            d.open(wrist)
            d.walkTo("imus")
            var n0 = setRoleCalls().length
            verify(!d.mountQuestionShown("WT-A"), "a remembered mount is asked again")
            compare(d.imuRows()[0].label, "Lead forearm — WT901BLE WT-A")
            compare(d.primaryLabel(), "Continue →")
            d.clickChangeMount("WT-A")
            verify(d.mountQuestionShown("WT-A"))
            var opts = d.mountOptions("WT-A")
            verify(opts.filter(function(o) { return o.role === "leadHand" })[0].taken, "WT-B's mount not greyed")
            d.chooseMount("WT-A", "leadUpperArm")
            compare(setRoleCalls().length, n0 + 1)
            compare(d.imu.roleForDevice("WT-A"), "leadUpperArm")
            verify(!d.mountQuestionShown("WT-A"))
            compare(d.imuRows()[0].label, "Lead upper arm — WT901BLE WT-A")
        }

        // ── MQ8 ─────────────────────────────────────────────────────────────
        function test_MQ8_absentHolderRowAndScan() {
            d.addWg3("HM-9", false)
            d.imu._find("HM-9").present = false              // remembered, not found this scan
            d.imu._touchDevices()
            d.open(wrist)
            compare(d.plan(), ["goals", "cameras", "ball", "imus", "ready"], "an absent sensor brought arm steps")
            d.walkTo("imus")
            var rows = d.imuRows()
            compare(rows.length, 1, JSON.stringify(rows))
            verify(rows[0].absent)
            compare(rows[0].label, "Lead forearm + hand — HackMotion wG3 HM-9 (not found)")
            compare(rows[0].sub, "POWER IT ON AND SCAN")
            compare(d.scanLabel(), "Scan")
            d.clickScan()
            compare(d.imu.countCalls("rescanImu"), 1)
            d.walkTo("ready")
            var iss = d.readinessIssues().map(function(x) { return x.text })
            verify(iss.indexOf("Lead forearm + hand — HackMotion wG3 HM-9 not found. Power it on and Scan.") >= 0,
                   JSON.stringify(iss))
        }

        // ── AN1–AN4 ─────────────────────────────────────────────────────────
        function test_AN1_absentNamedByTheCoachsAlias() {
            absentWg3("Lead arm wG3")
            d.open(wrist)
            d.walkTo("imus")
            compare(absentRowLabel(), "Lead forearm + hand — Lead arm wG3 (not found)")
            verify(d.readinessIssues().some(function(x) {
                return x.text === "Lead forearm + hand — Lead arm wG3 not found. Power it on and Scan." }),
                JSON.stringify(d.readinessIssues()))
            assertNoRawIds("AN1 Sensors")
        }
        function test_AN2_absentSeededAliasFallsBackToTheDescription() {
            absentWg3("HackMotion wG3 " + uuid)     // what ImuManager seeds for a newly seen device
            d.open(wrist)
            d.walkTo("imus")
            compare(absentRowLabel(), "Lead forearm + hand — HackMotion wG3 (not found)")
            assertNoRawIds("AN2 Sensors")
        }
        function test_AN3_absentWithNoAliasIsNamedByItsUnitKeys() {
            absentWg3()
            d.open(wrist)
            d.walkTo("imus")
            compare(absentRowLabel(), "Lead forearm + hand — HackMotion wG3 (not found)")
            assertNoRawIds("AN3 Sensors")
        }
        function test_AN4_absentWithNothingRememberedIsASensor() {
            var roles = {}
            roles[uuid] = "leadForearm"
            appSettings.imuRoles = roles
            appSettings.imuAlias = ({})
            d.open(wrist)
            d.walkTo("imus")
            compare(absentRowLabel(), "Lead forearm — sensor (not found)")
            verify(d.readinessIssues().some(function(x) {
                return x.text === "Lead forearm — sensor not found. Power it on and Scan." }),
                JSON.stringify(d.readinessIssues()))
            assertNoRawIds("AN4 Sensors")
        }

        // ── AN5 ─────────────────────────────────────────────────────────────
        function test_AN5_holderNamedWithoutItsId() {
            d.imu.addWitmotion(uuid, "")                 // description "WT901BLE {…}"
            d.imu.assign(uuid, "A")
            d.imu.connectDevice(uuid)
            var al = {}
            al[d.imu._find(uuid).description + "|" + uuid] = d.imu._find(uuid).description + " " + uuid
            appSettings.imuAlias = al
            d.addWitmotion("WT-N", "", true)
            d.open(wrist)
            d.walkTo("imus")
            var fa = d.mountOptions("WT-N").filter(function(o) { return o.role === "leadForearm" })[0]
            compare(fa.text, "Lead forearm — held by WT901BLE")
            compare(d.imuRows()[0].label, "Lead forearm — WT901BLE")
            d.flow.page.chooseMount("WT-N", "leadForearm")      // refused: the holder is named too
            compare(d.mountRefusal("WT-N"), "Lead forearm is held by WT901BLE. Unassign or disable it first.")
            assertNoRawIds("AN5 Sensors")
        }

        // ── AN6 ─────────────────────────────────────────────────────────────
        function test_AN6_noRawIdOnReady() {
            absentWg3("HackMotion wG3 " + uuid)
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.open(wrist)
            d.walkTo("imus")
            assertNoRawIds("AN6 Sensors")
            d.walkTo("ready")
            assertNoRawIds("AN6 Ready")
            d.readinessIssues().forEach(function(x) { verify(!uuidRe.test(x.text), x.text) })
        }

        // ── NA1 ─────────────────────────────────────────────────────────────
        function test_NA1_noArmSensorNoArmSteps() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCameraInstance("FO1", { ballPresent: true })
            d.open(wrist)
            compare(d.plan(), ["goals", "cameras", "ball", "imus", "ready"])
            d.walkTo("imus")
            compare(d.hint(), "No sensors in this session — wrist angles will not be measured")
            compare(d.primaryLabel(), "Continue →")
            verify(!d.primaryDimmed())
            d.next()
            compare(d.current(), "ready")
            compare(d.state("imus"), "done")
            var cap = d.capabilityRows()
            compare(cap[0].family, "Wrist")
            verify(/no wrist sensor/.test(cap[0].text), cap[0].text)
        }

        // ── D3N ─────────────────────────────────────────────────────────────
        // Each sensor takes 3 s to connect after it is selected (longer than the 2 s pacing), so
        // there are moments when the queue is done but a sensor is still connecting.
        function test_D3N_neverContinueWhileConnecting() {
            d.addWitmotion("WT-A", "A", false)
            d.addWitmotion("WT-B", "B", false)
            d.imu.connectDelayMs = 3000
            d.open(wrist)
            d.walkTo("imus")
            compare(d.primaryLabel(), "Connect")
            d.clickPrimary()
            var labels = {}, sawQueueDoneConnecting = false
            var t0 = Date.now()
            while (Date.now() - t0 < 8000) {
                var l = d.primaryLabel()
                labels[l] = true
                var allUp = d.ctx.allInSessionConnected
                if (!d.imu.pacedConnectActive && !allUp) sawQueueDoneConnecting = true
                if (!allUp) verify(l !== "Continue →", "Continue offered with a sensor still to connect ("
                                                      + Math.round(Date.now() - t0) + " ms)")
                if (allUp) break
                wait(100)
            }
            console.info("[D3N] primary labels seen: " + JSON.stringify(Object.keys(labels))
                         + "; the queue finished before the last sensor connected: " + sawQueueDoneConnecting)
            verify(sawQueueDoneConnecting, "the case never reached the window it is about")
            tryVerify(function() { return d.primaryLabel() === "Continue →" }, 3000)
        }

        // ── OC1 ─────────────────────────────────────────────────────────────
        function test_OC1_linkDropAfterDoneVoidsTheOutcome() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addWg3("HM-1", true)
            d.open(wrist)
            d.walkTo("calibrateArm")
            tryVerify(function() { return d.calibrationDone() }, 30000, "the HackMotion routine never completed")
            d.next()
            compare(d.current(), "checkArm")
            compare(d.state("calibrateArm"), "done")
            verify(!d.pip("calibrateArm").attention)
            d.imu.disconnectDevice("HM-1")
            verify(!d.calibrationDone(), "the link drop left the outcome standing")
            verify(d.pip("calibrateArm").attention, "no attention ring on Calibrate after the drop")
            verify(d.readinessIssues().some(function(x) {
                return x.text === "Sensor position calibration not completed — return to the Calibrate step" }))
        }

        // ── OC2 ─────────────────────────────────────────────────────────────
        function test_OC2_recalibrateAfterDone() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            var dev = d.addWg3("HM-1", true)
            d.open(wrist)
            d.walkTo("calibrateArm")
            tryVerify(function() { return d.calibrationDone() }, 30000, "the HackMotion routine never completed")
            d.next()
            compare(d.current(), "checkArm")
            compare(dev.countCalls("beginCalibration"), 1)
            d.clickRecalibrate()
            compare(d.current(), "calibrateArm")
            compare(d.state("calibrateArm"), "pending")
            compare(d.state("checkArm"), "pending")
            verify(!d.draft.outcome("arm").recorded, "the outcome was not dropped")
            verify(d.calibrationRunning(), "no fresh run")
            tryVerify(function() { return dev.countCalls("beginCalibration") === 2 }, 10000, "the fresh run never began")
        }
    }
}
