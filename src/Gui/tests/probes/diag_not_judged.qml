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

// END-TO-END: SESSION DIAGNOSTICS SAYS WHICH GATE KEPT A FINDING FROM BEING JUDGED
// (docs/design/trust_and_gates_design.md §4.4, §7.0, stage 4).
//
//   load a session → pick shot N → Analyse with Session diagnostics (and, with --probe-trust on, the Trust
//   panel) → THIS SHOT: report the not-judged groups ("Late pelvis rotation: pelvis peak out of sight"),
//   click the first group's Why → in place (Trust off) or the Trust panel's reading (Trust on, with the
//   findings it blocked) → grab the panel(s).
//
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path> --probe-session <dir> --probe-ordinal 4 \
//       [--probe-trust on] [--probe-png <abs path>] [--probe-tab session]
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
    readonly property string tab: arg("--probe-tab", "shot")

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
                navController.navigate(AppScreen.Session)
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (probe.step === 2) {
                for (let i = 0; i < shots.count; ++i) {
                    const s = shots.objectAt(i)
                    if (s.ordinal === probe.ordinal) SessionMode.enterReplay(s.shotId, s.swingDir)
                }
            } else if (probe.step === 3) {
                SessionMode.enterAnalyse()
                const m = SessionMode.analyse
                for (const k of ["camera", "swing3d", "launchMonitor", "charts", "table", "markup"]) ViewLayout.setPanel(m, k, false)
                ViewLayout.setPanel(m, "sessionDiagnostics", true)
                ViewLayout.setPanel(m, "trust", probe.trustPanel)
                ViewLayout.setArrangement(m, "split")
            } else if (probe.step === 4) {
                const bodies = []; probe.walk(root, "sdBody", bodies)
                if (bodies.length && probe.tab === "session") bodies[0].tab = "session"
            } else if (probe.step === 5) {
                // the regrade (ledger schema 8) runs on activation: wait for the groups to appear, up to ~40 s
                const groups = []; probe.walk(root, "sdNotJudged:*", groups)
                if (groups.length === 0 && probe.tab === "shot" && probe.waits < 16) { probe.waits += 1; probe.step -= 1; return }
                const boxes = []; probe.walk(root, "sdNotJudged", boxes)
                const box = boxes.filter(b => probe.shown(b, root))[0]
                probe.log("not-judged groups: " + groups.length + (box ? "" : " (box hidden)"))
                for (const g of groups) {
                    const t = []; probe.walk(g, "", t)
                    for (const k of g.children) if (k.text !== undefined) probe.log("  [w " + Math.round(k.width) + " of " + Math.round(g.width) + "] " + String(k.text).replace(/<[^>]*>/g, ""))
                }
                const notes = []; probe.walk(root, "sdNotMeasuredNote", notes)
                for (const n of notes) for (const k of n.children) if (k.text !== undefined && k.text !== "") probe.log("note: " + String(k.text).replace(/<[^>]*>/g, "").slice(0, 300))
                if (groups.length && box) {
                    const first = groups[0]
                    for (const k of first.children) if (k.text !== undefined) {
                        // the group's Why: open its reason (the same call its MouseArea makes)
                        if (!TrustFocus.open(first.modelData.subject, first.modelData.swingDir)) box.openSubject = first.modelData.subject
                        probe.log("opened " + first.modelData.subject + (probe.trustPanel ? " in the Trust panel" : " in place"))
                        break
                    }
                }
                if (probe.tab === "session") {
                    const caps = []; probe.walk(root, "*", caps)
                    let n = 0
                    for (const c of caps) if (c.text !== undefined && String(c.text).indexOf("judged on") >= 0 && n < 8) { probe.log("caption: " + String(c.text).replace(/<[^>]*>/g, "").slice(0, 240)); n++ }
                }
            } else if (probe.step === 7) {
                const inl = []; probe.walk(root, "trustInlineFor", inl)
                for (const x of inl) if (x.visible) probe.log("in place: open for " + x.subject + " height " + Math.round(x.height))
                const panels = []; probe.walk(root, "trustPanel", panels)
                for (const p of panels) {
                    for (const r of (p.shot.readings || [])) probe.log("trust: " + r.label + " · " + r.levelWord + " · blocks: " + (r.blocks || []).join(", "))
                    probe.log("trust focused '" + TrustFocus.subject + "'")
                }
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
