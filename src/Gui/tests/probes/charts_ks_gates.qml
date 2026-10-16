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
                SessionMode.enterAnalyse()
                // camera beside Charts, nothing else (the probe's config is a copy: this changes no real layout)
                const m = SessionMode.analyse
                for (const k of ["camera", "swing3d", "launchMonitor", "sessionDiagnostics", "table", "markup"])
                    ViewLayout.setPanel(m, k, k === "camera" && !probe.trustPanel)
                ViewLayout.setPanel(m, "charts", true)
                ViewLayout.setPanel(m, "trust", probe.trustPanel)
                ViewLayout.setArrangement(m, "split")
            } else if (probe.step === 4) {
                const bodies = []
                probe.walk(root, "chBody", bodies)
                probe.body = bodies.filter(b => probe.shown(b, root))[0] || bodies[0] || null
                if (!probe.body) { probe.log("no Charts body"); Qt.exit(1); return }
                probe.body.preset = "Kinematic sequence"
                probe.body.window = probe.arg("--probe-window", "down")     // P4 → P8 unless asked otherwise
                probe.body.tab = "shot"
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
                const rings = []; probe.walk(b, "chUnplaced:*", rings)
                for (const r of rings) if (probe.shown(r, root)) probe.log("dashed ring: " + r.objectName)
                const views = []; probe.walk(b, "chShotView*", views)
                if (probe.click !== "") {
                    const kind = probe.click.split(":")[0], subj = probe.click.split(":")[1] || ""
                    const name = kind === "figures" ? "chKeyFigures" : kind === "strip" ? "chSeqChain" : "chTraces"
                    const hits = []; probe.walk(b, name, hits)
                    const h = hits.filter(x => probe.shown(x, root))[0]
                    if (h) { h.trustClicked(subj); probe.log("clicked " + probe.click + " on " + name) }
                    else probe.log("no " + name + " to click")
                }
            } else if (probe.step === 6) {
                const inl = []; probe.walk(root, "trustInline", inl)
                for (const x of inl) if (x.visible) probe.log("in place: " + x.readings.length + " reading(s): " + x.readings.map(r => r.label + " (" + r.levelWord + ")").join(", "))
                const panels = []; probe.walk(root, "trustPanel", panels)
                for (const pnl of panels) {
                    probe.log("trust panel: " + (pnl.shot.kicker || "") + " — " + (pnl.shot.head || ""))
                    for (const r of (pnl.shot.readings || [])) probe.log("  " + r.label + " · " + r.levelWord + " · " + r.figure + " · " + r.gates.map(g => g.title + " [" + g.effectWord + "]").join("; "))
                    probe.log("  session: " + (pnl.session.head || "") + " | " + (pnl.session.gates || []).map(g => g.title + " " + g.countText).join("; "))
                    probe.log("  focused: '" + TrustFocus.subject + "' lit '" + pnl._lit + "'")
                }
            } else if (probe.step === 7) {
                if (probe.png === "") { Qt.exit(0); return }
                let card = probe.body
                if (probe.trustPanel) { while (card && card._defs === undefined) card = card.parent }
                else while (card && !(card.asideActive !== undefined && card.aside !== undefined)) card = card.parent
                const target = card || probe.body
                probe.log("grabbing " + (card ? "the Charts card" : "the Charts body") + " " + Math.round(target.width) + "×" + Math.round(target.height))
                target.grabToImage(function (res) { res.saveToFile(probe.png); Qt.exit(0) })
            }
        }
    }
}
