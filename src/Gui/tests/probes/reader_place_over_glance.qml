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

// END-TO-END (#129): THE ANALYSE REGISTER READ OVER THE REPLAY GLANCE KEEPS PLACES OF ITS OWN.
//
// Session diagnostics reads Analyse over the glance for a detail, a Consistency cell or the Why of a shot
// (PpSessionDiagnosticsBody.placeScope "Replay>Analyse"). Those lists must start at their own tops, a scroll
// there must leave Analyse's own place alone, and the one detail list keeps a place per condition:
//   G1  Analyse, This shot scrolled deep; the glance; "Why →": This shot over the glance opens at its top
//   G2  a scroll over the glance (to 40) does not touch Analyse's place
//   G3  a condition's detail over the glance, scrolled to 600, closed; ANOTHER condition opens at its top;
//       the first again comes back at 600
//   G4  back in Analyse, This shot is where the reader left it there
//   G5  Analyse places on This shot and Session; the glance; a detail over it; the stage to Analyse WITH the detail
//       open (the register is kept, its tabs hidden behind the detail); close it: both tabs at their places
// Each check polls for its expected value (8 s) after the step's settling delay, so a GUI stall cannot fail it.
//
// Writes the View layout and regrades the session — run on a COPY of the settings and of the session:
//   build/run-me/verify-reader-place.sh [session dir]      (runs this and reader_place_survives_shot_switch.qml)
// or by hand: PinPointStudio --probe-qml <abs path to this file> --probe-session <scratch session dir>
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why.
import QtQuick
import QtQuick.Controls
import PinPointStudio

