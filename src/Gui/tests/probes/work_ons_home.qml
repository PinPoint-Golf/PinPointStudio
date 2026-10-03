// END-TO-END: THE HOME SCREEN'S WORK ONS LIST.
//
// work_ons_test proves the reductions and work_ons_controller_test the catch-up; neither can
// see whether the home screen draws the list, opens a row, or offers the way into a session.
// This drives the real app:
//
//   wait for the catch-up → the list has rows → each row is drawn → opening one shows its
//   detail and the "review the session" link.
//
// Point the app at a folder of sessions that is NOT the library (PINPOINT_WORKONS_DIR), so the
// catch-up writes its work_ons.json files there:
//   PINPOINT_WORKONS_DIR=<athlete dir> QT_QPA_PLATFORM=offscreen PinPointStudio \
//       --probe-qml <abs path to this file> [--probe-min-rows 3] [--probe-png <abs path>]
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why. build/run-me/verify-workons.sh wraps it.

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
    readonly property int minRows: parseInt(arg("--probe-min-rows", "1"))
    readonly property string png: arg("--probe-png", "")

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
    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
    }
    function rootItem() { let t = probe; while (t.parent) t = t.parent; return t }

    property int waited: 0
    property double startedMs: Date.now()
    Timer {
        interval: 250; running: true; repeat: true
        onTriggered: {
            probe.waited += 1
            const done = workOns.athleteDir !== "" && !workOns.updating && workOns.sessionsFound > 0
            if (!done && probe.waited < 480) return
            running = false
            probe.inspect()
        }
    }

    function inspect() {
        log("athleteDir " + workOns.athleteDir + " · settled after " + (Date.now() - startedMs) + " ms")
        check(!workOns.updating, "the catch-up finished")
        check(workOns.sessionCount === workOns.sessionsFound && workOns.sessionsFound > 0,
              "every session has a record (" + workOns.sessionCount + " of " + workOns.sessionsFound + ")")

        const items = workOns.items
        for (let i = 0; i < items.length; ++i) {
            const it = items[i]
            log("  #" + (i + 1) + " " + it.name + " [" + it.status + "] " + it.countText)
            if (it.statusText)   log("       " + it.statusText)
            if (it.latestText)   log("       " + it.latestText)
            if (it.causedByText) log("       " + it.causedByText)
            if (it.drillLabel)   log("       drill: " + it.drillLabel)
            log("       review: " + it.sessionLabel + " · ticks " + it.ticks.map(t => t.state[0]).join(""))
        }
        log("cleared " + workOns.clearedCount)
        check(items.length >= minRows, "the list has at least " + minRows + " work-ons (" + items.length + ")")

        const rows = []
        walk(rootItem(), "workOnRow", rows)
        check(rows.length === Math.min(items.length, 5), "the home screen draws " + rows.length + " rows (five at most until 'more')")
        if (rows.length === 0) { finish(); return }

        const details = [], presses = [], links = []
        walk(rows[0], "workOnDetail", details)
        walk(rows[0], "workOnPress", presses)
        check(details.length === 1 && !details[0].visible, "a row starts closed")
        presses[0].clicked(null)
        check(details[0].visible && details[0].height > 0, "clicking it opens the detail in place (" + Math.round(details[0].height) + " px)")
        walk(rows[0], "workOnReview", links)
        check(links.length === 1 && links[0].visible && items[0].sessionDir !== "",
              "the detail offers the session to review: " + items[0].sessionDir)
        if (rows.length > 1) {
            const p2 = []
            walk(rows[1], "workOnPress", p2)
            p2[0].clicked(null)
            check(!details[0].visible, "opening another row closes the first")
            presses[0].clicked(null)
        }

        if (png !== "") {
            rows[0].parent.grabToImage(function (r) {
                log("image " + (r.saveToFile(png) ? png : "NOT SAVED"))
                finish()
            })
        } else finish()
    }
}
