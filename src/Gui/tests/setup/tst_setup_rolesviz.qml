// Stage 3 — the arm view and the toolbar sensor panel on roles (design §4.13, §8a).
//
// Hosts the REAL ArmVizView and the REAL PpImuPanel over the fakes. What each reads from its
// context, inventoried from the files:
//   ArmVizView — athleteController.currentHandedness; imuManager.instanceForRole,
//                deviceForRole, deviceIdForRole, isHackMotionDevice and, as its one gathered
//                dependency, appSettings.imuRoles + imuManager.instances / imuDeviceList /
//                sessionImuExcluded
//   PpImuPanel — imuManager (list, instances, roleForDevice, paced connect, …), appSettings, and
//                through its ImuCalibrationFlow the same context the wizard's flow reads (the
//                flow is inactive in list mode and starts nothing)
//
//   V1 two Witmotions: forearm and hand resolve by role; no upper arm
//   V2 three Witmotions: all three resolve; the upper arm is drawn once it is calibrated
//   V3 a wG3: forearm and hand are its UNIT objects, and the HackMotion branch is selected
//   V4 the dependency: a role reassigned at runtime, a late connect and a session exclusion
//      each re-resolve the view, and the legend's live dots follow
//   V5 the toolbar panel's chip shows the mount by name, and follows a reassignment
//
// Every case asserts in cleanup() that no unexpected warning was emitted.
import QtQuick
import QtTest
import PinPointStudio
import "fakes"

