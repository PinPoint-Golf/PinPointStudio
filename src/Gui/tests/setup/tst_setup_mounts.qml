// Mounts by name: set in ONE place, shown everywhere (design §4.12, §4.13; trunk design §16.2 W2.2).
//
// Since Mark's call of 26 Oct the Settings ▸ IMUs Placement combo is gone: each Settings row shows its
// mount as a read-only line, and session setup's Sensors page (pages/ImusPage.qml) is the one place a
// mount is set or changed. The combo's cases (Stage 3's M1–M9) moved with it — to the picker, here
// and in tst_setup_sensors.qml (MQ1 offered mounts, MQ2 one write, MQ3 greyed holder, MQ4 refusal,
// MQ5 the wG3's pair, MQ7 change, AN5 a holder never named by its id).
//
// Settings hosts the REAL ImusPanel over the SetupDriver's FakeImuManager and the real AppSettings
// over a scratch QSettings; the picker is the real shell (support/SetupDriver.qml). No sensor is
// connected on the Settings side, so the panel's ImuVizView never loads.
//
//   S1 Settings: every row shows "Worn as: <mount> (set in session setup)" or "Not worn (set in
//      session setup)" — a Witmotion, a wG3's pair, a trunk mount — and no row offers a picker
//   S2 the line follows the roles map live (a mount set or taken away in setup reads at once)
//   S3 the summary lists the worn and the unworn mounts by name, the trunk's included
//   S4 no slot letter anywhere in the panel
//   P1 the picker offers every offered mount — the arm's and the trunk's, by their §4.1 names — and a
//      wG3 only its pair; no slot letter (was M1 + M8)
//   P2 choosing the two trunk mounts writes each once and brings the trunk steps (was M8)
//   P3 "Not worn" takes a mount away (was the combo's Unassigned): offered only to a mounted sensor
//   P4 a held mount the pickers do not offer is still shown, in Settings and in the picker row (was M6)
//
// Every case asserts in cleanup() that no unexpected warning was emitted.
import QtQuick
import QtTest
import PinPointStudio
import "support"

