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

// END-TO-END: THE CLUB DELIVERY SECTION OF CHARTS ON REAL DATA (docs/design/club_delivery_chart_design.md §4, stage 3).
//
//   load a session → pick shot N with the carousel's click path → Analyse (or the glance) → Charts on the Club
//   delivery preset → wait for ChartsDeliveryPresenter → report the section's head, plane story, ball flight (the fit,
//   its pool and the scatter), the impact lines, the shot shape and the trust lines (This shot), or the session's
//   head, band, "varies most", flight and shapes (Session); the plot's lines (plotLayers) and what the trace figure
//   drew of them → PASS/FAIL checks on the objectNames → GEO lines → grab the page.
//   The day pool reads the golfer's other sessions of the same date: copy the golfer/session folder layout.
//
// Run through --probe-qml (dev builds only), offscreen, under tools/typeset-harness/guard.sh:
//   QT_QPA_PLATFORM=offscreen:configfile=<1680×1050 screen json> PinPointStudio --probe-qml <abs path to this file> \
//       --probe-session <session dir> --probe-ordinal 7 [--probe-png <abs path stem>.png]
//   --probe-mode analyse | glance — Analyse (default); the glance at the mat (Replay, autoReturnToCapture)
//   --probe-tab shot | session (default shot)
//   --probe-theme <Theme.themeIndex> — render in that theme (the settings are a copy: point XDG_CONFIG_HOME at one)
//   --probe-draw split | overlay — small multiples or one plot (else the reader's saved choice)
//   --probe-trust open — open the section's "How far can I trust this?" before the grab
//   --probe-fold delivery|deliverySession — fold that section before the checks (its one-line state is logged)
//   The picture strip (stage 6): "PROBE: PICTURE …" lines (available, lines drawn, why, legend, each frame's crop, marks
//   and note; each drawn frame's hasImage and scale), a check that the strip is drawn when the payload has one, and the
//   grab waits (up to 40 s) for the frames' stills.
//   --probe-dump 1 — the presenter's payloads (thisShot, session, plotLayers, choices) as one "PROBE: DUMP <json>" line
// --probe-png writes the page (the view's scrolling content) as window grabs a viewport apart, <stem>_part<i>.png, with a
// "PROBE: PAGE w h viewport x y w h" line and a "PROBE: PART i contentY" line each, to stitch. "PROBE:" lines carry the
// findings; "PROBE: GEO <objectName> x y w h" each key item's box ON THE PAGE (for review-sheet crops), "PROBE: PASS|FAIL …"
// the checks. Exit 0 = every check passed. On a build without the
// section (the review's BEFORE) the probe still grabs the view; its checks fail.
// Loading a session writes into it: use a scratch copy.

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
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "2"))
    readonly property string png: arg("--probe-png", "")
    readonly property string dump: arg("--probe-dump", "")
    readonly property string mode: arg("--probe-mode", "analyse")
    readonly property string tab: arg("--probe-tab", "shot")
    readonly property bool glance: mode === "glance"

    function log(s) { console.warn("PROBE: " + s) }
    property int fails: 0
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) fails += 1 }
    function walk(item, name, out) {
        if (!item) return
        if (name instanceof RegExp ? name.test(item.objectName) : item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
    }
    function shown(item, top) {
        let i = item
        while (i && i !== top) { if (!i.visible) return false; i = i.parent }
        return !!i
    }
    function all(under, name) { const o = []; walk(under, name, o); return o.filter(function (x) { return shown(x, probe.Window.window.contentItem) }) }
    function one(under, name) { return all(under, name)[0] || null }
    function words(it) {
        const tx = []
        const grab = function (x) {
            if (!x || !x.visible) return
            if (x.text !== undefined && typeof x.text === "string" && x.text !== "" && x.elide !== undefined) tx.push(String(x.text).replace(/<[^>]*>/g, "").replace(/&nbsp;/g, " "))
            for (const k of (x.children || [])) grab(k)
        }
        grab(it)
        return tx
    }
    function flickOf(it) { for (let p = it ? it.parent : null; p; p = p.parent) if (p.contentY !== undefined && p.contentItem !== undefined) return p; return null }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    // A build without the section (the review's BEFORE) has no body.deliveryP: the probe makes its own presenter on
    // the panel's model, so the payloads still log.
    Component {
        id: ownC
        ChartsDeliveryPresenter {}
    }
    property var dp: null

    property int step: 0
    property int waits: 0
    property int picWaits: 0
    property var body: null
    property var view: null
    property var section: null
    Timer {
        id: tick
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const root = probe.Window.window.contentItem
            if (probe.step === 1) {
                const th = probe.arg("--probe-theme", "")
                if (th !== "") Theme.themeIndex = Number(th)
                navController.navigate(AppScreen.Session)
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (probe.step === 2) {
                let found = false
                for (let i = 0; i < shots.count; ++i) {
                    const s = shots.objectAt(i)
                    if (s.ordinal === probe.ordinal) { SessionMode.enterReplay(s.shotId, s.swingDir); found = true; probe.log("swing " + s.swingDir) }
                }
                probe.log("shot " + probe.ordinal + (found ? " picked" : " NOT in the carousel"))
                if (!found) Qt.exit(1)
            } else if (probe.step === 3) {
                if (!probe.glance) SessionMode.enterAnalyse()
                else SessionMode.autoReturnToCapture = true   // the auto-replay after a shot: the glance
                const m = probe.glance ? SessionMode.replay : SessionMode.analyse
                // Charts alone (the widest section)
                for (const k of ["camera", "swing3d", "launchMonitor", "sessionDiagnostics", "table", "markup", "trust"])
                    ViewLayout.setPanel(m, k, false)
                ViewLayout.setPanel(m, "charts", true)
                ViewLayout.setArrangement(m, "tabs")
            } else if (probe.step === 4) {
                const bodies = []
                probe.walk(root, "chBody", bodies)
                probe.body = bodies.filter(b => probe.shown(b, root))[0] || bodies[0] || null
                if (!probe.body) { probe.log("FAIL no Charts body"); Qt.exit(1); return }
                const b = probe.body
                probe.log("mode " + SessionMode.mode + " register " + b.register + " · theme " + Theme.themeIndex + (Theme.dark ? " (dark)" : " (light)"))
                b.choose("preset", "Club delivery")
                b.tab = probe.tab
                const c = {}
                const fold = probe.arg("--probe-fold", "")
                if (fold !== "") c[fold] = true
                b.collapsed = c
                const draw = probe.arg("--probe-draw", "")
                if (draw !== "") b.overlay = draw === "overlay"
                probe.dp = b.deliveryP !== undefined && b.deliveryP ? b.deliveryP : null
                if (!probe.dp) {
                    probe.log("this build has no body.deliveryP: the probe makes its own presenter (BEFORE)")
                    probe.dp = ownC.createObject(probe, {
                        model: b.tracesP.model,
                        pickedSwingDir: Qt.binding(function () { return shotReplay.active ? shotReplay.swingDir : "" }),
                        pickedDetail: Qt.binding(function () { return shotReplay.active && shotReplay.analysisDetail ? shotReplay.analysisDetail : ({}) }),
                        register: b.register === "Analyse" ? "analyse" : "replay",
                        scope: probe.tab === "session" ? "session" : "shot" })
                }
            } else if (probe.step === 5) {
                // wait for the presenters (a coalesced job each)
                const b = probe.body, dp = probe.dp
                if ((b.busy || !dp || dp.busy) && probe.waits < 30) { probe.waits += 1; probe.step -= 1; return }
                const sh = dp.thisShot || {}, se = dp.session || {}, pl = dp.plotLayers || {}
                if (probe.dump !== "") probe.log("DUMP " + JSON.stringify({ thisShot: sh, session: se, plotLayers: pl, choices: dp.choices }))
                probe.log("preset '" + (b.tChoices.preset || "") + "' · delivery on show " + b.deliveryOnShow + ", available " + b.deliveryAvailable
                          + ", shot shown " + b.deliveryShotShown + ", session shown " + b.deliverySessionShown)
                probe.log("rows: " + (b.tShot.rows || []).map(r => r.key).join(", "))
                const lay = pl.swingPlaneCurve || []
                probe.log("plotLayers (" + dp.scope + "): " + lay.map(l => l.role + " " + l.k + (l.k === "hline" ? " " + Number(l.v).toFixed(1) : l.k === "band" ? " " + l.t.map(x => Math.round(x)).join("…") + " ms, " + Number(l.lo[0]).toFixed(1) + "–" + Number(l.hi[0]).toFixed(1) : " " + Math.round(l.t0) + "…" + Math.round(l.t1) + " ms")).join(" · "))
                if (probe.tab === "shot") {
                    probe.log("kicker: " + (sh.kicker || ""))
                    probe.log("mode: " + sh.mode + " · head rule " + sh.headRule + " · head: " + (sh.headText || ""))
                    probe.log("story: " + ((sh.story && sh.story.text) || "") + " | " + ((sh.story && sh.story.shapeLine) || ""))
                    probe.log("slot: " + ((sh.slot && sh.slot.text) || ""))
                    const fl = sh.flight || {}
                    probe.log("flight: " + (fl.text || "") + " | attack: " + (fl.attackText || "") + " | this: " + (fl.thisLine || ""))
                    probe.log("pool: " + JSON.stringify(fl.pool || {}) + " · scatter points " + ((fl.scatter && fl.scatter.points) || []).length)
                    for (const l of ((sh.impact && sh.impact.lines) || [])) probe.log("  impact " + l.key + " (" + l.source + "): " + l.text)
                    probe.log("shape: " + ((sh.shape && sh.shape.word) || "-") + " · face " + ((sh.shape && sh.shape.faceText) || "") + " · f2p " + ((sh.shape && sh.shape.faceToPathText) || ""))
                    for (const t of ((sh.trust && sh.trust.lines) || [])) probe.log("  trust: " + t)
                    probe.log("replay glance: " + JSON.stringify(sh.replay || {}))
                    const pic = sh.picture || {}
                    probe.log("PICTURE available " + pic.available + " · lines " + JSON.stringify(pic.lines || {}) + " · pending " + pic.pending)
                    for (const w of (pic.why || [])) probe.log("PICTURE why: " + w)
                    probe.log("PICTURE legend: " + (pic.legend || ""))
                    for (const g of (pic.groups || [])) for (const fr of (g.frames || [])) {
                        probe.log("PICTURE FRAME " + fr.tag + " " + fr.when + " · " + fr.width + "×" + fr.height + " crop " + JSON.stringify(fr.crop)
                                  + " · frame " + fr.frameIndex + " · note: " + (fr.note || "") + (fr.imageWhy ? " · imageWhy: " + fr.imageWhy : ""))
                        for (const m of (fr.marks || [])) probe.log("PICTURE MARK " + fr.tag + " " + m.kind + " '" + m.label + "' " + JSON.stringify(m.pts))
                    }
                    probe.view = probe.one(root, "chShotView")
                    probe.section = probe.one(root, "chDeliverySection")
                    if (probe.section && pic.available === true && b.collapsed.delivery !== true) {
                        probe.check(!!probe.one(probe.section, "chDeliveryPicture"), "chDeliveryPicture drawn")
                        probe.check(probe.all(probe.section, "chDeliveryPictureFrame").length === 3, "three frames: address, the top, P6")
                    }
                    probe.check(b.deliveryShotShown === true, "deliveryShotShown on the Club delivery preset")
                    probe.check(!!probe.section && probe.section.height > 0, "chDeliverySection shown")
                    if (probe.section) {
                        const sec = probe.section
                        probe.log("fold head: " + probe.words(probe.one(sec, "chFold:delivery")).join(" ¦ "))
                        if (b.collapsed.delivery === true) {
                            probe.check(probe.all(sec, "chDeliveryStory").length === 0, "folded: no plane story")
                        } else {
                            probe.check(!!probe.one(sec, "chDeliveryHead"), "chDeliveryHead")
                            probe.log("head drawn: " + probe.words(probe.one(sec, "chDeliveryHead")).join(" ¦ "))
                            probe.check(!!probe.one(sec, "chDeliveryStory") === (sh.story && sh.story.available === true), "chDeliveryStory when the plane is read")
                            probe.check(!!probe.one(sec, "chDeliveryShape") === (sh.shape && sh.shape.available === true || !probe.glance), "chDeliveryShape")
                            if (probe.glance) {
                                probe.check(probe.all(sec, "chDeliveryFlight").length === 0, "glance: no chDeliveryFlight")
                                probe.check(probe.all(sec, "chDeliveryImpact").length === 0, "glance: no chDeliveryImpact")
                                probe.check(probe.all(sec, "chDeliveryTrust").length === 0, "glance: no trust fold")
                            } else {
                                probe.check(!!probe.one(sec, "chDeliveryFlight"), "chDeliveryFlight")
                                probe.check(!!probe.one(sec, "chDeliveryImpact") === ((sh.impact && sh.impact.lines) || []).length > 0, "chDeliveryImpact when it has lines")
                                const tr = probe.one(sec, "chDeliveryTrust")
                                probe.check(!!tr, "chDeliveryTrust (closed)")
                                if (tr && probe.arg("--probe-trust", "") === "open") tr.open = true
                                for (const n of ["chDeliveryStory", "chDeliveryFlight", "chDeliveryImpact", "chDeliveryShape"])
                                    probe.log(n + ": " + probe.words(probe.one(sec, n)).join(" ¦ "))
                            }
                        }
                    }
                    // the plot's lines over the swing plane curve's row
                    const row = probe.one(root, "chTrace:swingPlaneCurve") || probe.one(root, "chTrace:overlay")
                    probe.check(!!row || lay.length === 0, "the swing plane curve's row is drawn")
                    const labels = row ? probe.all(row, "chPlaneLabel") : []
                    probe.log("plane labels on the plot: " + labels.map(l => l.text).join(" · "))
                    probe.check(lay.length === 0 || labels.length >= 1, "the plot labels its lines")
                } else {
                    probe.log("kicker: " + (se.kicker || ""))
                    probe.log("head: " + (se.headText || ""))
                    probe.log("band: shown " + (se.band && se.band.shown) + " n " + (se.band && se.band.n) + " · " + ((se.band && se.band.positions) || []).map(p => p.tag + " " + p.medianText + " " + p.halfText).join(" | "))
                    probe.log("varies: " + ((se.varies && se.varies.words) || ""))
                    probe.log("usual: " + JSON.stringify(se.usual || {}))
                    const fl = se.flight || {}
                    probe.log("flight: " + (fl.text || "") + " | attack: " + (fl.attackText || "") + " · pool " + JSON.stringify(fl.pool || {}))
                    probe.log("shapes: " + ((se.shapes && se.shapes.line) || "") + " · " + ((se.shapes && se.shapes.marks) || []).length + " marks")
                    for (const t of ((se.trust && se.trust.lines) || [])) probe.log("  trust: " + t)
                    probe.view = probe.one(root, "chSessionView")
                    const slot = probe.one(root, "chDeliverySessionSlot")
                    if (probe.glance) {
                        probe.check(!slot, "glance: no chDeliverySessionSlot")
                    } else {
                        probe.check(b.deliverySessionShown === true, "deliverySessionShown")
                        probe.check(!!slot, "chDeliverySessionSlot")
                        probe.section = probe.one(root, "chDeliverySession")
                        probe.check(!!probe.section, "chDeliverySession")
                        if (probe.section) for (const n of ["chDeliverySessionHead", "chDeliveryBand", "chDeliveryFlight", "chDeliveryShape"])
                            probe.log(n + ": " + probe.words(probe.one(probe.section, n)).join(" ¦ "))
                    }
                    const row = probe.one(root, "chEnvelope:swingPlaneCurve") || probe.one(root, "chEnvelope:overlay")
                    const labels = row ? probe.all(row, "chPlaneLabel") : []
                    probe.log("plane labels on the session plot: " + labels.map(l => l.text).join(" · "))
                }
                // the view at the top of its scroll
                const f = probe.view ? probe.flickOf(probe.view) : null
                if (f) f.contentY = 0
                probe.log("SURFACE " + Theme.colorSurface)
            } else if (probe.step === 6) {
                // the picture's stills land after the presenter's first delivery: wait for them (or 40 s)
                const frs = probe.all(probe.body, "chDeliveryPictureFrame")
                if (frs.length > 0 && frs.some(x => !x.hasImage) && probe.picWaits < 16) { probe.picWaits += 1; probe.step -= 1; return }
                for (const x of frs) probe.log("PICTURE DRAWN " + x.tag + " hasImage " + x.hasImage + " coach " + x.coachDrawn + " poly " + x.polyDrawn + " scale " + x.s.toFixed(4))
                // GEO lines in the page's own coordinates (the view's Flickable content), then the page grabbed in strips
                const f = probe.flickOf(probe.view)
                probe.flick = f
                const b = probe.body
                const names = ["chDeliverySection", "chDeliveryHead", "chDeliveryStory", "chDeliveryFlight", "chDeliveryImpact", "chDeliveryShape",
                               "chDeliveryTrust", "chDeliverySession", "chDeliverySessionHead", "chDeliveryBand", "chDeliveryUsual", "chScatter",
                               "chShotShapeGrid", "chTrace:swingPlaneCurve", "chTrace:handPlaneCurve", "chTrace:overlay", "chEnvelope:swingPlaneCurve",
                               "chEnvelope:overlay", "chPlaneLabel", "chFold:delivery", "chFold:deliverySession", "chHead", "chSessionHead", "chTraces",
                               "chSessionFigure", "chSummary", "chDeliveryPicture", "chDeliveryPictureFrame"]
                for (const n of names) for (const it of probe.all(b, n)) {
                    const p = f ? it.mapToItem(f.contentItem, 0, 0) : it.mapToItem(null, 0, 0)
                    probe.log("GEO " + (n === "chPlaneLabel" ? n + ":" + it.role : n === "chDeliveryPictureFrame" ? n + ":" + it.tag : n) + " " + Math.round(p.x) + " " + Math.round(p.y) + " " + Math.round(it.width) + " " + Math.round(it.height))
                }
                tick.running = false
                if (probe.png === "" || !f) { probe.finish(); return }
                const v = f.mapToItem(null, 0, 0)
                probe.log("PAGE " + Math.round(f.width) + " " + Math.round(f.contentHeight) + " viewport " + Math.round(v.x) + " " + Math.round(v.y) + " " + Math.round(f.width) + " " + Math.round(f.height))
                probe.part = 0
                f.contentY = 0
                strips.start()
            }
        }
    }
    // THE PAGE IN STRIPS: offscreen, an item grabbed off the window comes out on black (the window is what has the page's
    // own ground), so the window is grabbed once per viewport of the Flickable, scrolled down a viewport at a time; each
    // "PART i contentY" line says where its viewport rectangle sits on the page (the review sheets stitch them).
    property var flick: null
    property int part: 0
    function finish() { log(fails === 0 ? "RESULT PASS" : "RESULT FAIL (" + fails + ")"); Qt.exit(fails === 0 ? 0 : 1) }
    Timer {
        id: strips
        interval: 500; repeat: false
        onTriggered: {
            const f = probe.flick
            const ci = probe.Window.window.contentItem
            let top = probe.body
            while (top && top.parent && top.parent !== ci) top = top.parent
            const out = probe.png.replace(/\.png$/, "") + "_part" + probe.part + ".png"
            probe.log("PART " + probe.part + " " + Math.round(f.contentY) + " → " + out)
            top.grabToImage(function (res) {
                res.saveToFile(out)
                const most = Math.max(0, f.contentHeight - f.height)
                if (f.contentY >= most - 0.5 || probe.part >= 20) { probe.finish(); return }
                probe.part += 1
                f.contentY = Math.min(most, f.contentY + f.height - 40)
                strips.start()
            })
        }
    }
}
