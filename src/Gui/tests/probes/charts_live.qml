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

// END-TO-END: TODAY'S (LIVE) SESSION IN CHARTS AND IN A TRUST FOLD (refactor 5.5a, DUTIES-5.5 §D "a live shot").
//
//   select the scratch athlete → the session screen at the mat: today's session on screen (as 5.4x's probe-footer) →
//   pick its newest shot from the live carousel → Analyse with Charts alone on the Session view → at each state
//   (reloaded, Extended = running, Ended) log the session's shot count three ways and check they agree:
//     the Session's own (its diagnostics adapter's shotCount, diagnosticsAdapters.of(app.session, …));
//     the Charts panel's Session view (body.tSession.n, and the Relations status' shotCount);
//     a Trust summary fold (PpTrustSummaryFold, objectName trustSummaryFold: its TrustPresenter's session.n): Session
//     diagnostics' This shot fold, beside Charts, which is given the session folder; Charts' own fold under the Session
//     view shows only for a REPLAYED shot (tShot), and the fixture swings have no video, so it is logged and checked
//     only when shown — and that all equal --probe-expect, the swings copied in.
//
// ⚠ WHAT THIS DOES NOT DO: MAKE A SHOT ARRIVE. EVT-01 (Session::shotAdded) is emitted only by
// Session::shotDocumentWritten, which capture (ShotProcessor, a camera shot's export) and the launch monitor's
// device-only write call from C++; headless there is no capture buffer (onShotDetected refuses) and no launch-monitor
// connector (Kind::None), and no QML door reaches either. Emitting shotProcessor.shotProcessed from QML, as
// ledger_live_states.qml does, feeds the CONTROL's panel-owned model but is not EVT-01 (the Session never hears it),
// so it is deliberately not used. Shots arriving one by one need a C++ probe seam (for the Product Owner).
//
// THE SAME FILE RUNS ON THE CONTROL (6675bbe8) AND ON THE REFACTORED BUILD: chBody's properties and the views'
// objectNames only, never the series model.
//
//   run through tools/probe/run_headless.sh (build/refactor/step-5/5.5/sheets/run-sheets.sh):
//     --probe-qml <this file> --probe-athlete <uuid> --probe-live <today's session dir> --probe-lib <scratch library>
//     --probe-expect <analysed shots> [--probe-out <png dir>]

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function arg(name, dflt) { const a = Qt.application.arguments; const i = a.indexOf(name); return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt }
    readonly property string athlete: arg("--probe-athlete", "")
    readonly property string liveDir: arg("--probe-live", "")
    readonly property string lib: arg("--probe-lib", "")
    readonly property int expect: parseInt(arg("--probe-expect", "-1"))
    readonly property string outDir: arg("--probe-out", "")

    function log(s) { console.warn("PROBE: " + s) }
    property int fails: 0
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) fails += 1; return ok }
    function same(a, b) { return String(a).replace(/\/+$/, "") === String(b).replace(/\/+$/, "") }
    function root() { return probe.Window.window.contentItem }
    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name) out.push(item)
        for (const k of (item.children || [])) walk(k, name, out)
    }
    function shown(item) { let i = item; while (i && i !== root()) { if (!i.visible) return false; i = i.parent } return !!i }
    function all(under, name) { const o = []; walk(under || root(), name, o); return o.filter(shown) }
    function one(under, name) { return all(under, name)[0] || null }
    function body() { return one(null, "chBody") }

    Component { id: timerC; Timer { repeat: false } }
    function after(ms, fn) { const t = timerC.createObject(probe, { interval: ms }); t.triggered.connect(function () { t.destroy(); fn() }); t.start() }
    function waitFor(cond, ms, what, fn) {
        const t0 = Date.now()
        const poll = function () {
            let ok = false
            try { ok = !!cond() } catch (e) { ok = false }
            if (ok) { fn(true); return }
            if (Date.now() - t0 > ms) { log("timed out after " + ms + " ms waiting for " + what); fn(false); return }
            after(250, poll)
        }
        poll()
    }
    property var queue: []
    function run(list) { queue = list; next() }
    function next() { if (!queue.length) { finish(); return } const f = queue.shift(); try { f(next) } catch (e) { check(false, "a step threw: " + e); next() } }
    function finish() {
        if (panelModel) { try { panelModel.dropView() } catch (e) {} }
        log(fails === 0 ? "RESULT PASS" : "RESULT FAIL (" + fails + ")")
        Qt.exit(fails === 0 ? 0 : 1)
    }
    Timer { interval: 280000; running: true; onTriggered: { probe.check(false, "the probe finished inside 280 s"); probe.finish() } }

    Instantiator {
        id: live
        model: app.sessionReviewController.activeShots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    property var panelModel: null
    function idle(b) {
        if (!b || b.busy) return false
        for (const p of [b.tracesP, b.relationsP, b.wristP, b.postureP, b.deliveryP]) if (p && p.busy) return false
        return true
    }
    // the Trust fold under Charts' Session view, and the TrustPresenter it holds (one of its resources)
    function chartsFold() { const o = []; walk(one(null, "chSessionView"), "trustSummaryFold", o); return o[0] || null }
    function sdFold() { const o = []; walk(one(null, "sdBody"), "trustSummaryFold", o); return o.filter(function (f) { return f._session !== "" })[0] || o[0] || null }
    function presenterOf(fold) {
        if (!fold) return null
        for (const r of (fold.resources || [])) if (r && r.session !== undefined && r.pickedSwingDir !== undefined) return r
        for (const r of (fold.data || [])) if (r && r.session !== undefined && r.pickedSwingDir !== undefined) return r
        return null
    }
    // one state: wait for Charts to settle, then the three counts
    function trustCount() { const t = presenterOf(sdFold()); return t && t.session && t.session.n !== undefined ? t.session.n : -1 }
    function counts(tag) {
        return function (done) {
            waitFor(function () { const b = body(); return probe.idle(b) && (!probe.panelModel || !probe.panelModel.busy) }, 90000, tag + ": Charts and the Session idle", function () {
                waitFor(function () { return probe.trustCount() === probe.expect }, 30000, tag + ": the Trust fold's count", function () { after(1500, function () {
                    const b = body()
                    const sessionN = panelModel ? panelModel.shotCount : -1
                    const carousel = live.count
                    const se = b ? (b.tSession || {}) : {}
                    const st = b ? (b.rStatus || {}) : {}
                    const sh = b ? (b.tShot || {}) : {}
                    const chartsN = se.n !== undefined ? se.n : -1
                    const cf = chartsFold(), ctp = presenterOf(cf)
                    const fold = sdFold()
                    const tp = presenterOf(fold)
                    const nOf = function (t) { return t && t.session && t.session.n !== undefined ? t.session.n : -1 }
                    const trustN = nOf(tp)
                    const rec = fold && fold.summary ? fold.summary.recorded : undefined
                    log(tag + ": session on screen " + (same(app.state.sessionOnScreenFolder, liveDir) ? "today's" : "'" + app.state.sessionOnScreenFolder + "'")
                        + " · mode " + app.state.mode + " · Charts shown view " + (b ? b.shownView : "-") + " register " + (b ? b.register : "-"))
                    log(tag + ": shots — Session (its adapter) " + sessionN + " · carousel " + carousel + " · Charts Session view n " + chartsN
                        + " (available " + se.available + ", kicker '" + (se.kicker || "") + "') · Relations shotCount " + st.shotCount
                        + " · This shot kicker '" + (sh.kicker || "") + "' sessionCount " + sh.sessionCount
                        + " · expected " + expect)
                    log(tag + ": Trust folds — Session diagnostics' (scope " + (fold ? fold.scope : "-") + ", shown " + (fold ? shown(fold) : "-") + ", lines " + (fold ? fold.lines.length : "-")
                        + ") presenter session.n " + trustN + " recorded " + (tp && tp.session ? tp.session.recorded : "-") + " summary.recorded " + rec
                        + " · Charts Session view's (needs a replayed shot: shown " + (cf ? shown(cf) : "-") + ") presenter session.n " + nOf(ctp))
                    if (fold && fold.summary) log(tag + ": trust fold tail '" + (fold.summary.tail || "") + "' · " + (fold.lines || []).map(function (l) { return "[" + l.kind + "] " + l.text }).join(" ¦ "))
                    check(sessionN === expect, tag + ": the Session holds the " + expect + " analysed shots (" + sessionN + ")")
                    check(chartsN === expect, tag + ": Charts' Session view counts " + expect + " shots (" + chartsN + ")")
                    check(st.shotCount === expect, tag + ": Charts' Relations status counts " + expect + " shots (" + st.shotCount + ")")
                    check(trustN === expect, tag + ": Session diagnostics' Trust fold's presenter counts " + expect + " shots (" + trustN + ")")
                    if (cf && shown(cf)) check(nOf(ctp) === expect, tag + ": Charts' Trust fold's presenter counts " + expect + " shots (" + nOf(ctp) + ")")
                    if (outDir !== "") {
                        // the window, cropped by run-sheets.sh to the GEO box (a view's own grab has no alpha)
                        const v = one(null, "chSessionView")
                        const ci = probe.Window.window.contentItem
                        let top = v
                        while (top && top.parent && top.parent !== ci) top = top.parent
                        if (v && top) {
                            const p = v.mapToItem(null, 0, 0)
                            log("GEO live-" + tag + " " + Math.round(p.x) + " " + Math.round(p.y) + " " + Math.round(v.width) + " " + Math.min(Math.round(v.height), probe.Window.window.height - Math.round(p.y)))
                            top.grabToImage(function (res) { res.saveToFile(probe.outDir + "/live-" + tag + ".window.png"); done() })
                            return
                        }
                    }
                    done()
                }) })
            })
        }
    }

    Component.onCompleted: after(2000, function () { run([
        function (done) {
            if (!check(athlete !== "" && liveDir !== "" && lib !== "" && expect >= 0, "arguments given")) { finish(); return }
            probe.Window.window.height = 2000
            appSettings.athleteLibraryPath = lib
            check(app.athleteController.selectAthlete(athlete), "the scratch athlete selected")
            after(1500, done)
        },
        function (done) {
            app.navController.navigate(AppScreen.Session)
            app.state.enterCapture()
            waitFor(function () { return !!app.session && probe.same(app.state.sessionOnScreenFolder, probe.liveDir) && live.count > 0 }, 30000,
                    "today's session on screen", function (ok) {
                check(ok, "today's session is on screen with its shots on the carousel (" + live.count + ")")
                panelModel = app.session ? app.diagnosticsAdapters.of(app.session, "probe-live", liveDir) : null
                if (panelModel) { panelModel.addView(); panelModel.activateSession(liveDir) }
                check(!!panelModel, "the Session's diagnostics adapter")
                done()
            })
        },
        function (done) {
            // the newest shot, from the live carousel
            let best = null
            for (let i = 0; i < live.count; ++i) { const s = live.objectAt(i); if (!best || String(s.swingDir) > String(best.swingDir)) best = s }
            if (!check(!!best, "a shot to pick")) { finish(); return }
            app.state.enterReplay(app.sessionReviewController.activeShots.shotRefOf(best.shotId), best.swingDir)
            log("picked shot " + best.ordinal + " (" + String(best.swingDir).replace(/^.*\//, "") + ")")
            after(2000, function () {
                app.state.enterAnalyse()
                const m = app.state.analyseMode
                for (const k of ["camera", "swing3d", "launchMonitor", "table", "markup", "trust"]) ViewLayout.setPanel(m, k, false)
                ViewLayout.setPanel(m, "charts", true)
                ViewLayout.setPanel(m, "sessionDiagnostics", true)
                ViewLayout.setArrangement(m, "split")
                waitFor(function () { return !!body() }, 30000, "the Charts body", function () {
                    body().choose("preset", ""); body().tab = "session"
                    waitFor(function () { return !!one(null, "chSessionView") && !!one(null, "sdBody") }, 30000, "the Session view and Session diagnostics", function () {
                        one(null, "sdBody").tab = "shot"
                        done()
                    })
                })
            })
        },
        counts("reloaded"),
        function (done) { app.state.startSession(true); after(2500, function () { log("Extend: capture target today's " + same(app.state.captureTargetFolder, liveDir)); done() }) },
        counts("extended"),
        function (done) { app.state.endSession(true); after(3000, done) },
        function (done) {
            // End reloads the carousel: Charts may be re-made; put it back on the Session view
            waitFor(function () { return !!body() && !!one(null, "sdBody") }, 30000, "the Charts body after End", function () { body().tab = "session"; one(null, "sdBody").tab = "shot"; after(1000, done) })
        },
        counts("ended")
    ]) })
}
