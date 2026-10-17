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

// END-TO-END: THE CHARTS PANEL READS THE KINEMATIC SEQUENCE'S GATE RECORDS
// (docs/design/trust_and_gates_design.md §9 stage 3).
//
//   load a session → pick shot N with the carousel's click path → enter Analyse → Charts on the
//   "Kinematic sequence" preset → report what the strip says, which stretches are shaded, which rings
//   are dashed, and the gates that fired (opened) → grab the Charts card.
//
// Run through --probe-qml (dev builds only), offscreen:
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path to this file> \
//       --probe-session <session dir> --probe-ordinal 4 [--probe-png <abs path>] [--probe-closed]
//   --probe-mode analyse | replay | glance — Analyse (default); a Replay the golfer opened (reads in depth);
//       the glance at the mat (Replay with autoReturnToCapture: the Replay register)
//   --probe-open first | <readingId> — on the Readings tab, click that row (the profile's own rowActivated)
//       and report the curve it opens
//   --probe-wrist — report the Wrist section's words (score, cells, standfirst, missing line) and the trust fold
//   --probe-panels camera,table — the panels beside Charts (default camera); --probe-region Delivery — the Table's
//       region; the camera's gate chip (overlayGateChip) and the Table's "Not shown" line (dataNotShown) are reported
//   --probe-stay — navigate, report, then leave the app open for a person to look (build/run-me/trust-inapp-check.sh)
// "PROBE:" lines carry the findings. Loading a session writes into it: use a scratch copy.

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
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "4"))
    readonly property string png: arg("--probe-png", "")
    // --probe-trust on: the Trust panel beside Charts (else Camera + Charts, the in-place fallback)
    readonly property bool trustPanel: arg("--probe-trust", "off") === "on"
    // --probe-preset <chart name> (default Kinematic sequence); --probe-tab shot|session|relations|kinds
    // --probe-click figures:<subject> | strip | chart:<subject> — the mark the probe clicks
    readonly property string click: arg("--probe-click", "")

    function log(s) { console.warn("PROBE: " + s) }
    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name || (name.endsWith("*") && item.objectName.startsWith(name.slice(0, -1)))) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
    }
    function shown(item, top) {
        let i = item
        while (i && i !== top) { if (!i.visible) return false; i = i.parent }
        return !!i
    }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    property int step: 0
    property var body: null
    Timer {
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const root = probe.Window.window.contentItem
            if (probe.step === 1) {
                navController.navigate(2)
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (probe.step === 2) {
                let found = false
                for (let i = 0; i < shots.count; ++i) {
                    const s = shots.objectAt(i)
                    if (s.ordinal === probe.ordinal) { SessionMode.enterReplay(s.shotId, s.swingDir); found = true }
                }
                probe.log("shot " + probe.ordinal + (found ? " picked" : " NOT in the carousel"))
                if (!found) Qt.exit(1)
            } else if (probe.step === 3) {
                const mode = probe.arg("--probe-mode", "analyse")
                if (mode === "analyse") SessionMode.enterAnalyse()
                else if (mode === "glance") SessionMode.autoReturnToCapture = true   // the auto-replay after a shot
                // camera beside Charts, nothing else (the probe's config is a copy: this changes no real layout)
                const m = mode === "analyse" ? SessionMode.analyse : SessionMode.replay
                const beside = probe.arg("--probe-panels", "camera").split(",")
                for (const k of ["camera", "swing3d", "launchMonitor", "sessionDiagnostics", "table", "markup"])
                    ViewLayout.setPanel(m, k, beside.indexOf(k) >= 0 && !probe.trustPanel)
                ViewLayout.setPanel(m, "charts", true)
                ViewLayout.setPanel(m, "trust", probe.trustPanel)
                ViewLayout.setArrangement(m, "split")
            } else if (probe.step === 4) {
                const bodies = []
                probe.walk(root, "chBody", bodies)
                probe.body = bodies.filter(b => probe.shown(b, root))[0] || bodies[0] || null
                if (!probe.body) { probe.log("no Charts body"); Qt.exit(1); return }
                probe.log("mode " + SessionMode.mode + " register " + probe.body.register)
                probe.log("window on first display: '" + probe.body.window + "'")
                probe.body.customStartUs = 0; probe.body.customEndUs = 376000; probe.body.window = "custom"
                probe.body.choose("preset", probe.arg("--probe-preset", "Kinematic sequence"))   // a chart change: must reset the window
                probe.log("window after the chart change: '" + probe.body.window + "'")
                const w = probe.arg("--probe-window", "auto")
                if (w !== "auto") probe.body.window = w
                probe.body.tab = probe.arg("--probe-tab", "shot")   // shot | session | readings | relations | kinds
                // --probe-fold-shut a,b: close those folds (the probe's settings are a copy)
                const shut = probe.arg("--probe-fold-shut", "")
                const c = {}
                for (const k of (shut === "" ? [] : shut.split(","))) c[k] = true
                probe.body.collapsed = c
            } else if (probe.step === 5) {
                const b = probe.body
                const chains = []; probe.walk(b, "chSeqChain", chains)
                const chain = chains.filter(c => probe.shown(c, root))[0]
                if (chain) {
                    const texts = []
                    for (const k of chain.children) if (k.text !== undefined && k.visible && k.text !== "") texts.push(String(k.text).replace(/<[^>]*>/g, ""))
                    probe.log("strip: " + texts.join(" | "))
                }
                const traces = []; probe.walk(b, "chTrace:*", traces)
                probe.log("traces: " + traces.map(t => t.objectName + (t.r && t.r.blind ? "(blind " + t.r.blind.length + ")" : "")).join(", "))
                const blinds = []; probe.walk(b, "chBlind:*", blinds)
                for (const x of blinds) probe.log("shaded: " + x.objectName + " '" + x.text + "' visible=" + x.visible + " px " + Math.round(x.xa) + "…" + Math.round(x.xb))
                const figWhy = []; probe.walk(b, "chFigWhy:*", figWhy)
                for (const f of figWhy) probe.log("figure-column link: " + f.objectName + " '" + String(f.text) + "'")
                if (figWhy.length && probe.click === "figcol") { const fw = figWhy[0]; for (let p = fw; p; p = p.parent) if (p.trustClicked !== undefined) { p.trustClicked(fw.objectName.split(":")[1] + ":" + fw.objectName.split(":")[2]); probe.log("clicked the figure column"); break } }
                // --probe-fold open: open the tail's "How far can I trust this?" fold before the grab
                const folds = []; probe.walk(b, "trustSummaryFold", folds)
                for (const f of folds) if (probe.shown(f, root)) {
                    probe.log("trust fold: " + f.scope + " " + f.lines.length + " line(s); closed: '" + (f.summary && f.summary.tail || "") + "'")
                    if (probe.arg("--probe-fold", "") === "open") f.open = true
                }
                const rings = []; probe.walk(b, "chUnplaced:*", rings)
                for (const r of rings) if (probe.shown(r, root)) probe.log("dashed ring: " + r.objectName)
                const views = []; probe.walk(b, "chShotView*", views)
                const filt = probe.arg("--probe-filter", "")   // --probe-filter <text>: the Readings tab's name filter
                if (filt !== "") { const rvs = []; probe.walk(b, "chReadingsView", rvs); for (const rv of rvs) rv.filter = filt }
                const cautions = []; probe.walk(b, "chProfileCaution", cautions)
                for (const c of cautions) if (probe.shown(c, root)) probe.log("caution: '" + c.text + "'")
                const open = probe.arg("--probe-open", "")
                if (open !== "") {
                    const profs = []; probe.walk(b, "chProfile", profs)
                    const rows = []; for (const pf of profs) if (probe.shown(pf, root)) probe.walk(pf, "chApartRow*", rows)
                    probe.log("readings rows shown: " + rows.length)
                    const row = open === "first" ? rows[0] : rows.filter(r => r.readingId === open)[0]
                    if (row) {
                        let pf = row; while (pf && pf.objectName !== "chProfile") pf = pf.parent
                        pf.rowActivated(row.readingId)
                        probe.log("clicked reading '" + row.readingId + "' → openId '" + b.openId + "'")
                    } else probe.log("no readings row '" + open + "'")
                }
                if (Qt.application.arguments.indexOf("--probe-wrist") >= 0) {
                    probe.log("presets: " + (b.tChoices.presets || []).map(p => p.id + "=" + p.label).join(" | "))
                    probe.log("wrist on show " + b.wristOnShow + ", shot shown " + b.wristShotShown + ", session shown " + b.wristSessionShown)
                    const sc = b.wShot && b.wShot.score ? b.wShot.score : {}
                    probe.log("wrist score: text '" + sc.scoreText + "' line '" + sc.line + "' trust '" + (sc.trust || "") + "'")
                    const pr = b.wShot && b.wShot.profile ? b.wShot.profile : {}
                    for (const r of (pr.rows || [])) probe.log("  wrist row " + r.key + ": " + (r.cells || []).map(c => c.text !== undefined ? c.text : c.valueText).join(" "))
                    probe.log("wrist missing: '" + (pr.missingText || b.wShot.missingText || "") + "'")
                    probe.log("wrist standfirst: '" + JSON.stringify(b.wStandfirst).replace(/\{"[a-z]+":/g, "").slice(0, 400) + "'")
                }
                if (probe.click !== "") {
                    const kind = probe.click.split(":")[0], subj = probe.click.split(":")[1] || ""
                    const name = kind === "figures" ? "chKeyFigures" : kind === "strip" ? "chSeqChain" : "chTraces"
                    const hits = []; probe.walk(b, name, hits)
                    const h = hits.filter(x => probe.shown(x, root))[0]
                    if (h) { h.trustClicked(subj); probe.log("clicked " + probe.click + " on " + name) }
                    else probe.log("no " + name + " to click")
                }
            } else if (probe.step === 6) {
                const b = probe.body
                if (probe.arg("--probe-open", "") !== "") {
                    const o = b.rOpen || {}
                    probe.log("open row: " + (o.readingId || "none") + " '" + (o.label || "") + "' curve " + (o.curve ? "yes" : "no") + " keys " + Object.keys(o).join(","))
                    const opened = []; probe.walk(b, "chApartRowOpen", opened)
                    probe.log("rows drawn open: " + opened.filter(x => probe.shown(x, root)).length)
                }
                if (Qt.application.arguments.indexOf("--probe-wrist") >= 0) {
                    const secs = []; probe.walk(b, "chWristSection", secs)
                    for (const sec of secs) if (probe.shown(sec, root)) {
                        const tx = []
                        const grabText = function (it) { if (!it || !it.visible) return; if (it.text !== undefined && String(it.text) !== "") tx.push(String(it.text).replace(/<[^>]*>/g, "")); for (const k of (it.children || [])) grabText(k) }
                        grabText(sec)
                        probe.log("wrist section words: " + tx.join(" ¦ "))
                    }
                }
                const inl = []; probe.walk(root, "trustInline", inl)
                for (const x of inl) if (x.visible) probe.log("in place: " + x.readings.length + " reading(s): " + x.readings.map(r => r.label + " (" + r.levelWord + ")").join(", "))
                const panels = []; probe.walk(root, "trustPanel", panels)
                for (const pnl of panels) {
                    probe.log("trust panel: " + (pnl.shot.kicker || "") + " — " + (pnl.shot.head || ""))
                    for (const r of (pnl.shot.readings || [])) probe.log("  " + r.label + " · " + r.levelWord + " · " + r.figure + " · " + r.gates.map(g => g.title + " [" + g.effectWord + "]").join("; "))
                    probe.log("  session: " + (pnl.session.head || "") + " | " + (pnl.session.gates || []).map(g => g.title + " " + g.countText).join("; "))
                    probe.log("  focused: '" + TrustFocus.subject + "' lit '" + pnl._lit + "'")
                }
                const chips = []; probe.walk(root, "overlayGateChip", chips)
                for (const c of chips) probe.log("camera gate chip: visible=" + probe.shown(c, root) + " '" + c.text + "'")
                const viewers = []; probe.walk(root, "dataNotShown", viewers)
                for (const v of viewers) {
                    let dv = v; while (dv && dv.persistRegion === undefined) dv = dv.parent   // PpDataViewer (the probe's settings are a copy)
                    const region = probe.arg("--probe-region", "")
                    if (dv && region !== "") { dv.persistRegion(region); probe.log("table region → " + region) }
                }
            } else if (probe.step === 7) {
                if (Qt.application.arguments.indexOf("--probe-stay") >= 0) running = false
                const nots = []; probe.walk(probe.Window.window.contentItem, "dataNotShown", nots)
                for (const v of nots) {
                    const tx = []
                    const grab = function (it) { if (!it || !it.visible) return; if (it.text !== undefined && String(it.text) !== "") tx.push(String(it.text).replace(/<[^>]*>/g, "")); for (const k of (it.children || [])) grab(k) }
                    grab(v)
                    probe.log("table not-shown: visible=" + v.visible + " '" + tx.join(" ") + "'")
                }

                if (!running) { probe.log("left open"); return }
                if (probe.png === "") { Qt.exit(0); return }
                let card = probe.body
                if (probe.trustPanel) { while (card && card._defs === undefined) card = card.parent }
                else while (card && !(card.asideActive !== undefined && card.aside !== undefined)) card = card.parent
                const target = card || probe.body
                const cs = []; probe.walk(target, "chProfileCaution", cs)
                for (const c of cs) if (probe.shown(c, target)) { const q = c.mapToItem(target, 0, 0); probe.log("caution at " + Math.round(q.x) + "," + Math.round(q.y) + " '" + c.text + "'") }
                probe.log("grabbing " + (card ? "the Charts card" : "the Charts body") + " " + Math.round(target.width) + "×" + Math.round(target.height))
                target.grabToImage(function (res) { res.saveToFile(probe.png); Qt.exit(0) })
            }
        }
    }
}
