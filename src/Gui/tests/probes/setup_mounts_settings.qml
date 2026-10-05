// END-TO-END: SETTINGS ▸ IMUs ON ROLES (session-setup refactor, Stage 3).
//
// tst_setup_mounts.qml proves the mount picker over a fake manager; it cannot see the real
// ImuManager, the real settings file, or what this build's first launch did to them (the
// one-time imu/placement → imu/roles migration). This drives the real app:
//
//   wait for the IMU list → open Settings ▸ IMUs → log the stored roles, the legacy placement
//   view and the manager's rolesInSession → for every device row, log the mount it shows, its
//   combo's option labels with their enabled state, and its "Live test" heading → log the
//   summary → check no shown text carries a slot letter.
//
// ⚠ READ-ONLY. It presses nothing: no Scan, no Connect, no Test, no mount pick, no calibration,
// so it connects to no sensor and plays no sound. It writes nothing to settings.
//
// Run it against a COPY of the settings file first (Qt reads $XDG_CONFIG_HOME/PinPointStudio/
// PinPointStudio.ini when XDG_CONFIG_HOME is set — ppSettings() is IniFormat/UserScope):
//   mkdir -p /tmp/pps-cfg/PinPointStudio && cp ~/.config/PinPointStudio/PinPointStudio.ini /tmp/pps-cfg/PinPointStudio/
//   XDG_CONFIG_HOME=/tmp/pps-cfg PINPOINT_LOG_STDERR=1 PinPointStudio --probe-qml <abs path to this file> [--probe-wait-ms 8000]
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why.

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    // How long to wait for the enumerator to list known sensors before inspecting anyway.
    readonly property int waitMs: parseInt(arg("--probe-wait-ms", "8000"))

    readonly property var slotPatterns: [/\b[A-D] —/, /A \+ B/, /\bSlot [A-D]\b/, /\bIMU [A-D]\b/]

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
    }
    function finish() {
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }
    function findAll(item, pred, out) {
        out = out || []
        if (!item) return out
        if (pred(item)) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) findAll(kids[i], pred, out)
        return out
    }
    function rootItem() { let t = probe; while (t.parent) t = t.parent; return t }
    function json(v) { return JSON.stringify(v) }

    property double startedMs: Date.now()
    property int stage: 0
    property var settingsScreen: null
    property var panel: null

    Timer {
        id: tick
        interval: 250; running: true; repeat: true
        onTriggered: probe.step()
    }

    function step() {
        const elapsed = Date.now() - startedMs
        if (stage === 0) {
            // The enumerator lists remembered sensors without a scan; give it a moment.
            if (imuManager.imuDeviceList.length === 0 && elapsed < waitMs) return
            log("imuDeviceList " + imuManager.imuDeviceList.length + " device(s) after " + elapsed + " ms")
            navController.navigateRail(9)   // Main.qml screenSettings
            stage = 1
            return
        }
        if (stage === 1) {
            const s = findAll(rootItem(), function(o) {
                return o.activeNavIndex !== undefined && o.navigateToResult !== undefined
            })
            if (s.length === 0) { if (elapsed < waitMs + 5000) return; check(false, "the Settings screen exists"); tick.running = false; finish(); return }
            settingsScreen = s[0]
            settingsScreen.activeNavIndex = 4   // IMUs (ScreenSettings.qml nav list)
            stage = 2
            return
        }
        if (stage === 2) {
            const p = findAll(settingsScreen, function(o) {
                return o.openTestId !== undefined && o.scrollToItem !== undefined && o.visible
            })
            if (p.length === 0) { if (elapsed < waitMs + 8000) return; check(false, "Settings ▸ IMUs is shown"); tick.running = false; finish(); return }
            panel = p[0]
            stage = 3
            return
        }
        tick.running = false
        inspect()
    }

    function inspect() {
        check(true, "Settings ▸ IMUs is shown")
        log("appSettings.imuRoles      " + json(appSettings.imuRoles))
        log("appSettings.imuPlacement  " + json(appSettings.imuPlacement) + "  (legacy view, derived)")
        log("imuManager.rolesInSession " + json(imuManager.rolesInSession))
        log("ImuMounts.offeredRoles    " + json(ImuMounts.offeredRoles)
            + " (trunkMountsOffered " + ImuMounts.trunkMountsOffered + ")")

        const list = imuManager.imuDeviceList
        for (let i = 0; i < list.length; ++i) {
            const d = list[i]
            log("device " + d.id + " [" + d.vendor + "] alias '" + d.alias + "' present " + d.present
                + " sessionEnabled " + d.sessionEnabled + " roleForDevice '" + imuManager.roleForDevice(d.id) + "'")
        }

        const rows = findAll(panel, function(o) {
            return o.imuData !== undefined && o.mountLabel !== undefined && o.refusal !== undefined
        })
        check(rows.length === list.length, "one row per enumerated device (" + rows.length + " of " + list.length + ")")
        for (let r = 0; r < rows.length; ++r) {
            const row = rows[r]
            const combos = findAll(row, function(o) { return o.placementOptions !== undefined && o.currentRole !== undefined })
            if (combos.length !== 1) { check(false, "row " + row.imuData.id + " has one mount picker"); continue }
            const c = combos[0]
            log("row " + row.imuData.id + " shows '" + c.displayText + "' (currentRole '" + c.currentRole
                + "', mountLabel '" + row.mountLabel + "')")
            for (let k = 0; k < c.placementOptions.length; ++k)
                log("    option " + k + " '" + c.placementOptions[k].label + "' value '" + c.placementOptions[k].value
                    + "' " + (c.itemEnabledFn(k) ? "enabled" : "GREYED"))
            const expected = row.mountLabel !== "" ? row.mountLabel : ImuMounts.unassignedLabel
            check(c.displayText === expected,
                  "row " + row.imuData.id + " shows its mount: '" + c.displayText + "' vs '" + expected + "'")
            const live = findAll(row, function(o) { return typeof o.text === "string" && o.text.indexOf("Live test — ") === 0 })
            if (live.length > 0) log("    heading '" + live[0].text + "'")
        }

        const summary = findAll(panel, function(o) {
            return o.visible && typeof o.text === "string"
                   && (o.text.indexOf("Assigned: ") === 0 || o.text.indexOf("Missing: ") === 0
                       || o.text === "No placements assigned")
        })
        for (let m = 0; m < summary.length; ++m) log("summary '" + summary[m].text + "'")

        // Every shown string, plus every picker's option labels (the popup rows exist only open).
        const shown = []
        findAll(panel, function(o) {
            if (!o.visible) return false
            if (typeof o.text === "string" && o.text !== "") shown.push(o.text)
            if (typeof o.displayText === "string" && o.displayText !== "") shown.push(o.displayText)
            if (o.placementOptions !== undefined)
                for (let k = 0; k < o.placementOptions.length; ++k) shown.push(o.placementOptions[k].label)
            return false
        })
        const offenders = shown.filter(function(t) {
            for (let p = 0; p < slotPatterns.length; ++p) if (slotPatterns[p].test(t)) return true
            return false
        })
        log(shown.length + " shown strings checked")
        check(offenders.length === 0, "no shown text carries a slot letter" + (offenders.length ? ": " + json(offenders) : ""))
        finish()
    }
}
