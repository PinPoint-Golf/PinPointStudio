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

import QtQuick
import PinPointStudio

// SESSION — "what do my curves usually look like, and where do they part?" (design §2.2, A; D2, D3, D6, D9).
//   Analyse: the head built from the data (where the curves part, the facet, the fit's payoff), its
//     standfirst, the alignment note (§4.1), the mixed-club note, the co-occurrence caveat; the P-aligned
//     figure; "How far can I trust this?" (with the chest's probable peak said there, §2.2.1); the Controls fold
//     (Metrics, Window, Show: middle half · every shot · group fit, Line up: P-positions · impact (§2.2.1), Draw: small
//     multiples · overlaid (This shot's choice, shared), Facet: none by default, D3); up to three questions answered in place (a facet or band set, the
//     lit shots); then the sequence across the session (order barcode and peak-time dot plot), folded.
//   Replay (D6: follows the playhead): the head, the figure with "now" against "typical" in its column, and
//     the lead reading at the latest P-position the playhead has passed (PpChReplaySession).
// A session the presenter refuses (n < 3, nothing aligned) says its own sentence (session.why).
// Then, in Analyse, the wrist section ("Wrist", PpChWristSession) when some shot of the session carries a wrist
// reading and a wrist metric is among the curves on show (body.wristSessionShown; Mark, 16 Oct: the standard
// curves first, the domain read added where it means something). The glance never adds it.
// Then the posture section ("Posture", PpChPostureSession) by the same rule (body.postureSessionShown), Analyse only.
// objectNames: chSessionView, chSessionHead, chSessionControls, chFacetPicker (Analyse), chFacetPickerGlance.
Column {
    id: sv
    objectName: "chSessionView"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property string reg: "Analyse"
    property bool interactive: true
    property bool cast: false
    property real pageH: 500
    readonly property bool replay: reg === "Replay"
    readonly property var se: body ? body.tSession : ({})
    readonly property var tc: body ? body.tChoices : ({})
    readonly property var tn: body ? body.tNow : ({})
    readonly property bool available: !!se && se.available === true
    readonly property int nRows: Math.max(1, available ? (se.rows || []).length : 1)
    readonly property real headEst: !roles ? 0 : replay ? roles.u(90) + (body && body.rReplay && body.rReplay.readingId ? 0 : 0) : roles.u(240)
    // overlaid (This shot's Draw choice, shared): one plot, given the room the rows would have had, as This shot's
    readonly property bool overlaid: !replay && !!body && body.overlay
    readonly property real plotH: !roles ? 60
        : overlaid ? Math.max(roles.u(90), Math.min(roles.u(200), (pageH - headEst) * 0.8))
        : Math.max(roles.u(replay ? 44 : 56), Math.min(roles.u(replay ? 120 : 120), (pageH - headEst) / nRows - roles.u(14)
                                   - (se && se.band === "fit" ? roles.u(26) : 0)))
    spacing: roles ? roles.u(8) : 8

    function currentLabel(list) { const a = (list || []).filter(function (x) { return x.current }); return a.length ? a[0].label : "" }
    property string pickerOpen: ""
    // a long list stays one quiet line: the common choices, and the rest opening IN PLACE under it
    readonly property int facetShown: roles && roles.atLeast(width, "medium") ? 4 : 2
    readonly property var facetsMain: {
        const fs = tc.facets || [], out = [], cond = fs.filter(function (f) { return f.kind === "condition" })
        for (const f of fs) if (f.kind !== "condition") out.push(f)
        const cur = fs.filter(function (f) { return f.current && f.kind === "condition" })[0]
        const lead = cond.slice(0, facetShown)
        if (cur && lead.indexOf(cur) < 0) lead.push(cur)
        return out.slice(0, 2).concat(lead).concat(out.slice(2))
    }
    readonly property var sets: {
        if (!roles || !tc) return []
        const s = []
        s.push({ key: "preset", label: "Metrics", opens: true, open: pickerOpen === "preset", options: [{ key: "open", label: tc.presetTitle || "" }] })
        if ((tc.windows || []).length) s.push({ key: "window", label: "Window", opens: true, open: pickerOpen === "window", options: [{ key: "open", label: currentLabel(tc.windows) }] })
        s.push({ key: "band", label: "Show", current: ((tc.bands || []).filter(function (b) { return b.current })[0] || { id: "" }).id,
                 options: (tc.bands || []).map(function (b) { return { key: b.id, label: b.label } }) })
        // how the shots are lined up (§2.2.1): the session's P-positions, or each on its own clock at impact
        if ((tc.aligns || []).length)
            s.push({ key: "align", label: "Line up", current: ((tc.aligns || []).filter(function (a) { return a.current })[0] || { id: "p" }).id,
                     options: (tc.aligns || []).map(function (a) { return { key: a.id, label: a.label } }) })
        s.push({ key: "draw", label: "Draw", current: body && body.overlay ? "overlay" : "split",
                 options: [{ key: "split", label: "small multiples" }, { key: "overlay", label: "overlaid" }] })
        const more = (tc.facets || []).length > facetsMain.length
        s.push({ key: "facet", label: "Facet", current: ((tc.facets || []).filter(function (f) { return f.current })[0] || { id: "" }).id,
                 options: facetsMain.map(function (f) { return { key: f.id, label: f.label } })
                          .concat(more ? [{ key: "_more", label: pickerOpen === "facet" ? "fewer ▴" : "more ▾" }] : []) })
        return replay ? s.filter(function (x) { return x.key === "band" }) : s
    }
    function picked(set, opt) {
        if (!body) return
        if (set === "preset" || set === "window") { pickerOpen = pickerOpen === set ? "" : set; return }
        if (set === "facet" && opt === "_more") { pickerOpen = pickerOpen === "facet" ? "" : "facet"; return }
        else if (set === "band") { body.choose("band", opt); body.clearAsk(brush) }
        else if (set === "align") body.choose("align", opt)
        else if (set === "draw") body.choose("overlay", opt === "overlay")
        else if (set === "facet") { body.choose("facet", opt); body.clearAsk(brush) }
    }

    PpChText { objectName: "chSessionWhy"; visible: !sv.available && present; roles: sv.roles; sentence: sv.se ? sv.se.why : null
               font: sv.roles ? sv.roles.fBody : Qt.font({}); tone: sv.roles ? sv.roles.ink2 : "grey" }
    Column {
        objectName: "chSessionHead"
        visible: sv.available
        width: sv.width; spacing: sv.roles ? sv.roles.u(4) : 4
        Text { visible: !sv.cast && !!sv.se.kicker; text: sv.se.kicker || ""; font: sv.roles ? sv.roles.fKicker : Qt.font({}); color: sv.roles ? sv.roles.ink3 : "grey" }
        PpChText { roles: sv.roles; sentence: sv.se.head; measure: false; font: sv.roles ? sv.roles.fTitle : Qt.font({}); tone: sv.roles ? sv.roles.ink : "black"; lineHeight: 1.08 }
        PpChText { visible: !sv.replay && present; roles: sv.roles; sentence: sv.se.standfirst; font: sv.roles ? sv.roles.fStand : Qt.font({}); lineHeight: sv.roles ? sv.roles.r.leading : 1 }
        PpChText { visible: !sv.replay && present; roles: sv.roles; sentence: sv.se.alignNote; tone: sv.roles ? sv.roles.ink3 : "grey" }
        PpChText { visible: present; roles: sv.roles; sentence: sv.se.clubNote; tone: sv.roles ? sv.roles.ink2 : "grey" }
        PpChText { visible: !sv.replay && present; roles: sv.roles; sentence: sv.se.caveat; tone: sv.roles ? sv.roles.ink2 : "grey" }
    }
    // the glance keeps its one choice (Show) over the figure; Analyse folds its controls under the trust line
    PpChChoices {
        visible: sv.replay && sv.available && !sv.cast
        objectName: "chFacetPickerGlance"
        width: sv.width; roles: sv.roles; interactive: sv.interactive
        sets: sv.sets
        onPicked: (set, opt) => sv.picked(set, opt)
    }
    PpChSessionFigure {
        visible: sv.available
        width: sv.width
        roles: sv.roles; brush: sv.brush; interactive: sv.interactive
        presenter: sv.body ? sv.body.tracesP : null
        session: sv.available ? sv.se : null
        nowRows: sv.tn && sv.tn.valid ? (sv.tn.rows || []) : []
        playCanon: sv.tn && sv.tn.valid && sv.tn.canonMs !== null ? sv.tn.canonMs : NaN
        figMode: sv.replay ? "now" : "figures"
        pickedOrdinal: sv.body && sv.body.tShot ? (sv.body.tShot.ordinal || 0) : 0
        plotH: sv.plotH
        overlay: sv.overlaid
        onShotActivated: (o) => { if (sv.body) sv.body.pickOrdinal(o) }
    }
    // why readings are missing or held back across the session, said once and closed by default, under the figure
    // it explains (Mark, 17 Oct 2026; Session diagnostics carries the same fold, §7.4). The session is the picked
    // shot's folder.
    PpTrustSummaryFold {
        readonly property string pickedDir: sv.body && sv.body.tShot ? (sv.body.tShot.swingDir || "") : ""
        visible: sv.available && pickedDir !== "" && lines.length > 0 && !sv.cast
        width: sv.width; roles: sv.roles; scope: "session"
        topPadding: sv.roles ? sv.roles.u(6) : 6
        swingDir: pickedDir
        // the chest's probable peak (§2.2.1): how it was made and what it assumes, said once, here
        extraLines: sv.available ? (sv.se.trustLines || []) : []
    }
    // ═══ the controls, under the figure and its trust line, as on This shot (Mark, 17 Oct 2026) ═══
    // Foldable to one line that still says what is chosen. The glance has none (its Show line sits over the figure).
    readonly property bool controlsFolded: !!body && !!body.collapsed && body.collapsed.sessionControls === true
    Column {
        objectName: "chSessionControls"
        visible: !sv.replay && sv.available && !sv.cast
        width: sv.width
        spacing: sv.roles ? sv.roles.u(8) : 8
        PpChFoldHead {
            width: parent.width; roles: sv.roles; key: "sessionControls"; title: "Controls"; interactive: sv.interactive
            collapsed: sv.controlsFolded
            state_: [sv.tc.presetTitle || "", sv.tc.fromAddress ? "from address" : "", sv.currentLabel(sv.tc.windows), sv.currentLabel(sv.tc.bands),
                     sv.se && sv.se.alignedAt === "impact" ? "lined up at impact" : "", sv.overlaid ? "overlaid" : "",
                     sv.body && sv.body.facet ? sv.currentLabel(sv.tc.facets) : ""].filter(function (x) { return x !== "" }).join("  ·  ")
            onToggled: if (sv.body) sv.body.toggleSection("sessionControls")
        }
        PpChChoices {
            visible: !sv.controlsFolded
            objectName: "chFacetPicker"
            width: parent.width; roles: sv.roles; interactive: sv.interactive
            sets: sv.sets
            onPicked: (set, opt) => sv.picked(set, opt)
        }
        PpInlinePicker {
            visible: !sv.controlsFolded && sv.pickerOpen === "preset"
            open: visible; width: Math.min(parent.width, sv.roles ? sv.roles.u(360) : 360)
            roles: sv.roles; interactive: sv.interactive
            title: "Metrics to trace"
            items: visible ? (sv.tc.presets || []).map(function (p) { return { id: p.id, label: p.label, note: String(p.count) } }) : []
            current: sv.tc.preset || ""
            clearLabel: "The default for this view"
            itemObjectName: "chPresetOption"; clearObjectName: "chPresetDefault"
            onPicked: (id) => { if (sv.body) sv.body.choose("preset", id); sv.pickerOpen = "" }
        }
        PpInlinePicker {
            visible: !sv.controlsFolded && sv.pickerOpen === "window"
            open: visible; width: Math.min(parent.width, sv.roles ? sv.roles.u(360) : 360)
            roles: sv.roles; interactive: sv.interactive
            title: "The window, on the session's P-positions"
            items: visible ? (sv.tc.windows || []).map(function (w) { return { id: w.key, label: w.label } }) : []
            current: sv.tc.window || ""
            clearLabel: "The default: around the data"
            itemObjectName: "chWindowOption"; clearObjectName: "chWindowDefault"
            onPicked: (id) => { if (sv.body) sv.body.choose("window", id === "" ? "auto" : id); sv.pickerOpen = "" }
        }
        PpInlinePicker {
            visible: !sv.controlsFolded && sv.pickerOpen === "facet"
            open: visible; width: Math.min(parent.width, sv.roles ? sv.roles.u(420) : 420)
            roles: sv.roles; interactive: sv.interactive
            title: "Split the session by"
            items: visible ? (sv.tc.facets || []).filter(function (f) { return f.id !== "" }).map(function (f) { return { id: f.id, label: f.label } }) : []
            current: sv.body ? sv.body.facet : ""
            clearLabel: "none"
            itemObjectName: "chFacetOption"; clearObjectName: "chFacetNone"
            onPicked: (id) => { if (sv.body) { sv.body.choose("facet", id); sv.body.clearAsk(sv.brush) } sv.pickerOpen = "" }
        }
    }
    PpChQuestions {
        id: sessAsks
        visible: !sv.replay && sv.available && (sv.se.questions || []).length > 0
        width: sv.width; roles: sv.roles; interactive: sv.interactive
        questions: sv.available ? (sv.se.questions || []) : []
        active: sv.body ? sv.body.askedId : ""
        onAsked: (q) => { if (sv.body) sv.body.ask(q, sv.brush) }
    }
    // Replay: the lead reading at the P-position the playhead has passed (D6)
    PpChReplaySession {
        visible: sv.replay && ok
        width: sv.width; roles: sv.roles; brush: sv.brush; interactive: sv.interactive; cast: sv.cast
        presenter: sv.body ? sv.body.relationsP : null
        info: sv.body ? sv.body.rReplay : null
        onShotActivated: (o) => { if (sv.body) sv.body.pickOrdinal(o) }
    }
    // Analyse: the sequence across the session
    // the sections after the figure fold, as on This shot (Mark, 17 Oct 2026); folded, each keeps its first clause
    function firstClause(sentence) { const t = String(sentence && sentence.text ? sentence.text : ""); const m = t.match(/^[^;.]*/); return m ? m[0] : t }
    readonly property bool seqFolded: !!body && !!body.collapsed && body.collapsed.seqSession === true
    readonly property bool wristFolded: !!body && !!body.collapsed && body.collapsed.wristSession === true
    PpChFoldHead {
        visible: !sv.replay && sv.available && !!sv.se.sequence && sv.se.sequence.present === true
        width: sv.width; roles: sv.roles; key: "seqSession"; title: "The sequence across the session"
        sub: "kinematic sequence, every shot"; interactive: sv.interactive
        collapsed: sv.seqFolded
        state_: sv.available && sv.se.sequence ? sv.firstClause(sv.se.sequence.stand) : ""
        onToggled: if (sv.body) sv.body.toggleSection("seqSession")
    }
    PpChSeqSession {
        visible: !sv.replay && sv.available && !!sv.se.sequence && sv.se.sequence.present === true && !sv.seqFolded
        width: sv.width; roles: sv.roles; brush: sv.brush; interactive: sv.interactive
        sequence: sv.available ? sv.se.sequence : null
        onShotActivated: (o) => { if (sv.body) sv.body.pickOrdinal(o) }
    }
    // the wrist section: the checkpoints across the session, the score by shot, the style, what keeps firing
    Loader {
        objectName: "chWristSessionSlot"
        active: !!sv.body && sv.body.wristSessionShown && !!sv.roles
        visible: active
        width: sv.width
        sourceComponent: Column {
            spacing: sv.roles.u(14)
            PpChFoldHead { width: parent.width; roles: sv.roles; key: "wristSession"; title: "Wrist"; interactive: sv.interactive
                           collapsed: sv.wristFolded
                           state_: sv.body && sv.body.wSession ? sv.firstClause(sv.body.wSession.head) : ""
                           onToggled: if (sv.body) sv.body.toggleSection("wristSession") }
            PpChWristSession { visible: !sv.wristFolded; width: parent.width; roles: sv.roles; brush: sv.brush; body: sv.body; interactive: sv.interactive }
        }
    }
    // the posture section: the positions across the session, the pattern by shot, the departures, repeatability
    readonly property bool postureFolded: !!body && !!body.collapsed && body.collapsed.postureSession === true
    Loader {
        objectName: "chPostureSessionSlot"
        active: !!sv.body && sv.body.postureSessionShown && !!sv.roles
        visible: active
        width: sv.width
        sourceComponent: Column {
            spacing: sv.roles.u(14)
            PpChFoldHead { width: parent.width; roles: sv.roles; key: "postureSession"; title: "Posture"; interactive: sv.interactive
                           collapsed: sv.postureFolded
                           state_: sv.body && sv.body.pSession ? sv.firstClause(sv.body.pSession.head) : ""
                           onToggled: if (sv.body) sv.body.toggleSection("postureSession") }
            PpChPostureSession { visible: !sv.postureFolded; width: parent.width; roles: sv.roles; brush: sv.brush; body: sv.body; interactive: sv.interactive }
        }
    }
}
