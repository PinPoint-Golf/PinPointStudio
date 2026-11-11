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

// END-TO-END (#129): THE READER'S PLACE COMES BACK WHERE THE READER LEFT IT, on Charts and Session diagnostics.
//   C1/D1  a place both lists can hold, through the carousel (Analyse → shot B in Replay → shot A → Analyse)
//   C2     Charts scrolled DEEPER than the Replay list can hold: the short list shows its end, the full place returns
//   C3     …and the reader scrolls on the short list (to 300): that DOES become the place
//   D2/D3  the same two for Session diagnostics in ONE register: THIS SHOT in Analyse, a long shot and a short one
//          picked the figures' way (SessionMode.pickShot keeps Analyse)
// Grabs the panel body into --probe-out as <case>-<moment>-<phase>.png (--probe-phase, default "before").
//
// Writes the View layout and regrades the session — run on a COPY of the settings and of the session:
//   build/run-me/verify-reader-place.sh [session dir]      (runs this and reader_place_over_glance.qml)
// or by hand: PinPointStudio --probe-qml <abs path to this file> --probe-session <scratch session dir> [--probe-out <dir>]
// The session needs ≥ 3 shots and This shot lists of different lengths among shots 2–9 (2026-10-08 has them).
// Each check polls for its expected value (8 s) after the step's settling delay, so a GUI stall cannot fail it.
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why. Takes about four minutes.
import QtQuick
import QtQuick.Controls
import PinPointStudio