Item {
    id: probe
    function arg(name, dflt) { const a = Qt.application.arguments, i = a.indexOf(name); return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt }
    readonly property string sessionDir: arg("--probe-session", "")
    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) failures += 1 }
    function after(ms, fn) { const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe); t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start() }
    function find(it, pred, out) { if (!it) return; if (pred(it)) out.push(it); const k = it.children || []; for (let i = 0; i < k.length; ++i) find(k[i], pred, out) }
    function all(root, pred) { const o = []; find(root || Overlay.overlay.parent, pred, o); return o }
    function shown(it) { for (let p = it; p; p = p.parent) if (!p.visible) return false; return !!it }
    function body() { return all(null, o => o.objectName === "sdBody").filter(b => shown(b))[0] || null }
    function flick(name) { const b = body(); return b ? all(b, o => o.objectName === name && shown(o))[0] || null : null }
    function most(f) { return f ? Math.max(0, f.contentHeight - f.height) : 0 }
    function place(key) { const n = SessionMode.diagnosticsNav; return n && n.scroll ? n.scroll[key] : undefined }
    function val(tag, name) { const f = flick(name), b = body(), n = SessionMode.diagnosticsNav
        log("VALUE " + tag + " " + name + " contentY=" + (f ? Math.round(f.contentY) : "missing") + " most=" + Math.round(most(f))
            + " register=" + (b ? b.register : "-") + " scope=" + (b ? b.placeScope : "-") + " mode=" + SessionMode.mode
            + " detail=" + (b ? b.detailConditionId : "-") + " nav=" + (n ? JSON.stringify(n.scroll) : "null")); return f }
    function steps(list) { if (!list.length) { log(failures === 0 ? "RESULT PASS" : "RESULT FAIL (" + failures + ")"); Qt.exit(failures === 0 ? 0 : 1); return }
        const s = list.shift(); s(() => after(250, () => steps(list))) }
    Instantiator { id: shotList; model: sessionReviewController.shots
        delegate: QtObject { required property int shotId; required property int ordinal; required property string swingDir } }
    // Every check POLLS for its expected value (up to `ms`) after the step's settling delay, so a GUI stall
    // (the app has 2–3 s ones) cannot make the probe read a list before the panel has put its place back.
    function until(pred, ms, then) {
        const t0 = Date.now()
        const tick = () => { let ok = false; try { ok = !!pred() } catch (e) {} if (ok || Date.now() - t0 > ms) then(ok); else after(100, tick) }
        tick()
    }
    function expect(what, pred, then) { until(pred, 8000, (ok) => { check(ok, what()); then() }) }
    function near(a, b) { return a !== undefined && b !== undefined && Math.abs(a - b) < 1 }
    function yOf(name) { const f = flick(name); return f ? f.contentY : undefined }

    property var shotA: null
    property var cards: []
    property real deep: 0
    property real d1: 0
    property real sess: 0
    property var plan: [
        (done) => { navController.navigate(AppScreen.Session); SessionMode.enterCapture(); sessionReviewController.loadSession(probe.sessionDir)
            after(5000, () => { const l = []; for (let i = 0; i < shotList.count; ++i) l.push(shotList.objectAt(i)); l.sort((a, b) => a.ordinal - b.ordinal)
                probe.shotA = l[1] || l[0]; log("shots " + l.length + " A #" + (shotA ? shotA.ordinal : "-")); if (!shotA) { Qt.exit(2); return } done() }) },
        (done) => { for (const m of [SessionMode.replay, SessionMode.analyse]) { for (const k of ["camera", "swing3d", "launchMonitor", "sessionDiagnostics", "table", "markup", "trust", "charts"]) ViewLayout.setPanel(m, k, k === "sessionDiagnostics"); ViewLayout.setArrangement(m, "tabs") }
            SessionMode.enterReplay(shotA.shotId, shotA.swingDir); after(2500, () => { SessionMode.enterAnalyse(); after(3500, () => { const b = body(); if (b) b.tab = "shot"; after(2000, done) }) }) },
        // G1
        (done) => { const f = flick("sdShotCardsFlick"); check(most(f) > 400, "G1: This shot has room to scroll (" + Math.round(most(f)) + ")")
            if (f) f.contentY = Math.round(most(f) * 0.75); probe.deep = f ? f.contentY : 0
            after(500, () => expect(() => "G1: Analyse's place is " + Math.round(probe.deep) + " (" + place("Analyse/sdShotCardsFlick") + ")",
                                    () => near(place("Analyse/sdShotCardsFlick"), probe.deep), () => { val("G1-analyse-scrolled", "sdShotCardsFlick"); done() })) },
        (done) => { SessionMode.enterReplay(shotA.shotId, shotA.swingDir); after(3500, () => { val("G1-glance", "sdShotCardsFlick"); const b = body(); probe.cards = b ? (b.cards || []).map(c => c.id) : []
            if (b) b._openShotWhy(); after(2500, () => {
                expect(() => "G1: This shot is read over the glance (" + (body() ? body().placeScope : "-") + ")", () => !!flick("sdShotCardsFlick") && body().placeScope === "Replay>Analyse", () => {
                    expect(() => "G1: over the glance This shot opens at its top (" + yOf("sdShotCardsFlick") + ")", () => near(yOf("sdShotCardsFlick"), 0), () => {
                        val("G1-why-over-glance", "sdShotCardsFlick"); done() }) }) }) }) },
        // G2
        (done) => { const f = flick("sdShotCardsFlick"); if (f) f.contentY = 40; after(500, () => {
            expect(() => "G2: the over-glance scroll is its own place (" + place("Replay>Analyse/sdShotCardsFlick") + ")", () => near(place("Replay>Analyse/sdShotCardsFlick"), 40), () => {
                check(near(place("Analyse/sdShotCardsFlick"), probe.deep), "G2: Analyse's place stands (" + place("Analyse/sdShotCardsFlick") + ")")
                val("G2-why-scrolled-to-40", "sdShotCardsFlick"); body()._closeShotWhy(); after(1500, done) }) }) },
        // G3
        (done) => { const b = body(); check(probe.cards.length >= 2, "G3: two conditions to open (" + probe.cards.length + ")"); if (!b || probe.cards.length < 2) { done(); return }
            b._openDetail(probe.cards[0]); after(2500, () => expect(() => "G3: the first condition's detail opens at its top (" + yOf("sdDetailFlick") + ")", () => near(yOf("sdDetailFlick"), 0), () => {
                const f = flick("sdDetailFlick"); if (f) f.contentY = Math.round(Math.min(600, most(f) / 2)); probe.d1 = f ? f.contentY : 0
                after(1200, () => { val("G3-detail1-scrolled", "sdDetailFlick"); body()._closeDetail(); after(1500, () => { body()._openDetail(probe.cards[1])
                    after(2500, () => expect(() => "G3: ANOTHER condition's detail opens at its top (" + yOf("sdDetailFlick") + ")", () => near(yOf("sdDetailFlick"), 0), () => {
                        val("G3-detail2-open", "sdDetailFlick"); body()._closeDetail(); after(1500, () => { body()._openDetail(probe.cards[0])
                            after(2500, () => expect(() => "G3: the first condition again comes back at " + Math.round(probe.d1) + " (" + yOf("sdDetailFlick") + ")",
                                                     () => probe.d1 > 0 && near(yOf("sdDetailFlick"), probe.d1), () => {
                                val("G3-detail1-again", "sdDetailFlick"); body()._closeDetail(); after(1500, done) })) }) })) }) }) })) },
        // G4
        (done) => { SessionMode.enterAnalyse(); after(3500, () => { const b = body(); if (b) b.tab = "shot"; after(1500, () =>
            expect(() => "G4: back in Analyse, This shot at " + Math.round(probe.deep) + " (" + yOf("sdShotCardsFlick") + ")",
                   () => { const f = flick("sdShotCardsFlick"); return !!f && near(f.contentY, Math.min(probe.deep, most(f))) }, () => { val("G4-back-in-analyse", "sdShotCardsFlick"); done() })) }) },
        // G5: Analyse places on two tabs; the glance; a detail over it; the stage goes to Analyse WITH the detail open
        // (the Analyse register is kept, its tabs hidden behind the detail); close it: both tabs at their places
        (done) => { const b = body(); b.tab = "session"; after(2000, () => { const f = flick("sdCardsFlick"); if (f) f.contentY = Math.round(most(f) * 0.5); probe.sess = f ? f.contentY : 0
            after(500, () => expect(() => "G5: Analyse's Session place is " + Math.round(probe.sess), () => probe.sess > 0 && near(place("Analyse/sdCardsFlick"), probe.sess), () => {
                b.tab = "shot"; after(1500, () => { val("G5-analyse-shot", "sdShotCardsFlick"); done() }) })) }) },
        (done) => { SessionMode.enterReplay(shotA.shotId, shotA.swingDir); after(3500, () => { body()._openDetail(probe.cards[0]); after(2500, () => {
            val("G5-detail-over-glance", "sdDetailFlick"); SessionMode.enterAnalyse(); after(3500, () => {
                val("G5-analyse-detail-open", "sdDetailFlick")
                check(near(place("Analyse/sdShotCardsFlick"), probe.deep) && near(place("Analyse/sdCardsFlick"), probe.sess),
                      "G5: with the detail open in Analyse, Analyse's places stand (" + JSON.stringify(SessionMode.diagnosticsNav ? SessionMode.diagnosticsNav.scroll : null) + ")")
                body()._closeDetail(); after(1500, () => { body().tab = "shot"; after(1000, () =>
                    expect(() => "G5: detail closed, This shot at " + Math.round(probe.deep) + " (" + yOf("sdShotCardsFlick") + ")",
                           () => { const f = flick("sdShotCardsFlick"); return !!f && near(f.contentY, Math.min(probe.deep, most(f))) }, () => {
                        val("G5-closed-shot", "sdShotCardsFlick"); body().tab = "session"; after(1000, () =>
                            expect(() => "G5: and Session at " + Math.round(probe.sess) + " (" + yOf("sdCardsFlick") + ")",
                                   () => { const f = flick("sdCardsFlick"); return !!f && near(f.contentY, Math.min(probe.sess, most(f))) }, () => {
                                val("G5-closed-session", "sdCardsFlick"); done() })) })) }) }) }) }) }
    ]
    Timer { interval: 5000; running: true; onTriggered: probe.steps(probe.plan.slice()) }
    Timer { interval: 420000; running: true; onTriggered: { probe.log("TIMEOUT"); Qt.exit(9) } }
}
