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

// REPORT CONTENT IN THE DIAGNOSTIC MODEL (#60). Opens Settings → Swing model, selects a habit, a
// measure and a gate in turn, logs every field the inspector offers and grabs the inspector's whole
// column for each. With --probe-edit it also drives the new editors WITHOUT SAVING: types a bad goal
// into the live editor and logs the inline message, creates a drill and attaches it to the habit
// through the habit's own drill picker, and reads the gate's golfer words field. Nothing is saved, so
// the user's own pack on disk is never touched.
//
//   QT_QPA_PLATFORM=offscreen:configfile=<screen json> PinPointStudio --probe-qml <abs path>
//       --probe-out <abs dir> [--probe-edit]

import QtQuick
import QtQuick.Controls
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent
    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property bool edit: Qt.application.arguments.indexOf("--probe-edit") >= 0
    readonly property string out: arg("--probe-out", "")
    function log(s) { console.warn("PROBE: " + s) }
    function find(item, test) {
        if (!item) return null
        if (test(item)) return item
        for (const k of (item.children || [])) { const r = find(k, test); if (r) return r }
        return null
    }
    function browserOf(dm) {
        for (const o of (dm.resources || [])) if (o && typeof o.setField === "function") return o
        for (const o of (dm.data || []))      if (o && typeof o.setField === "function") return o
        return null
    }
    function logFields(tag) {
        const d = dm._inspectorDetail
        for (const s of (d.sections || [])) {
            if (s.kind === "fields") {
                for (const f of (s.rows || s.fields || []))
                    log(tag + " field " + f.field + " [" + f.kind + "] = " + JSON.stringify(f.value)
                        + (f.issues && f.issues.length ? "  ISSUES " + JSON.stringify(f.issues) : ""))
            } else if (s.title !== undefined) {
                log(tag + " section " + s.title + " (" + s.kind + (s.action ? ", action " + s.action : "") + ")"
                    + (s.rows ? " rows " + JSON.stringify(s.rows.map(r => r.id || r.label)) : ""))
            }
        }
    }
    function grab(name, next) {
        const insp = find(dm, i => i.fieldCommitted !== undefined && i.detail !== undefined && i.visible && i.width > 0)
        const sv = insp ? find(insp, i => i.contentChildren !== undefined && i instanceof ScrollView) : null
        const col = sv && sv.contentChildren.length ? sv.contentChildren[0] : insp
        log("grab sizes: inspector " + (insp ? insp.width : -1) + ", scroll " + (sv ? sv.width : -1)
            + ", column " + col.width + "x" + col.height)
        if (!col || out === "" || col.width <= 0 || col.height <= 0) { next(); return }
        col.grabToImage(function (res) { res.saveToFile(out + "/" + name + ".png"); log("grabbed " + name); next() })
    }
    property int step: 0
    property var dm: null
    property var mb: null
    property string newDrill: ""
    Timer {
        id: tick
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const root = probe.Window.window.contentItem
            if (probe.step === 1) {
                navController.navigate(AppScreen.Settings)
            } else if (probe.step === 2) {
                const ss = probe.find(root, i => typeof i.showMetricDetail === "function")
                if (!ss) { probe.log("no settings screen"); Qt.exit(1); return }
                ss.showMetricDetail("pelvisSway")       // opens Settings → Swing model
            } else if (probe.step === 3) {
                probe.dm = probe.find(root, i => typeof i.selectFresh === "function" && i._type !== undefined)
                if (!probe.dm) { probe.log("no diagnostic model"); Qt.exit(1); return }
                probe.mb = probe.browserOf(probe.dm)
                probe.log("browser " + (probe.mb ? "found" : "MISSING") + "; window " + probe.Window.window.width
                          + "x" + probe.Window.window.height + ", panel " + probe.dm.width)
                probe.dm._type = "characteristics"
            } else if (probe.step === 4) {
                probe.dm.selectFresh("characteristics", "s_posture")
            } else if (probe.step === 5) {
                probe.logFields("habit")
                tick.stop(); probe.grab("habit", () => tick.start())
            } else if (probe.step === 6) {
                if (probe.edit) {
                    const ed = probe.find(probe.dm, i => i.objectName === "fieldEditor:goal:text")
                    probe.log("goal editor " + (ed ? "found: " + ed.text : "MISSING"))
                    if (ed) ed.text = "Don't let your lower back arch at address, ever, whatever you do."
                    if (probe.mb && probe.mb.fieldIssues)
                        probe.log("fieldIssues goal " + JSON.stringify(probe.mb.fieldIssues("characteristics", "s_posture", "goal",
                                  "Don't let your lower back arch at address, ever, whatever you do.")))
                }
            } else if (probe.step === 7) {
                if (probe.edit) {
                    const iss = probe.find(probe.dm, i => i.objectName === "fieldIssues:goal")
                    probe.log("goal inline issues " + (iss ? (iss.visible ? "VISIBLE" : "hidden") : "MISSING"))
                    tick.stop(); probe.grab("habit-badgoal", () => tick.start())
                }
            } else if (probe.step === 8) {
                if (probe.edit && probe.mb) {
                    const ed = probe.find(probe.dm, i => i.objectName === "fieldEditor:goal:text")
                    if (ed) ed.text = "Set up with a natural curve in your lower back"
                    const r = probe.mb.createObject("drills", "Probe wall drill")
                    probe.newDrill = r.id || ""
                    probe.log("created drill " + JSON.stringify(r))
                    const cands = probe.mb.conditionDrillCandidates ? probe.mb.conditionDrillCandidates("s_posture", "") : []
                    probe.log("habit drill candidates include new: " + cands.some(c => c.id === probe.newDrill)
                              + " (" + cands.length + " candidates)")
                    probe.log("attach " + JSON.stringify(probe.mb.addDrillAnswers(probe.newDrill, "s_posture")))
                    probe.dm.selectFresh("characteristics", "s_posture")
                }
            } else if (probe.step === 9) {
                if (probe.edit) {
                    probe.logFields("habit-attached")
                    tick.stop(); probe.grab("habit-attached", () => tick.start())
                }
            } else if (probe.step === 10) {
                probe.dm._type = "measures"
                probe.dm.selectFresh("measures", "m_lumbarCurve")
            } else if (probe.step === 11) {
                probe.logFields("measure")
                tick.stop(); probe.grab("measure", () => tick.start())
            } else if (probe.step === 12) {
                probe.dm._type = "gates"
            } else if (probe.step === 14) {
                probe.dm.selectFresh("gates", "sequence.sightedBand")
            } else if (probe.step === 15) {
                probe.logFields("gate")
                tick.stop(); probe.grab("gate", () => tick.start())
            } else if (probe.step === 16) {
                probe.dm.selectFresh("gates", "ks.pelvis")
            } else if (probe.step === 17) {
                probe.logFields("reading")
                tick.stop(); probe.grab("reading", () => tick.start())
            } else if (probe.step === 18) {
                Qt.exit(0)
            }
        }
    }
}