Item {
    id: probe
    function arg(name, dflt) { const a = Qt.application.arguments, i = a.indexOf(name); return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt }
    readonly property string outDir: arg("--probe-out", "")   // "" = no grabs
    readonly property string phase: arg("--probe-phase", "before")
    readonly property string sessionDir: arg("--probe-session", "")
    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) failures += 1 }
    function after(ms, fn) { const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe); t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start() }
    function find(it, pred, out) { if (!it) return; if (pred(it)) out.push(it); const k = it.children || []; for (let i = 0; i < k.length; ++i) find(k[i], pred, out) }
    function all(root, pred) { const o = []; find(root || Overlay.overlay.parent, pred, o); return o }
    function first(root, pred) { const o = all(root, pred); return o.length ? o[0] : null }
    function shown(it) { for (let p = it; p; p = p.parent) if (!p.visible) return false; return !!it }
    function bodyNamed(n) { return all(Overlay.overlay.parent, o => o.objectName === n).filter(b => shown(b))[0] || null }
    function grabBody(bodyName, shot, then) {
        const b = bodyNamed(bodyName)
        if (!b || probe.outDir === "") { then(); return }
        b.grabToImage(img => { const f = outDir + "/" + shot + "-" + phase + ".png"; img.saveToFile(f); log("saved " + f); after(150, then) })
    }
    function steps(list) { if (!list.length) { log(failures === 0 ? "RESULT PASS" : "RESULT FAIL (" + failures + ")"); Qt.exit(failures === 0 ? 0 : 1); return }
        const s = list.shift(); s(() => after(250, () => steps(list))) }

    Instantiator {
        id: shotList
        model: sessionReviewController.shots
        delegate: QtObject { required property int shotId; required property int ordinal; required property string swingDir }
    }
    property var shotsSorted: []
    property var shotA: null
    property var shotB: null
    function layout(only) {
        for (const m of [SessionMode.replay, SessionMode.analyse]) {
            for (const k of ["camera", "swing3d", "launchMonitor", "sessionDiagnostics", "table", "markup", "trust", "charts"])
                ViewLayout.setPanel(m, k, k === only)
            ViewLayout.setArrangement(m, "tabs")
        }
    }
    function flickOf(bodyName, name) { const b = bodyNamed(bodyName); return b ? first(b, o => o.objectName === name && shown(o)) : null }
    function most(f) { return f ? Math.max(0, f.contentHeight - f.height) : 0 }
    function value(tag, bodyName, name, want) {
        const f = flickOf(bodyName, name)
        log("VALUE " + tag + " " + name + " contentY=" + (f ? Math.round(f.contentY) : "missing") + " want=" + Math.round(want)
            + " most=" + Math.round(most(f)) + " contentHeight=" + (f ? Math.round(f.contentHeight) : "-")
            + " mode=" + SessionMode.mode + " shot=" + SessionMode.focusedShotId)
        return f
    }
    function nav(kind) { const n = kind === "charts" ? SessionMode.chartsNav : SessionMode.diagnosticsNav
        log("VALUE " + kind + "Nav " + (n ? JSON.stringify(n.scroll) : "null")) }
    // Each check POLLS for its expected value (up to 8 s) after the step's settling delay, so a GUI stall (the
    // app has 2–3 s ones) cannot make the probe read a list before the panel has put its place back.
    function until(pred, ms, then) {
        const t0 = Date.now()
        const tick = () => { let ok = false; try { ok = !!pred() } catch (e) {} if (ok || Date.now() - t0 > ms) then(ok); else after(100, tick) }
        tick()
    }
    function expectAt(tag, bodyName, name, want, kind, then) {
        until(() => { const f = flickOf(bodyName, name); return !!f && want > 0 && Math.abs(f.contentY - want) < 1 }, 8000, (ok) => {
            const f = value(tag, bodyName, name, want); nav(kind)
            check(ok, tag + ": " + name + " at " + (f ? Math.round(f.contentY) : "-") + " (want " + Math.round(want) + ")")
            then()
        })
    }
    function expectEnd(tag, bodyName, name, place, kind, then) {
        until(() => { const f = flickOf(bodyName, name), m = most(f); return !!f && m < place && Math.abs(f.contentY - m) < 1 }, 8000, (ok) => {
            const f = value(tag, bodyName, name, place); nav(kind)
            check(ok, tag + ": the shorter list shows its end, " + (f ? Math.round(f.contentY) : "-") + " of " + Math.round(most(f)) + " (place " + Math.round(place) + ")")
            then()
        })
    }
    function carousel(s) { SessionMode.enterReplay(s.shotId, s.swingDir) }   // PpShotCarousel._toggleShot
    function figure(s) { SessionMode.pickShot(s.shotId, s.swingDir) }        // a shot picked on a figure: Analyse keeps Analyse

    // ── the cases ──
    property real want: 0
    property real readerY: 0
    // C1 / D1 / C2 / C3: through the carousel. depth: "fit" (min(most/2, 700)), "deep" (most/2); reader: scroll on shot B to 300
    function carouselCase(id, kind, only, bodyName, name, prep, depth, reader) {
        return [
            (done) => { layout(only); carousel(shotA); after(2500, () => { SessionMode.enterAnalyse(); after(3500, () => {
                const b = bodyNamed(bodyName)
                check(!!b, id + ": the panel is up in Analyse")
                if (!b) { done(); return }
                if (prep) prep(b)
                after(2500, () => {
                    const f = flickOf(bodyName, name)
                    const m = most(f)
                    check(!!f && m > 600, id + ": " + name + " has room to scroll (" + Math.round(m) + ")")
                    if (!f) { done(); return }
                    f.contentY = depth === "deep" ? Math.round(m / 2) : Math.min(Math.round(m / 2), 700)
                    probe.want = f.contentY
                    after(1200, () => { value(id + "-scrolled", bodyName, name, probe.want); nav(kind); grabBody(bodyName, id + "-scrolled", done) })
                })
            }) }) },
            (done) => { carousel(shotB); after(3500, () => {
                const next = () => {
                    if (reader) {
                        const f = flickOf(bodyName, name)
                        if (f) { f.contentY = Math.min(300, Math.round(most(f) / 2)); probe.readerY = f.contentY }
                        log(id + ": the reader scrolls the shorter list to " + Math.round(probe.readerY))
                        after(1200, () => { nav(kind); grabBody(bodyName, id + "-shotB", done) })
                    } else grabBody(bodyName, id + "-shotB", done)
                }
                if (depth === "deep" && kind === "charts") expectEnd(id + "-shotB", bodyName, name, probe.want, kind, next)
                else { value(id + "-shotB", bodyName, name, probe.want); nav(kind); next() }
            }) },
            (done) => { carousel(shotA); after(3500, () => { value(id + "-shotA-replay", bodyName, name, probe.want); nav(kind); done() }) },
            (done) => { SessionMode.enterAnalyse(); after(3500, () => {
                expectAt(id + "-back", bodyName, name, reader ? probe.readerY : probe.want, kind, () => grabBody(bodyName, id + "-back", done))
            }) }
        ]
    }

    // D2 / D3: THIS SHOT in Analyse, one register, a long shot and a short one
    property var longShot: null
    property var shortShot: null
    property var scan: ({})
    function scanSteps() {
        const out = [ (done) => { layout("sessionDiagnostics"); carousel(shotA); after(2500, () => { SessionMode.enterAnalyse(); after(3500, () => {
            const b = bodyNamed("sdBody"); if (b) b.tab = "shot"; after(1500, done) }) }) } ]
        const idx = [1, 2, 3, 4, 5, 6, 7, 8]          // resolved when they run: the shots load after the plan is made
        for (const k of idx)
            out.push((done) => { const s = probe.shotsSorted[k]; if (!s) { done(); return }
                figure(s); after(2500, () => {
                const f = flickOf("sdBody", "sdShotCardsFlick")
                probe.scan[s.ordinal] = most(f)
                log("SCAN shot " + s.ordinal + " sdShotCardsFlick most=" + Math.round(most(f)))
                done()
            }) })
        out.push((done) => {
            let lo = null, hi = null
            for (const s of probe.shotsSorted.slice(1, 9)) {
                const m = probe.scan[s.ordinal]
                if (m === undefined) continue
                if (!hi || m > probe.scan[hi.ordinal]) hi = s
                if (!lo || m < probe.scan[lo.ordinal]) lo = s
            }
            const ok = !!hi && !!lo && probe.scan[hi.ordinal] - probe.scan[lo.ordinal] > 100 && probe.scan[lo.ordinal] > 300
            check(ok, "THIS SHOT: a long shot (#" + (hi ? hi.ordinal + " most " + Math.round(probe.scan[hi.ordinal]) : "-")
                  + ") and a shorter one (#" + (lo ? lo.ordinal + " most " + Math.round(probe.scan[lo.ordinal]) : "-") + ")")
            probe.longShot = ok ? hi : null; probe.shortShot = ok ? lo : null
            done()
        })
        return out
    }
    function shotCase(id, reader) {
        const B = "sdBody", N = "sdShotCardsFlick"
        return [
            (done) => { if (!probe.longShot) { log(id + ": skipped, no long/short pair"); done(); return }
                figure(probe.longShot); after(3000, () => {
                    const f = flickOf(B, N)
                    const deep = probe.scan[probe.shortShot.ordinal] + (most(f) - probe.scan[probe.shortShot.ordinal]) / 2
                    if (f) f.contentY = Math.round(deep)
                    probe.want = f ? f.contentY : 0
                    after(1200, () => { value(id + "-scrolled", B, N, probe.want); nav("diag"); grabBody(B, id + "-scrolled", done) })
                }) },
            (done) => { if (!probe.longShot) { done(); return }
                figure(probe.shortShot); after(3000, () => {
                    expectEnd(id + "-short", B, N, probe.want, "diag", () => {
                        if (reader) {
                            const f = flickOf(B, N)
                            if (f) { f.contentY = Math.min(300, Math.round(most(f) / 2)); probe.readerY = f.contentY }
                            log(id + ": the reader scrolls the shorter list to " + Math.round(probe.readerY))
                            after(1200, () => { nav("diag"); grabBody(B, id + "-short", done) })
                        } else grabBody(B, id + "-short", done)
                    })
                }) },
            (done) => { if (!probe.longShot) { done(); return }
                figure(probe.longShot); after(3000, () => {
                    expectAt(id + "-back", B, N, reader ? probe.readerY : probe.want, "diag", () => grabBody(B, id + "-back", done))
                }) }
        ]
    }

    property var plan: [
        (done) => {
            navController.navigate(AppScreen.Session)
            SessionMode.enterCapture()
            sessionReviewController.loadSession(probe.sessionDir)
            after(5000, () => {
                const list = []
                for (let i = 0; i < shotList.count; ++i) list.push(shotList.objectAt(i))
                list.sort((a, b) => a.ordinal - b.ordinal)
                probe.shotsSorted = list
                probe.shotA = list.length > 1 ? list[1] : list[0] || null
                probe.shotB = list.length > 2 ? list[2] : list[0] || null
                log("session " + probe.sessionDir + " shots " + list.length + " A #" + (shotA ? shotA.ordinal : "-") + " B #" + (shotB ? shotB.ordinal : "-"))
                if (!shotA || !shotB || shotA === shotB) { log("need two shots"); Qt.exit(2); return }
                done()
            })
        }
    ].concat(carouselCase("C1", "charts", "charts", "chBody", "chSessionFlick", null, "fit", false))
     .concat(carouselCase("C2", "charts", "charts", "chBody", "chSessionFlick", null, "deep", false))
     .concat(carouselCase("C3", "charts", "charts", "chBody", "chSessionFlick", null, "deep", true))
     .concat(carouselCase("D1", "diag", "sessionDiagnostics", "sdBody", "sdCardsFlick",
                          (b) => { b.tab = "session"; b.cardFilter = "all"; b.watchingExpanded = true }, "fit", false))
     .concat(scanSteps())
     .concat(shotCase("D2", false))
     .concat(shotCase("D3", true))
    Timer { interval: 5000; running: true; onTriggered: probe.steps(probe.plan.slice()) }
    Timer { interval: 400000; running: true; onTriggered: { probe.log("TIMEOUT"); Qt.exit(9) } }
}