Item {
    id: root
    width: 1400
    height: 1400

    readonly property var _testLog: testLog

    Item { id: host; width: 1400; height: 900 }
    Item { id: panelHost; y: 900; width: 1100; height: 1400 }
    SetupDriver { id: drv; host: host; testCase: tc; testLog: root._testLog }

    TestCase {
        id: tc
        name: "SetupMounts"
        when: windowShown

        readonly property var d: drv
        readonly property int wrist: SessionController.Wrist
        property var panel: null

        readonly property var armLabels: ["Lead forearm", "Lead hand", "Lead upper arm"]
        readonly property string hipLabel:   "Hip sensor (belt, lower back)"
        readonly property string chestLabel: "Chest sensor (vest, between the shoulder blades)"
        // The Settings summary's one-line lists use the short names (ImuMounts.shortLabel).
        readonly property string hipShort:   "Hip sensor"
        readonly property string chestShort: "Chest sensor"
        readonly property var slotPatterns: [/\b[A-D] —/, /A \+ B/, /\bSlot [A-D]\b/, /\bIMU [A-D]\b/]

        // ── Helpers ──────────────────────────────────────────────────────────
        function findAll(item, pred, out) {
            out = out || []
            if (!item) return out
            if (pred(item)) out.push(item)
            var kids = item.children
            if (kids) for (var i = 0; i < kids.length; ++i) findAll(kids[i], pred, out)
            return out
        }
        function rows() {
            return findAll(panel, function(o) { return o.imuData !== undefined && o.mountLine !== undefined })
        }
        function rowFor(id) {
            var rs = rows()
            for (var i = 0; i < rs.length; ++i) if (rs[i].imuData.id === id) return rs[i]
            return null
        }
        // The line the row SHOWS (the Text, not the property behind it).
        function mountLineShown(id) {
            var t = findAll(rowFor(id), function(o) { return o.objectName === "imuMountLine" })
            return t.length === 1 && t[0].visible ? t[0].text : null
        }
        function shownTexts() {
            var out = []
            findAll(panel, function(o) {
                if (!o.visible) return false
                if (typeof o.text === "string" && o.text !== "") out.push(o.text)
                if (typeof o.displayText === "string" && o.displayText !== "") out.push(o.displayText)
                return false
            })
            return out
        }
        function textStartingWith(prefix) {
            var ts = shownTexts()
            for (var i = 0; i < ts.length; ++i) if (ts[i].indexOf(prefix) === 0) return ts[i]
            return null
        }
        function makePanel() {
            panel = harness.createWithContext("ImusPanel", { imuManager: d.imu }, panelHost,
                                              { width: panelHost.width, height: panelHost.height })
            verify(panel !== null, "ImusPanel failed to instantiate over the fake")
            return panel
        }
        function setRoleCalls() { return d.imu.callsNamed("setRoleForDevice") }

        function init() {
            testLog.reset()
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            ImuMounts.trunkMountsOffered = true
            d.setUp()
        }
        function cleanup() {
            if (panel) harness.destroyNow(panel)
            panel = null
            d.tearDown()
            ImuMounts.trunkMountsOffered = true
            appSettings.imuRoles = ({})
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        // ── S1 ───────────────────────────────────────────────────────────────
        function test_S1_settingsShowsTheMountReadOnly() {
            d.imu.addWitmotion("WT-1", "Wrist sensor")
            d.imu.addWitmotion("WT-2", "")
            d.imu.addWitmotion("WT-P", "")
            d.imu.addHackMotion("HM-1", "wG3")
            verify(d.imu.setRoleForDevice("WT-1", "leadUpperArm"))
            verify(d.imu.setRoleForDevice("WT-P", "pelvis"))
            verify(d.imu.setRoleForDevice("HM-1", "leadForearm"))
            makePanel()
            compare(rows().length, 4)
            compare(mountLineShown("WT-1"), "Worn as: Lead upper arm (set in session setup)")
            compare(mountLineShown("WT-2"), "Not worn (set in session setup)")
            compare(mountLineShown("WT-P"), "Worn as: " + hipLabel + " (set in session setup)")
            compare(mountLineShown("HM-1"), "Worn as: Lead forearm + hand (set in session setup)")
            // Nothing in a row can set a mount: no picker, no Unassigned entry.
            compare(findAll(panel, function(o) { return o.placementOptions !== undefined || o.currentRole !== undefined }).length,
                    0, "a mount picker is left in Settings")
            verify(shownTexts().every(function(t) { return t.indexOf("Unassigned") < 0 }), "an Unassigned entry in Settings")
            // The Live test heading still names the mount.
            verify(shownTexts().indexOf("Live test — " + hipLabel + " — WT901BLE WT-P") >= 0,
                   JSON.stringify(shownTexts().filter(function(t) { return t.indexOf("Live test") === 0 })))
        }

        // ── S2 ───────────────────────────────────────────────────────────────
        function test_S2_lineFollowsTheRolesMap() {
            d.imu.addWitmotion("WT-1", "")
            makePanel()
            compare(mountLineShown("WT-1"), "Not worn (set in session setup)")
            verify(d.imu.setRoleForDevice("WT-1", "thorax"))
            compare(mountLineShown("WT-1"), "Worn as: " + chestLabel + " (set in session setup)")
            verify(d.imu.setRoleForDevice("WT-1", "leadHand"))
            compare(mountLineShown("WT-1"), "Worn as: Lead hand (set in session setup)")
            verify(d.imu.setRoleForDevice("WT-1", ""))
            compare(mountLineShown("WT-1"), "Not worn (set in session setup)")
            // Settings wrote nothing.
            compare(setRoleCalls().length, 3)
        }

        // ── S3 ───────────────────────────────────────────────────────────────
        function test_S3_summaryByName() {
            d.imu.addWitmotion("WT-1", "")
            d.imu.addWitmotion("WT-2", "")
            makePanel()
            compare(textStartingWith("No placements assigned"), "No placements assigned")
            compare(textStartingWith("Not worn: "),
                    "Not worn: Lead forearm · Lead hand · Lead upper arm · " + hipShort + " · " + chestShort)
            verify(d.imu.setRoleForDevice("WT-2", "pelvis"))
            verify(d.imu.setRoleForDevice("WT-1", "thorax"))
            compare(textStartingWith("Assigned: "), "Assigned: " + hipShort + " · " + chestShort)
            compare(textStartingWith("Not worn: "), "Not worn: Lead forearm · Lead hand · Lead upper arm")
            appSettings.imuRoles = ({ "WT-1": "leadForearm", "WT-2": "leadHand", "X": "leadUpperArm",
                                      "Y": "pelvis", "Z": "thorax" })
            compare(textStartingWith("Not worn: "), null)
        }

        // ── S4 ───────────────────────────────────────────────────────────────
        function test_S4_noSlotLetters() {
            d.imu.addWitmotion("WT-1", "Wrist sensor")
            d.imu.addHackMotion("HM-1", "wG3")
            verify(d.imu.setRoleForDevice("HM-1", "leadForearm"))
            verify(d.imu.setRoleForDevice("WT-1", "leadUpperArm"))
            makePanel()
            var ts = shownTexts()
            for (var i = 0; i < ts.length; ++i)
                for (var p = 0; p < slotPatterns.length; ++p)
                    verify(!slotPatterns[p].test(ts[i]), "slot letter in shown text \"" + ts[i] + "\"")
        }

        // ── P1 ───────────────────────────────────────────────────────────────
        function test_P1_pickerOffersArmAndTrunkByName() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addWitmotion("WT-N", "", true)
            d.imu.addHackMotion("HM-1", "")
            d.imu.connectDevice("HM-1")
            d.open(wrist)
            d.walkTo("imus")
            compare(d.mountOptions("WT-N").map(function(o) { return o.text }),
                    armLabels.concat([hipLabel, chestLabel]))
            compare(d.mountOptions("WT-N").map(function(o) { return o.role }),
                    ["leadForearm", "leadHand", "leadUpperArm", "pelvis", "thorax"])
            compare(d.mountOptions("HM-1").map(function(o) { return o.text }), ["Lead forearm + hand"])
            var all = d.mountOptions("WT-N").concat(d.mountOptions("HM-1")).map(function(o) { return o.text })
            for (var i = 0; i < all.length; ++i)
                for (var p = 0; p < slotPatterns.length; ++p)
                    verify(!slotPatterns[p].test(all[i]), "slot letter in option \"" + all[i] + "\"")
        }

        // ── P2 ───────────────────────────────────────────────────────────────
        function test_P2_trunkMountsBringTheTrunkSteps() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addWitmotion("WT-P", "", true)
            d.addWitmotion("WT-T", "", true)
            d.open(wrist)
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "imus", "ready"])
            d.walkTo("imus")
            var n0 = setRoleCalls().length
            d.chooseMount("WT-P", "pelvis")
            // The chest mount is greyed on no one: the hip sensor holds only its own.
            verify(!d.mountOptions("WT-T").filter(function(o) { return o.role === "thorax" })[0].taken)
            compare(d.mountOptions("WT-T").filter(function(o) { return o.role === "pelvis" })[0].text,
                    hipLabel + " — held by WT901BLE WT-P")
            d.chooseMount("WT-T", "thorax")
            var calls = setRoleCalls().slice(n0)
            compare(calls.map(function(c) { return c.args }), [["WT-P", "pelvis"], ["WT-T", "thorax"]])
            compare(d.imuRows()[0].label, hipLabel + " — WT901BLE WT-P")
            compare(d.imuRows()[1].label, chestLabel + " — WT901BLE WT-T")
            compare(d.plan(), ["goals", "cameras", "framing", "ball", "imus", "calibrateTrunk", "checkTrunk", "ready"])
            verify(d.ctx.groups.trunk.complete)
        }

        // ── P3 ───────────────────────────────────────────────────────────────
        function test_P3_notWornTakesTheMountAway() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addWitmotion("WT-N", "", true)
            d.addWitmotion("WT-A", "A", true)
            d.open(wrist)
            d.walkTo("imus")
            // Not offered to a sensor with no mount to take away.
            verify(d.mountOptions("WT-N").every(function(o) { return o.role !== "" }), "Not worn offered unmounted")
            d.clickChangeMount("WT-A")
            var nw = d.mountOptions("WT-A").filter(function(o) { return o.role === "" })
            compare(nw.length, 1)
            compare(nw[0].text, "Not worn")
            var n0 = setRoleCalls().length
            d.chooseMount("WT-A", "")
            compare(setRoleCalls().slice(n0).map(function(c) { return c.args }), [["WT-A", ""]])
            compare(appSettings.imuRoles["WT-A"], undefined)
            compare(d.imuRows()[1].sub, "NO MOUNT — IT SITS OUT THIS SESSION")
            verify(!d.plan().some(function(k) { return k === "calibrateArm" }), "arm steps with no arm mount")
            // …and Settings reads it.
            makePanel()
            compare(mountLineShown("WT-A"), "Not worn (set in session setup)")
        }

        // ── P4 ───────────────────────────────────────────────────────────────
        function test_P4_heldMountNotOfferedIsStillShown() {
            ImuMounts.trunkMountsOffered = false
            d.imu.addWitmotion("WT-1", "Belt sensor")
            appSettings.imuRoles = ({ "WT-1": "pelvis" })   // as a hand-edited settings file would
            verify(!ImuMounts.isOffered("pelvis"))
            makePanel()
            compare(mountLineShown("WT-1"), "Worn as: " + hipLabel + " (set in session setup)")
            compare(textStartingWith("Assigned: "), "Assigned: " + hipShort)
            d.imu.connectDevice("WT-1")
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.open(wrist)
            d.walkTo("imus")
            compare(d.imuRows()[0].label, hipLabel + " — Belt sensor")
        }
    }
}