Item {
    id: root
    width: 1200
    height: 900

    Item { id: vizHost;   x: 0;   y: 0; width: 700; height: 600 }
    Item { id: panelHost; x: 720; y: 0; width: 380; height: 760 }

    TestCase {
        id: tc
        name: "SetupRolesViz"
        when: windowShown

        Component { id: imuMgrComp;    FakeImuManager {} }
        Component { id: camMgrComp;    FakeCameraManager {} }
        Component { id: athleteComp;   FakeAthlete {} }
        Component { id: liveWristComp; FakeLiveWrist {} }
        Component { id: sessionComp;   FakeSessionController {} }
        Component { id: navComp;       FakeNav {} }
        Component { id: lmComp;        FakeLaunchMonitor {} }
        Component { id: appLogComp;    FakeAppLog {} }

        property var fx: null
        property var imu: null
        property var view: null
        property var panel: null

        function findAll(item, pred, out) {
            out = out || []
            if (!item) return out
            if (pred(item)) out.push(item)
            var kids = item.children
            if (kids) for (var i = 0; i < kids.length; ++i) findAll(kids[i], pred, out)
            return out
        }
        function shownTexts(item) {
            var out = []
            findAll(item, function(o) {
                if (o.visible && typeof o.text === "string" && o.text !== "") out.push(o.text)
                return false
            })
            return out
        }
        function makeView() {
            view = harness.createWithContext("ArmVizView", fx, vizHost,
                                             { width: vizHost.width, height: vizHost.height })
            verify(view !== null, "ArmVizView failed to instantiate over the fakes")
            return view
        }
        // Witmotion `id` on `role`, connected; returns its instance.
        function witmotion(id, role, connect) {
            imu.addWitmotion(id, "")
            if (role) verify(imu.setRoleForDevice(id, role))
            return connect === false ? null : imu.connectDevice(id)
        }
        // The legend's delegates (a Row carrying `live`), as {label, live}; the label is the Row's Text.
        function legendRows() {
            return findAll(view, function(o) { return o.live !== undefined && o.spacing !== undefined })
        }
        function legend() {
            return legendRows().map(function(r) {
                var t = findAll(r, function(o) { return typeof o.text === "string" })
                return { label: t.length ? t[0].text : "", live: r.live }
            })
        }
        function q(a) { return [a.scalar, a.x, a.y, a.z].map(function(v) { return Math.round(v * 1e6) / 1e6 }) }

        function init() {
            testLog.reset()
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            appSettings.imuRoles = ({})
            fx = {
                imuManager:        createTemporaryObject(imuMgrComp, tc),
                cameraManager:     createTemporaryObject(camMgrComp, tc),
                athleteController: createTemporaryObject(athleteComp, tc),
                liveWrist:         createTemporaryObject(liveWristComp, tc),
                sessionController: createTemporaryObject(sessionComp, tc),
                navController:     createTemporaryObject(navComp, tc),
                launchMonitor:     createTemporaryObject(lmComp, tc),
                appLog:            createTemporaryObject(appLogComp, tc)
            }
            imu = fx.imuManager
        }
        function cleanup() {
            if (view) harness.destroyNow(view)
            if (panel) harness.destroyNow(panel)
            view = panel = null
            appSettings.imuRoles = ({})
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        // ── V1 ───────────────────────────────────────────────────────────────
        function test_V1_twoWitmotions() {
            var f = witmotion("WT-F", "leadForearm")
            var h = witmotion("WT-H", "leadHand")
            makeView()
            verify(view.imuLeadForearm === f, "forearm not resolved by role")
            verify(view.imuLeadHand === h, "hand not resolved by role")
            compare(view.imuLeadUpperArm, null)
            verify(!view.upperArmKnown)
            verify(!view.leadForearmIsHm && !view.leadHandIsHm && !view.leadUpperArmIsHm)
            compare(legend(), [ { label: "Lead forearm", live: true }, { label: "Lead hand", live: true },
                                { label: "Lead upper arm", live: false } ])
            var ts = shownTexts(view)
            verify(ts.indexOf("Lead forearm") >= 0 && ts.indexOf("Lead hand") >= 0
                   && ts.indexOf("Lead upper arm") >= 0, JSON.stringify(ts))
            for (var i = 0; i < ts.length; ++i) verify(!/\bSlot [A-D]\b/.test(ts[i]), ts[i])
        }

        // ── V2 ───────────────────────────────────────────────────────────────
        function test_V2_threeWitmotions() {
            var f = witmotion("WT-F", "leadForearm")
            var h = witmotion("WT-H", "leadHand")
            var u = witmotion("WT-U", "leadUpperArm")
            makeView()
            verify(view.imuLeadForearm === f && view.imuLeadHand === h && view.imuLeadUpperArm === u)
            verify(!view.upperArmKnown, "an uncalibrated upper arm is drawn")
            u.anatCalibrated = true
            verify(view.upperArmKnown, "a calibrated upper arm is not drawn")
            compare(legend().map(function(l) { return l.live }), [true, true, true])
        }

        // ── V3 ───────────────────────────────────────────────────────────────
        function test_V3_wg3UnitsAndHackMotionBranch() {
            imu.addHackMotion("HM-1", "")
            verify(imu.setRoleForDevice("HM-1", "leadForearm"))
            var dev = imu.connectDevice("HM-1")
            makeView()
            verify(view.imuLeadForearm === dev.unitLowerArm, "forearm is not the wG3's lower-arm unit")
            verify(view.imuLeadHand === dev.unitPalm, "hand is not the wG3's palm unit")
            compare(view.imuLeadUpperArm, null)
            verify(view.leadForearmIsHm && view.leadHandIsHm, "HackMotion branch not selected")
            verify(!view.leadUpperArmIsHm)
            // The branch is what puts the wG3's reference pose in front of a calibrated unit.
            dev.unitLowerArm.anatCalibrated = true
            dev.unitLowerArm.anatQuat = Qt.quaternion(1, 0, 0, 0)
            compare(q(view.quatApplyCalib(view.imuLeadForearm, Qt.quaternion(1, 0, 0, 0), view.leadForearmIsHm)),
                    q(view.hmReferenceQuat))
            // The legend asks the DEVICE, which is live for both of its roles.
            compare(legend().map(function(l) { return l.live }), [true, true, false])
        }

        // ── V4 ───────────────────────────────────────────────────────────────
        function test_V4_reResolvesAtRuntime() {
            var a = witmotion("WT-1", "leadForearm")
            var b = witmotion("WT-2", "leadHand")
            makeView()
            verify(view.imuLeadForearm === a && view.imuLeadHand === b)
            // A reassignment (roles map only).
            verify(imu.setRoleForDevice("WT-1", ""))
            compare(view.imuLeadForearm, null)
            verify(imu.setRoleForDevice("WT-2", "leadForearm"))
            verify(view.imuLeadForearm === b, "forearm did not follow the reassignment")
            compare(view.imuLeadHand, null)
            compare(legend().map(function(l) { return l.live }), [true, false, false])
            // A late connect (instances only): assigned first, connected after the view exists.
            witmotion("WT-3", "leadUpperArm", false)
            compare(view.imuLeadUpperArm, null)
            var c = imu.connectDevice("WT-3")
            verify(view.imuLeadUpperArm === c, "upper arm did not follow the connect")
            compare(legend().map(function(l) { return l.live }), [true, false, true])
            // A HackMotion swap: the Witmotion lets go of the forearm, the wG3 takes the pair.
            verify(imu.setRoleForDevice("WT-2", ""))
            imu.addHackMotion("HM-1", "")
            verify(imu.setRoleForDevice("HM-1", "leadForearm"))
            var dev = imu.connectDevice("HM-1")
            verify(view.imuLeadForearm === dev.unitLowerArm && view.imuLeadHand === dev.unitPalm)
            verify(view.leadForearmIsHm && view.leadHandIsHm)
            // A session exclusion (sessionImuExcluded only): the parked claim stays in the map,
            // and the ladder still answers with it (present but disabled beats nothing).
            imu.setSessionImuEnabled("HM-1", false)
            compare(appSettings.imuRoles["HM-1#lowerArm"], "leadForearm")
            compare(view.imuLeadForearm, null)   // deselected on disable → no live unit
            verify(view.leadForearmIsHm)
        }

        // ── V5 ───────────────────────────────────────────────────────────────
        function test_V5_toolbarChipShowsMountNames() {
            imu.addHackMotion("HM-1", "wG3")
            verify(imu.setRoleForDevice("HM-1", "leadForearm"))
            witmotion("WT-1", "leadUpperArm", false)
            witmotion("WT-2", "", false)
            panel = harness.createWithContext("PpImuPanel", fx, panelHost,
                                              { width: panelHost.width, height: panelHost.height })
            verify(panel !== null, "PpImuPanel failed to instantiate")
            compare(panel.mode, "list")
            var rowOf = function(id) {
                var r = findAll(panel, function(o) { return o.devId === id && o.placement !== undefined })
                return r.length === 1 ? r[0] : null
            }
            compare(rowOf("HM-1").placement, "Lead forearm + hand")
            compare(rowOf("WT-1").placement, "Lead upper arm")
            compare(rowOf("WT-2").placement, "")
            var ts = shownTexts(panel)
            verify(ts.indexOf("Lead forearm + hand") >= 0 && ts.indexOf("Lead upper arm") >= 0, JSON.stringify(ts))
            for (var i = 0; i < ts.length; ++i)
                verify(!/A \+ B|\bIMU [A-D]\b|\b[A-D] —/.test(ts[i]), "slot letter in toolbar text: " + ts[i])
            // A reassignment updates the chips without a device-list change.
            verify(imu.setRoleForDevice("WT-1", ""))
            verify(imu.setRoleForDevice("WT-2", "leadUpperArm"))
            compare(rowOf("WT-1").placement, "")
            compare(rowOf("WT-2").placement, "Lead upper arm")
            console.info("[V5] " + JSON.stringify(shownTexts(panel)))
        }
    }
}
