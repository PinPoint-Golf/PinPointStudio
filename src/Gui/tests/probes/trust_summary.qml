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

// END-TO-END: "HOW FAR CAN I TRUST THIS?" (docs/design/trust_and_gates_design.md §7.4, stage 5).
//
//   load a session → pick shot N → Analyse with Session diagnostics (and, with --probe-trust on, the Trust
//   panel) → THIS SHOT or SESSION: report the fold's closed line (its tail), then open it (--probe-open on)
//   and report every line; with the panel on, report its head (the same statement) → grab the stage.
//
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path> --probe-session <dir> --probe-ordinal 4 \
//       [--probe-mode analyse|replay] [--probe-tab shot|session] [--probe-open on|off] [--probe-trust on|off] [--probe-png <abs path>]
// "PROBE:" lines carry the findings. Loading a session regrades and writes into it: use a scratch copy.

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
    readonly property bool trustPanel: arg("--probe-trust", "off") === "on"
    readonly property bool openFold: arg("--probe-open", "off") === "on"
    readonly property string tab: arg("--probe-tab", "shot")
    readonly property string mode: arg("--probe-mode", "analyse")     // analyse | replay (the register)

    function log(s) { console.warn("PROBE: " + s) }
    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name || (name.endsWith("*") && String(item.objectName).startsWith(name.slice(0, -1)))) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
    }
    function shown(item, top) {
        let i = item
        while (i && i !== top) { if (!i.visible) return false; i = i.parent }
        return !!i
    }
    function texts(item, out) {
        if (!item) return
        if (item.text !== undefined && item.visible && String(item.text) !== "") out.push(String(item.text).replace(/<[^>]*>/g, ""))
        for (const k of (item.children || [])) texts(k, out)
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
    property int waits: 0
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
                for (let i = 0; i < shots.count; ++i) {
                    const s = shots.objectAt(i)
                    if (s.ordinal === probe.ordinal) SessionMode.enterReplay(s.shotId, s.swingDir)
                }
            } else if (probe.step === 3) {
                if (probe.mode === "analyse") SessionMode.enterAnalyse()
                const m = probe.mode === "analyse" ? SessionMode.analyse : SessionMode.mode
                for (const k of ["camera", "swing3d", "launchMonitor", "charts", "table", "markup"]) ViewLayout.setPanel(m, k, false)
                ViewLayout.setPanel(m, "sessionDiagnostics", true)
                ViewLayout.setPanel(m, "trust", probe.trustPanel)
                ViewLayout.setArrangement(m, "split")
            } else if (probe.step === 4) {
                const bodies = []; probe.walk(root, "sdBody", bodies)
                if (bodies.length && probe.tab === "session") bodies[0].tab = "session"
            } else if (probe.step === 5) {
                // the regrade runs on activation and the series model loads in the background: wait for the fold
                const folds = []; probe.walk(root, "trustSummaryFold", folds)
                const fold = folds.filter(f => probe.shown(f, root) && f.scope === probe.tab)[0]
                if (!fold && probe.waits < 16) { probe.waits += 1; probe.step -= 1; return }
                if (!fold) { probe.log("NO FOLD SHOWN on " + probe.tab + " (folds: " + folds.length + ")"); return }
                probe.log("fold (" + probe.tab + ", closed): " + (fold.summary ? fold.summary.tail : "?") + " · open=" + fold.open
                          + " · lines=" + fold.lines.length + " · height " + Math.round(fold.height))
                if (probe.openFold) fold.open = true
            } else if (probe.step === 6) {
                const folds = []; probe.walk(root, "trustSummaryFold", folds)
                const fold = folds.filter(f => probe.shown(f, root) && f.scope === probe.tab)[0]
                if (fold) for (const l of fold.lines) probe.log("  [" + l.kind + "] " + l.text)
                // the first gated line's Why: in place when the Trust panel is off (the call its MouseArea makes)
                const g = fold ? fold.lines.find(l => l.kind === "gated" && l.subject) : null
                if (g && probe.openFold && !TrustFocus.open(g.subject, g.swingDir)) fold.openSubject = g.subject
                if (g && probe.openFold) probe.log("Why on " + g.subject + (probe.trustPanel ? " → Trust panel ('" + TrustFocus.subject + "')" : " → in place"))
                const old = []; probe.walk(root, "sdNotJudgedLine", old)
                probe.log("old not-judged line present: " + (old.filter(o => probe.shown(o, root)).length > 0))
                const panels = []; probe.walk(root, "trustPanel", panels)
                for (const p of panels) {
                    const heads = []; probe.walk(p, "trustHead", heads)
                    const t = []; probe.texts(heads[0], t)
                    probe.log("trust panel head: " + t.join(" | "))
                    const ss = []; probe.walk(p, "trustSession", ss)
                    const u = []; probe.texts(ss[0], u)
                    probe.log("trust panel session: " + u.slice(0, 3).join(" | "))
                }
            } else if (probe.step === 7) {
                const inl = []; probe.walk(root, "trustInlineFor", inl)
                for (const x of inl) if (x.visible && x.open) probe.log("in place: open for " + x.subject + ", height " + Math.round(x.height))
            } else if (probe.step === 8) {
                if (probe.png === "") { Qt.exit(0); return }
                const bodies = []; probe.walk(root, "sdBody", bodies)
                let t = bodies[0] || root
                while (t && t._defs === undefined) t = t.parent      // the whole stage (both panels)
                const target = t || bodies[0] || root
                probe.log("grabbing " + Math.round(target.width) + "×" + Math.round(target.height))
                target.grabToImage(function (res) { res.saveToFile(probe.png); Qt.exit(0) })
            }
        }
    }
}
