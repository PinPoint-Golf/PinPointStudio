// Stage 3 — Settings ▸ IMUs on roles: the mount picker by name (design §4.12, §4.13, §8a).
//
// Hosts the REAL ImusPanel over FakeImuManager (whose setRoleForDevice follows
// imu_role_map.cpp claimRole rule for rule, roleRefused included) and the real AppSettings over
// a scratch QSettings. What the panel reads from its context, inventoried from the file:
//   imuManager  — imuDeviceList, instances, imuCount, imuScanActive, imuScanError, instanceFor,
//                 roleForDevice, setRoleForDevice, roleRefused (+ setImuAlias / setSelected /
//                 rescanImu / setOrientationFilter, only on presses these cases do not make)
//   appSettings — imuRoles, imuExcluded, imuOutputRateHz and the global IMU toggles (root context)
//   ImuMounts   — the singleton under test alongside the panel (labels, offered set)
// No sensor is connected here, so the test panel's ImuVizView never loads.
//
//   M1 a Witmotion row offers exactly the offered mounts + Unassigned, and no slot letter is
//      visible anywhere in the panel
//   M2 choosing a mount calls setRoleForDevice(id, role) once and the row shows it
//   M3 a mount held by another present, enabled sensor is greyed and names the holder
//   M4 a refused write snaps the combo back and the panel names the holder
//   M5 a wG3 offers Unassigned and "Lead forearm + hand" only; choosing it fills both roles, and
//      a Witmotion's forearm and hand entries are then greyed (no dual instrument on the arm)
//   M6 a sensor holding pelvis shows "Pelvis…" although pelvis is not offered
//   M7 the summary lists assigned and missing mounts by name
//   M8 flipping ImuMounts.trunkMountsOffered offers the trunk mounts (the one-line promise)
//   M9 a holder is never named by its raw "{…}" device id (Stage 5d): its seeded alias
//      ("<description> <id>") reads as the description, in the greyed entry and the refusal
//
// Every case asserts in cleanup() that no unexpected warning was emitted.
import QtQuick
import QtTest
import PinPointStudio
import "fakes"

