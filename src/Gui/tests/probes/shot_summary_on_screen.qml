/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// THE CAROUSEL SHOT SUMMARY ON SCREEN (refactor 4-b): the real app, headless, drives the session screen's carousel
// through app.state as the carousel does, and grabs the carousel at each state:
//   nothing     the session loaded, nothing selected and no shot landed since: no summary (app.state.outcome is empty)
//   lm          a launch-monitor shot selected: its summary (the band already names it)
//   lm_min      the same with the carousel minimised (the strip folded; the band stays)
//   nolm        a shot with no launch-monitor reading selected: no summary, the band as today (CONTROL)
//   cleared     the selection cleared: the outcome with it (R-APP-7), so no summary
// A shot LANDING (EVT-01, R-APP-7's other half) needs a capture or a launch monitor, which the headless start has none
// of: outcome_summary_test drives that half over the real owners.
// With --probe-expect-summary it also checks the summary (shown or not, never blank, never a 0); without it (the build
// before 4-b) it only grabs, for the BEFORE half of the review sheet.
//
//   PINPOINT_HEADLESS_PROBE=1 QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 HOME=<scratch> CFFIXED_USER_HOME=<scratch> \
//     XDG_CONFIG_HOME=<scratch>/.config PINPOINT_DIAGNOSTICS_DIR=<scratch>/diag \
//     <PinPointStudio> --probe-qml <abs path to this file> --probe-athlete <uuid> --probe-live <a session folder> \
//     --probe-lm <swing number> --probe-nolm <swing number> --probe-out <folder for the PNGs> [--probe-expect-summary]
//     [--probe-width 1120]
// ⚠ SLOW BY DESIGN: every carousel()/summaryRow() call walks the whole window tree (7–17 s of GUI stall each on the
// review's run); the probe's ticks wait for it. Cache them if this probe grows (carried, 4-b review).
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
    readonly property string athlete: arg("--probe-athlete", "")
    readonly property string liveDir: arg("--probe-live", "")
    readonly property int lmSwing: parseInt(arg("--probe-lm", "0"))
    readonly property int nolmSwing: parseInt(arg("--probe-nolm", "0"))
    readonly property string outDir: arg("--probe-out", "")
    readonly property bool expectSummary: Qt.application.arguments.indexOf("--probe-expect-summary") >= 0
    readonly property int probeWidth: parseInt(arg("--probe-width", "1880"))   // 1120: the first-launch window

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
        return ok
    }
    function finish() {
        tick.running = false
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }
    function walk(obj, pred, out, seen) {
        if (!obj || seen.has(obj)) return
        seen.add(obj)
        if (pred(obj)) out.push(obj)
        const kids = obj.data || obj.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], pred, out, seen)
    }
    function find(pred) { const out = []; walk(probe.Window.window.contentItem, pred, out, new Set()); return out }
    function shown(item) {
        let i = item
        while (i) { if (i.visible === false) return false; i = i.parent }
        return true
    }
    function carousel() {
        const c = find(o => o.markedIds !== undefined && o._collapseKey !== undefined && o.activeModel !== undefined).filter(shown)
        return c.length > 0 ? c[0] : null
    }
    // The summary as drawn: the band's row and its words (what Mark reads).
    function summaryRow() {
        const r = find(o => o.objectName === "shotSummary")
        return r.length > 0 ? r[0] : null
    }
    function summaryWords() {
        const r = summaryRow()
        if (!r || !shown(r)) return ""
        const out = []
        walk(r, o => o.font !== undefined && typeof o.text === "string" && o.text !== "" && shown(o), out, new Set())   // drawn text only
        return out.map(o => o.text).join(" ")
    }

    Instantiator {
        id: live
        model: shotModel
        delegate: QtObject { required property int shotId; required property int ordinal; required property string swingDir }
    }
    function shotOf(n) {
        for (let i = 0; i < live.count; ++i) if (live.objectAt(i).ordinal === n) return live.objectAt(i)
        return null
    }

    property int step: 0
    property int waits: 0
    property var pending: []
    function grab(name, then) {
        const c = carousel()
        if (!check(!!c, "the carousel is on screen for " + name)) { then(); return }
        c.grabToImage(function (r) {
            r.saveToFile(outDir + "/" + name + ".png")
            log("grabbed " + name + " " + c.width + "x" + c.height + " summary='" + summaryWords() + "'")
            then()
        }, Qt.size(c.width * 2, c.height * 2))
    }
    function select(n) {
        const s = shotOf(n)
        if (!check(!!s, "swing " + n + " is on the carousel")) return
        app.state.enterReplay(shotModel.shotRefOf(s.shotId), s.swingDir)     // the carousel's click path
    }
    // The summary's own checks (after 4-b only): shown or not, its shot named or not, no blank and no 0.
    function checkSummary(state, wantShown, wantNamed) {
        if (!expectSummary) return
        const r = summaryRow()
        const vis = !!r && shown(r)
        check(vis === wantShown, state + ": the summary is " + (wantShown ? "shown" : "not shown"))
        if (!vis) return
        const w = summaryWords()
        const o = app.state.shotOutcome
        check(w.indexOf("STRIKE") >= 0 || w.toUpperCase().indexOf("STRIKE") >= 0, state + ": it names the factors (" + w + ")")
        check(!/(^|\s)0(\s|$)/.test(w) && !/(^|\s)0 yd/.test(w), state + ": no value is a bare 0")
        check([o.strikeText, o.directionText, o.flightText, o.carryText].every(t => t !== ""), state + ": no value is blank")
        // Nothing cut (the 4-b review's blocker): the summary's right edge inside the carousel's.
        const c = carousel(), right = r.mapToItem(c, r.width, 0).x
        check(right <= c.width, state + ": nothing cut — the summary ends at " + Math.round(right) + " of " + Math.round(c.width)
              + " (font scale " + Theme.fontScale + ", " + Theme.density + ")")
        check(o.selected === !wantNamed, state + ": its shot is " + (wantNamed ? "named (not the selection)" : "the selection"))
        if (wantNamed) check(w.indexOf("#" + o.swingNumber) >= 0, state + ": the band names swing #" + o.swingNumber)
    }

    Timer {
        id: tick
        interval: 2500; repeat: true; running: true
        onTriggered: probe.next()
    }

    function next() {
        step += 1
        if (step === 1) {
            if (!check(liveDir !== "" && outDir !== "" && athlete !== "", "--probe-live, --probe-out and --probe-athlete given")) { finish(); return }
            const win = probe.Window.window              // full screen first: the width the studio runs at
            if (win) { win.width = probeWidth; win.height = 1040 }
            athleteController.selectAthlete(athlete)
            navController.navigate(AppScreen.Session)
            app.state.enterCapture()
        } else if (step === 2) {
            shotModel.loadSessionDir(liveDir)          // the session screen's own call, as for today's session
        } else if (step === 3) {
            log("on screen " + app.state.sessionOnScreenFolder + ", carousel " + live.count + " shot(s)")
            if (!check(live.count >= 2, "the live carousel holds the session's shots")) { finish(); return }
            for (let i = 0; i < live.count; ++i) {
                const o = live.objectAt(i), m = shotModel.shotSummary(o.shotId)
                log("swing " + o.ordinal + " " + m.club + " " + m.timestampLabel)
            }
            app.state.clearSelection()
        } else if (step === 4) {
            // the session's diagnostics land: wait for the newest shot's (after 4-b), or a fixed time (before it)
            const landed = waits >= 6
            if (!landed && waits < 24) { waits += 1; step = 3; return }   // the next tick comes back here
            log("diagnostics landed after " + waits + " wait(s)")
            tick.running = false
            checkSummary("nothing selected, nothing landed", false, false)
            grab("nothing", function () { select(lmSwing); tick.running = true })
        } else if (step === 5) {
            tick.running = false
            if (expectSummary) check(app.state.shotOutcome.judged, "LM shot selected: its factors are judged")
            checkSummary("LM shot selected", true, false)
            grab("lm", function () { carousel().expanded = false; tick.running = true })
        } else if (step === 6) {
            tick.running = false
            check(!carousel().expanded, "the carousel is minimised")
            checkSummary("LM shot selected, carousel minimised", true, false)
            grab("lm_min", function () { carousel().expanded = true; select(nolmSwing); tick.running = true })
        } else if (step === 7) {
            tick.running = false
            checkSummary("non-LM shot selected (CONTROL)", false, false)
            grab("nolm", function () { app.state.clearSelection(); tick.running = true })
        } else if (step === 8) {
            tick.running = false
            checkSummary("selection cleared", false, false)
            grab("cleared", function () { finish() })
        }
    }
}
