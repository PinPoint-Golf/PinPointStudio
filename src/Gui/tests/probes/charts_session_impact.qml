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

// END-TO-END: SESSION LINED UP AT IMPACT, OVERLAID, AND THE CHEST'S PROBABLE PEAK
// (docs/design/charts_panel_traces_relations.md §2.2.1).
//
//   load a session → pick shot N → Analyse → Charts alone, Session tab, "Kinematic sequence" → set the
//   choices (WITHOUT saving them) → report what the view says (alignment note, Controls, the chest's
//   sequence row, the trust lines, the bridges drawn) → grab the Charts card.
//
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path to this file> \
//       --probe-session <session dir> --probe-ordinal 2 [--probe-align p|impact] [--probe-draw split|overlay]
//       [--probe-band middle|every|fit] [--probe-fold open] [--probe-png <abs path>]
// Runs on a build without the feature too (the before sheet): the choices it lacks are skipped and said.
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
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "2"))
    readonly property string png: arg("--probe-png", "")

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
    function words(v) {
        if (!v) return ""
        if (typeof v === "string") return v
        if (v.runs) return v.runs.map(function (r) { return r.text }).join("")
        if (v.parts) return v.parts.map(function (r) { return r.text }).join("")
        return JSON.stringify(v).replace(/\{"[a-z]+":/g, "").slice(0, 300)
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
    property var held: null
    Timer {
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const root = probe.Window.window.contentItem
            if (probe.step === 1) {
                navController.navigate(AppScreen.Session)
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
                SessionMode.enterAnalyse()
                const m = SessionMode.analyse
                for (const k of ["camera", "swing3d", "launchMonitor", "sessionDiagnostics", "table", "markup", "trust"])
                    ViewLayout.setPanel(m, k, false)
                ViewLayout.setPanel(m, "charts", true)
            } else if (probe.step === 4) {
                const bodies = []
                probe.walk(root, "chBody", bodies)
                probe.body = bodies.filter(b => probe.shown(b, root))[0] || bodies[0] || null
                if (!probe.body) { probe.log("no Charts body"); Qt.exit(1); return }
                const b = probe.body
                b.preset = "Kinematic sequence"
                b.tab = probe.arg("--probe-tab", "session")      // session | shot
                b.band = probe.arg("--probe-band", "middle")
                b.overlay = probe.arg("--probe-draw", "split") === "overlay"
                if (b.align !== undefined) b.align = probe.arg("--probe-align", "p")
                else probe.log("this build has no Line up choice (before)")
                b.collapsed = ({})                  // every fold open: Controls and the sequence across the session
                const w = probe.arg("--probe-window", "")
                if (w !== "") b.window = w
            } else if (probe.step === 5) {
                const b = probe.body
                if (b.tab === "shot") {
                    // THIS SHOT: the headline, the KS tile and chain, the chest row's peak and bridge
                    const ts = b.tShot || {}
                    probe.log("shot head: " + probe.words(ts.head))
                    probe.log("shot stand: " + probe.words(ts.standfirst))
                    const sq = ts.sequence || {}
                    probe.log("tile verdict: " + (sq.verdictText || "") + " | chain: " + probe.words(sq.chain) + " | note: " + probe.words(sq.note))
                    for (const r of (sq.table || []))
                        probe.log("  tile " + r.segment + (r.warn ? " " + r.warn : "") + ": rank " + r.actualRank + (r.late ? " late" : "")
                                  + " | " + r.peakText + " | " + r.speedText + " | " + r.pctText)
                    const th = (ts.rows || []).filter(r => r.key === "thoraxAngularSpeed")[0]
                    if (th) probe.log("chest row: estimate " + (th.estimate ? th.estimate.t.length + " pts" : "none") + ", peaks "
                                      + (th.peaks || []).map(p => p.text + (p.probable ? " (probable)" : "")).join(", ")
                                      + ", figures " + (th.figures || []).map(f => f.label + "=" + f.text).join("; "))
                    const folds = []; probe.walk(b, "trustSummaryFold", folds)
                    for (const f of folds) if (probe.shown(f, root)) {
                        probe.log("trust fold: " + f.scope + " " + f.lines.length + " line(s)")
                        for (const l of f.lines) probe.log("  trust [" + (l.kind || "") + "] " + String(l.text || l.sentence || JSON.stringify(l)).slice(0, 300))
                        if (probe.arg("--probe-fold", "") === "open") f.open = true
                    }
                    return
                }
                const se = b.tSession || {}
                probe.log("available " + se.available + ", n " + se.n + ", alignedAt " + (se.alignedAt || "(none)"))
                probe.log("head: " + probe.words(se.head))
                probe.log("align note: " + probe.words(se.alignNote))
                probe.log("axis caption: " + (se.axisCaption || ""))
                const ch = []; probe.walk(b, "chFacetPicker", ch)
                for (const c of ch) if (probe.shown(c, root)) probe.log("controls: " + (c.sets || []).map(s => s.label + "=" + (s.current || "") + " [" + (s.options || []).map(o => o.label).join("|") + "]").join("; "))
                probe.log("windows: " + ((b.tChoices || {}).windows || []).map(w => w.key + "=" + w.label + (w.current ? "*" : "")).join(" | "))
                const seq = se.sequence || {}
                probe.log("sequence stand: " + probe.words(seq.stand))
                for (const g of (seq.segments || []))
                    probe.log("  " + g.word + (g.warn ? " " + g.warn : "") + ": " + g.figText + (g.warnShort ? " | " + g.warnShort : "")
                              + " | placed " + g.placed + (g.probable !== undefined ? ", probable " + g.probable : "")
                              + " | ranks " + (g.cells || []).map(c => c.text + (c.probable ? "*" : "") + (c.late ? "!" : "")).join(" "))
                for (const l of (se.trustLines || [])) probe.log("trust line [" + l.kind + "]: " + l.text)
                const rows = se.rows || []
                const th = rows.filter(r => r.key === "thoraxAngularSpeed")[0]
                if (th) {
                    const pn = (th.panels || [])[0] || {}
                    const est = (pn.others || []).filter(o => !!o.estimate).length
                    const medEst = (pn.medEst || []).filter(v => isFinite(v)).length
                    probe.log("chest row picked keys: " + Object.keys(th.picked || {}).join(",") + " ordinal " + (th.picked || {}).ordinal)
                    probe.log("chest row: picked estimate " + (th.picked && th.picked.estimate ? th.picked.estimate.t.length + " pts" : "none")
                              + ", others with an estimate " + est + ", dashed-median points " + medEst)
                }
                const ov = []; probe.walk(b, "chEnvelope:overlay", ov)
                probe.log("overlaid plot drawn: " + ov.filter(x => probe.shown(x, root)).length)
                const keys = []; probe.walk(b, "chSessionKey:*", keys)
                probe.log("overlay key: " + keys.filter(x => probe.shown(x, root)).map(k => k.objectName.split(":")[1]).join(", "))
                const folds = []; probe.walk(b, "trustSummaryFold", folds)
                for (const f of folds) if (probe.shown(f, root)) {
                    probe.log("trust fold: " + f.scope + " " + f.lines.length + " line(s)")
                    if (probe.arg("--probe-fold", "") === "open") f.open = true
                }
            } else if (probe.step === 6) {
                if (probe.png === "") { Qt.exit(0); return }
                // the whole Session view, scrolled or not: the flickable's content (the card shows only its viewport)
                const views = []; probe.walk(probe.body, probe.body.tab === "shot" ? "chShotView*" : "chSessionView", views)
                const view = views.filter(v => probe.shown(v, probe.Window.window.contentItem))[0]
                let flick = view
                while (flick && !(flick.contentHeight !== undefined && flick.contentItem !== undefined)) flick = flick.parent
                const target = flick ? flick.contentItem : (view || probe.body)
                probe.log("grabbing " + (flick ? "the scrolled content" : "the Session view") + " " + Math.round(target.width) + "×" + Math.round(target.height))
                // a C++-made content item has no QML engine to grab with: render it through a ShaderEffectSource in a
                // probe-made rectangle on the page colour, and grab that
                const holder = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { color: "' + probe.arg("--probe-bg", "#1a1a19") + '"; z: -1000; '
                                                  + 'property alias src: ses; ShaderEffectSource { id: ses; anchors.fill: parent; live: false; recursive: true } }', probe)
                holder.width = Math.ceil(target.width); holder.height = Math.ceil(target.height)
                holder.src.sourceItem = target
                holder.src.scheduleUpdate()
                probe.held = holder
            } else if (probe.step === 7) {
                if (!probe.held) return
                probe.held.grabToImage(function (res) { res.saveToFile(probe.png); Qt.exit(0) })
            }
        }
    }
}