Item {
    id: root
    width: 1100
    height: 1400

    Item { id: host; anchors.fill: parent }

    TestCase {
        id: tc
        name: "SetupMounts"
        when: windowShown

        Component { id: imuMgrComp; FakeImuManager {} }

        property var imu: null
        property var panel: null

        readonly property var armLabels: ["Lead forearm", "Lead hand", "Lead upper arm"]
        readonly property string pelvisLabel: "Pelvis (belt, sacrum)"
        readonly property string thoraxLabel: "Thorax (vest, upper back)"
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
            return findAll(panel, function(o) {
                return o.imuData !== undefined && o.mountLabel !== undefined && o.refusal !== undefined
            })
        }
        function rowFor(id) {
            var rs = rows()
            for (var i = 0; i < rs.length; ++i) if (rs[i].imuData.id === id) return rs[i]
            return null
        }
        function comboOf(row) {
            var c = findAll(row, function(o) { return o.placementOptions !== undefined && o.currentRole !== undefined })
            return c.length === 1 ? c[0] : null
        }
        function labels(combo) { return combo.placementOptions.map(function(o) { return o.label }) }
        function indexOfValue(combo, role) {
            for (var i = 0; i < combo.placementOptions.length; ++i)
                if (combo.placementOptions[i].value === role) return i
            return -1
        }
        // Every string the panel shows: visible items' `text`, plus every combo's option labels
        // (the popup rows exist only while it is open).
        function shownTexts() {
            var out = []
            findAll(panel, function(o) {
                if (!o.visible) return false
                if (typeof o.text === "string" && o.text !== "") out.push(o.text)
                if (typeof o.displayText === "string" && o.displayText !== "") out.push(o.displayText)
                if (o.placementOptions !== undefined) out = out.concat(labels(o))
                return false
            })
            return out
        }
        function textStartingWith(prefix) {
            var ts = shownTexts()
            for (var i = 0; i < ts.length; ++i) if (ts[i].indexOf(prefix) === 0) return ts[i]
            return null
        }
        function assertNoSlotLetters(where) {
            var ts = shownTexts()
            for (var i = 0; i < ts.length; ++i)
                for (var p = 0; p < slotPatterns.length; ++p)
                    verify(!slotPatterns[p].test(ts[i]),
                           where + ": slot letter in shown text \"" + ts[i] + "\" (" + slotPatterns[p] + ")")
        }
        function makePanel() {
            panel = harness.createWithContext("ImusPanel", { imuManager: imu }, host,
                                              { width: host.width, height: host.height })
            verify(panel !== null, "ImusPanel failed to instantiate over the fake")
            return panel
        }
        // Picks option `idx` the way a coach does: open the popup, click the row.
        function pick(combo, idx) {
            combo.popup.open()
            tryVerify(function() { return combo.popup.opened }, 3000, "combo popup never opened")
            var lv = combo.popup.contentItem
            var d = null
            tryVerify(function() { d = lv.itemAtIndex(idx); return d !== null && d.width > 0 }, 3000,
                      "popup row " + idx + " not built")
            // One frame for the overlay to place the popup; until then a click misses it.
            // (waitForRendering would cost its full timeout whenever nothing redraws.)
            waitForItemPolished(lv)
            wait(50)
            mouseClick(d)
            return d
        }
        function closePopup(combo) {
            if (combo.popup.opened) combo.popup.close()
            tryVerify(function() { return !combo.popup.visible }, 3000)
        }

        function init() {
            _t0 = testLog.elapsedMs()
            testLog.reset()
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            ImuMounts.trunkMountsOffered = false
            appSettings.imuRoles = ({})
            appSettings.imuAlias = ({})
            appSettings.imuExcluded = []
            imu = createTemporaryObject(imuMgrComp, tc)
        }
        property real _t0: 0
        function cleanup() {
            console.info("[mounts] took " + Math.round(testLog.elapsedMs() - _t0) + " ms")
            if (panel) harness.destroyNow(panel)
            panel = null
            ImuMounts.trunkMountsOffered = false
            appSettings.imuRoles = ({})
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        // ── M1 ───────────────────────────────────────────────────────────────
        function test_M1_witmotionOffersOfferedMountsNoLetters() {
            imu.addWitmotion("WT-1", "Wrist sensor")
            imu.addWitmotion("WT-2", "")
            makePanel()
            compare(rows().length, 2)
            var c = comboOf(rowFor("WT-1"))
            verify(c !== null)
            compare(labels(c), ["— Unassigned —"].concat(armLabels))
            compare(c.placementOptions.map(function(o) { return o.value }),
                    ["", "leadForearm", "leadHand", "leadUpperArm"])
            compare(c.currentIndex, 0)
            compare(c.displayText, "— Unassigned —")
            for (var i = 0; i < c.count; ++i) verify(c.itemEnabledFn(i), "option " + i + " greyed with nothing assigned")
            assertNoSlotLetters("M1, nothing assigned")
            // …and with mounts held, including the wG3-shaped pair that used to read "A + B".
            imu.addHackMotion("HM-1", "wG3")
            verify(imu.setRoleForDevice("HM-1", "leadForearm"))
            verify(imu.setRoleForDevice("WT-1", "leadUpperArm"))
            tryVerify(function() { return rowFor("HM-1") !== null })
            assertNoSlotLetters("M1, three mounts held")
            console.info("[M1] " + JSON.stringify(shownTexts().filter(function(t) { return /Lead|Missing|Assigned|Live test/.test(t) }))
)
        }

        // ── M2 ───────────────────────────────────────────────────────────────
        function test_M2_choosingAMountWritesTheRole() {
            imu.addWitmotion("WT-1", "Wrist sensor")
            makePanel()
            var row = rowFor("WT-1"), c = comboOf(row)
            var idx = indexOfValue(c, "leadHand")
            pick(c, idx)
            tryCompare(c.popup, "visible", false)
            var calls = imu.callsNamed("setRoleForDevice")
            compare(calls.length, 1, JSON.stringify(calls))
            compare(calls[0].args, ["WT-1", "leadHand"])
            compare(appSettings.imuRoles["WT-1"], "leadHand")
            compare(c.currentIndex, idx)
            compare(c.displayText, "Lead hand")
            compare(row.mountLabel, "Lead hand")
            compare(textStartingWith("Live test — "), "Live test — Lead hand — WT901BLE WT-1")
            // Back to Unassigned through the same path.
            pick(c, 0)
            tryCompare(c.popup, "visible", false)
            compare(imu.countCalls("setRoleForDevice"), 2)
            compare(appSettings.imuRoles["WT-1"], undefined)
            compare(c.displayText, "— Unassigned —")
            compare(textStartingWith("Live test — "), "Live test — WT901BLE WT-1")
        }

        // ── M3 ───────────────────────────────────────────────────────────────
        function test_M3_heldMountIsGreyedAndNamesTheHolder() {
            imu.addWitmotion("WT-1", "Wrist sensor")
            imu.addWitmotion("WT-2", "")
            verify(imu.setRoleForDevice("WT-1", "leadForearm"))
            makePanel()
            var c = comboOf(rowFor("WT-2"))
            var idx = indexOfValue(c, "leadForearm")
            compare(labels(c)[idx], "Lead forearm — held by Wrist sensor")
            verify(!c.itemEnabledFn(idx), "a held mount is offered as if free")
            verify(c.itemEnabledFn(indexOfValue(c, "leadHand")))
            // The holder's own row offers it plainly, as its current choice.
            var own = comboOf(rowFor("WT-1"))
            compare(labels(own)[indexOfValue(own, "leadForearm")], "Lead forearm")
            compare(own.displayText, "Lead forearm")
            // The greyed row is inert in the real popup: a click writes nothing.
            var before = imu.countCalls("setRoleForDevice")
            var d = pick(c, idx)
            verify(!d.enabled, "the popup row for a held mount is enabled")
            closePopup(c)
            compare(imu.countCalls("setRoleForDevice"), before)
            compare(c.currentIndex, 0)
            // No alias → the holder is named by its description. (A device-list change rebuilds
            // the rows, as the real QVariantList model does, so the combo is found again.)
            imu._devices[0].alias = ""
            imu._touchDevices()
            c = comboOf(rowFor("WT-2"))
            compare(labels(c)[idx], "Lead forearm — held by WT901BLE WT-1")
            // Disabled for the session, the holder no longer blocks (the manager's own rule).
            imu.setSessionImuEnabled("WT-1", false)
            c = comboOf(rowFor("WT-2"))
            verify(c.itemEnabledFn(idx), "a disabled holder still greys the mount")
            compare(labels(c)[idx], "Lead forearm")
        }

        // ── M9 ───────────────────────────────────────────────────────────────
        function test_M9_holderNeverNamedByARawId() {
            var uuid = "{767a6a67-14b5-1705-bb58-9ebea6e89014}"
            imu.addWitmotion(uuid, "")                      // description "WT901BLE {…}"
            imu.addWitmotion("WT-2", "")
            var al = {}
            al["WT901BLE " + uuid + "|" + uuid] = "WT901BLE " + uuid + " " + uuid   // the seeded alias
            appSettings.imuAlias = al
            verify(imu.setRoleForDevice(uuid, "leadForearm"))
            makePanel()
            var c = comboOf(rowFor("WT-2"))
            var idx = indexOfValue(c, "leadForearm")
            compare(labels(c)[idx], "Lead forearm — held by WT901BLE")
            verify(!imu.setRoleForDevice("WT-2", "leadForearm"))      // refused → the panel names the holder
            compare(rowFor("WT-2").refusal, "Lead forearm is held by WT901BLE. Unassign or disable it first.")
            var re = /\{[0-9a-f-]{36}\}/i
            var holderTexts = shownTexts().filter(function(t) { return t.indexOf("held by") >= 0 })
            verify(holderTexts.length >= 2, JSON.stringify(holderTexts))
            holderTexts.forEach(function(t) { verify(!re.test(t), "a raw device id in \"" + t + "\"") })
        }

        // ── M4 ───────────────────────────────────────────────────────────────
        // The greying makes a refusal rare (it takes a holder that appears between the popup
        // opening and the click), so the activation is driven directly: what is under test is
        // what the panel does when the manager says no.
        function test_M4_refusedWriteSnapsBackAndNamesTheHolder() {
            imu.addWitmotion("WT-1", "Wrist sensor")
            imu.addWitmotion("WT-2", "")
            verify(imu.setRoleForDevice("WT-2", "leadUpperArm"))
            verify(imu.setRoleForDevice("WT-1", "leadForearm"))
            makePanel()
            var row = rowFor("WT-2"), c = comboOf(row)
            compare(c.displayText, "Lead upper arm")
            var spy = createTemporaryQmlObject("import QtTest; SignalSpy { signalName: \"roleRefused\" }", tc)
            spy.target = imu
            var idx = indexOfValue(c, "leadForearm")
            c.currentIndex = idx
            c.activated(idx)
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], "WT-2")
            compare(spy.signalArguments[0][1], "leadForearm")
            compare(spy.signalArguments[0][2], "WT-1")
            compare(appSettings.imuRoles["WT-2"], "leadUpperArm", "a refused write changed the map")
            compare(c.currentIndex, indexOfValue(c, "leadUpperArm"), "the combo did not snap back")
            compare(c.displayText, "Lead upper arm")
            compare(row.refusal, "Lead forearm is held by Wrist sensor. Unassign or disable it first.")
            compare(textStartingWith("Lead forearm is held by"),
                    "Lead forearm is held by Wrist sensor. Unassign or disable it first.")
            // The note goes once the map moves (here: the holder lets go).
            verify(imu.setRoleForDevice("WT-1", ""))
            compare(row.refusal, "")
            compare(textStartingWith("Lead forearm is held by"), null)
        }

        // ── M5 ───────────────────────────────────────────────────────────────
        function test_M5_wg3OffersThePairAndBlocksTheArm() {
            imu.addHackMotion("HM-1", "wG3 strap")
            imu.addWitmotion("WT-1", "Wrist sensor")
            makePanel()
            var hc = comboOf(rowFor("HM-1"))
            compare(labels(hc), ["— Unassigned —", "Lead forearm + hand"])
            pick(hc, 1)
            tryCompare(hc.popup, "visible", false)
            var calls = imu.callsNamed("setRoleForDevice")
            compare(calls.length, 1)
            compare(calls[0].args, ["HM-1", "leadForearm"])
            compare(appSettings.imuRoles["HM-1#lowerArm"], "leadForearm")
            compare(appSettings.imuRoles["HM-1#palm"], "leadHand")
            compare(hc.displayText, "Lead forearm + hand")
            compare(rowFor("HM-1").mountLabel, "Lead forearm + hand")
            compare(textStartingWith("Live test — "), "Live test — Lead forearm + hand — HackMotion wG3 HM-1")
            var wc = comboOf(rowFor("WT-1"))
            var fi = indexOfValue(wc, "leadForearm"), hi = indexOfValue(wc, "leadHand"), ui = indexOfValue(wc, "leadUpperArm")
            verify(!wc.itemEnabledFn(fi), "forearm free on a Witmotion while the wG3 holds it")
            verify(!wc.itemEnabledFn(hi), "hand free on a Witmotion while the wG3 holds it")
            verify(wc.itemEnabledFn(ui))
            compare(labels(wc)[fi], "Lead forearm — held by wG3 strap")
            compare(labels(wc)[hi], "Lead hand — held by wG3 strap")
            // And the reverse: a Witmotion on the hand greys the wG3's only choice.
            verify(imu.setRoleForDevice("HM-1", ""))
            verify(imu.setRoleForDevice("WT-1", "leadHand"))
            verify(!hc.itemEnabledFn(1), "the wG3 pair is free while a Witmotion holds the hand")
            compare(labels(hc)[1], "Lead forearm + hand — held by Wrist sensor")
        }

        // ── M6 ───────────────────────────────────────────────────────────────
        function test_M6_heldTrunkMountShownTruthfully() {
            imu.addWitmotion("WT-1", "Belt sensor")
            appSettings.imuRoles = ({ "WT-1": "pelvis" })   // as a hand-edited settings file would
            makePanel()
            verify(!ImuMounts.isOffered("pelvis"))
            var row = rowFor("WT-1"), c = comboOf(row)
            compare(c.displayText, pelvisLabel)
            compare(row.mountLabel, pelvisLabel)
            compare(labels(c), ["— Unassigned —"].concat(armLabels).concat([pelvisLabel]))
            compare(textStartingWith("Live test — "), "Live test — " + pelvisLabel + " — WT901BLE WT-1")
            // Unassigning works and the not-offered mount leaves the list with it.
            pick(c, 0)
            tryCompare(c.popup, "visible", false)
            compare(appSettings.imuRoles["WT-1"], undefined)
            compare(labels(c), ["— Unassigned —"].concat(armLabels))
            compare(c.displayText, "— Unassigned —")
        }

        // ── M7 ───────────────────────────────────────────────────────────────
        function test_M7_summaryByName() {
            imu.addWitmotion("WT-1", "")
            imu.addWitmotion("WT-2", "")
            makePanel()
            compare(textStartingWith("No placements assigned"), "No placements assigned")
            compare(textStartingWith("Missing: "), "Missing: Lead forearm · Lead hand · Lead upper arm")
            verify(imu.setRoleForDevice("WT-2", "leadHand"))
            verify(imu.setRoleForDevice("WT-1", "leadForearm"))
            compare(textStartingWith("Assigned: "), "Assigned: Lead forearm · Lead hand")
            compare(textStartingWith("Missing: "), "Missing: Lead upper arm")
            verify(imu.setRoleForDevice("WT-1", "leadUpperArm"))
            compare(textStartingWith("Assigned: "), "Assigned: Lead hand · Lead upper arm")
            compare(textStartingWith("Missing: "), "Missing: Lead forearm")
            verify(imu.setRoleForDevice("WT-1", "leadForearm"))
            imu.addWitmotion("WT-3", "")
            verify(imu.setRoleForDevice("WT-3", "leadUpperArm"))
            compare(textStartingWith("Assigned: "), "Assigned: Lead forearm · Lead hand · Lead upper arm")
            compare(textStartingWith("Missing: "), null)
            // A held trunk role is listed as assigned; it is not "missing" when unheld (not offered).
            appSettings.imuRoles = ({ "WT-1": "leadForearm", "WT-2": "thorax" })
            compare(textStartingWith("Assigned: "), "Assigned: Lead forearm · " + thoraxLabel)
            compare(textStartingWith("Missing: "), "Missing: Lead hand · Lead upper arm")
        }

        // ── M8 ───────────────────────────────────────────────────────────────
        function test_M8_theOneSwitchOffersTheTrunk() {
            imu.addWitmotion("WT-1", "")
            imu.addHackMotion("HM-1", "")
            makePanel()
            var c = comboOf(rowFor("WT-1")), hc = comboOf(rowFor("HM-1"))
            compare(labels(c), ["— Unassigned —"].concat(armLabels))
            compare(textStartingWith("Missing: "), "Missing: Lead forearm · Lead hand · Lead upper arm")
            ImuMounts.trunkMountsOffered = true
            compare(labels(c), ["— Unassigned —"].concat(armLabels).concat([pelvisLabel, thoraxLabel]))
            compare(textStartingWith("Missing: "),
                    "Missing: Lead forearm · Lead hand · Lead upper arm · " + pelvisLabel + " · " + thoraxLabel)
            // The wG3 still has exactly its one assignment: its cable decides, not the offered set.
            compare(labels(hc), ["— Unassigned —", "Lead forearm + hand"])
            // And a trunk mount can then be chosen like any other.
            pick(c, indexOfValue(c, "thorax"))
            tryCompare(c.popup, "visible", false)
            compare(appSettings.imuRoles["WT-1"], "thorax")
            compare(c.displayText, thoraxLabel)
            assertNoSlotLetters("M8")
        }
    }
}
