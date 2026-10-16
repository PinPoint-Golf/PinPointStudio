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

// THIS SHOT — "what did this swing's curves do, and how does it differ from my usual swing?" (design §2.1).
// One view, drawn in either register; the register chooses how much it says, never its encodings.
//   Replay (glance): the answer head, the key figures with their place words, the sequence chain, and the
//     traces with the readings AT THE PLAYHEAD; click or drag seeks.
//   Analyse (read): the head, key figures and the quiet controls — preset and its metrics on/off ("Custom"),
//     window (segment chips, Full recording, the data window, Custom from the brush), Against and its
//     Difference, small multiples / overlay, P dots, cursor — fold into a left rail at wide; the window header
//     and brush; the traces (peaks with σ, P dots by verdict with their key in words, honesty runs, hover
//     crosshair with its in-place readout, the reference and its difference strip); then the Summary
//     (window-scoped PEAK / Δ / PK RATE, partial, σ provenance, ⚠), the per-segment Sequence table (% of
//     pro), "Apart from the session" with its questions, and every reading against the session (D5), a row
//     opening its curve or run in place.
//   The wrist section (PpChWristShotSection, "Wrist"), under the traces in either register, when this shot carries a
//     wrist reading and a wrist metric is among the curves on show (body.wristShotShown): the standard curves first,
//     the domain read added where it means something (Mark, 16 Oct).
// Every word and number is a presenter's (body.t* = ChartsTracesPresenter, body.r* = ChartsRelationsPresenter,
// each bound ONCE on the body). The presenters' own empty / refusal sentences are what an empty view says.
// objectName: chShotView.
Column {
    id: sv
    objectName: "chShotView"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property string reg: "Analyse"
    property bool interactive: true
    property bool cast: false
    property real pageH: 500
    // renders only (the register's demo state): a held hover, an open picker
    property var demoHoverMs: undefined
    property string openPicker: ""               // "" | "preset" | "against"
    readonly property bool replay: reg === "Replay"

    readonly property var ts: body ? body.tShot : ({})
    readonly property var tc: body ? body.tChoices : ({})
    readonly property var tr: body ? body.tRef : ({})
    readonly property var tn: body ? body.tNow : ({})
    readonly property var rs: body ? body.rShot : ({})
    readonly property bool available: !!ts && ts.available === true
    readonly property var rows: available ? (ts.rows || []) : []
    readonly property bool hasSeq: available && !!ts.sequence && (ts.sequence.links || []).length > 0
    // ── WHY, ON DEMAND (trust_and_gates_design.md §7.0, TrustFocus): a click on a claim's mark opens its reading
    //    in the Trust panel when that is on, else in place under the claim (origin "figures" | "strip" | "chart").
    //    subject "" = every sequence reading a gate touched.
    property string trustSubject: ""
    property string trustOrigin: ""
    function openTrust(subject, origin) {
        if (TrustFocus.open(subject, ts.swingDir || "")) return
        const same = trustOrigin === origin && trustSubject === subject
        trustSubject = same ? "" : subject
        trustOrigin = same ? "" : origin
    }
    function trustReadings(origin) {
        if (!available || trustOrigin !== origin) return []
        return (ts.trust || []).filter(function (r) {
            return sv.trustSubject === "" ? String(r.subject).indexOf("ks.") === 0 : r.subject === sv.trustSubject })
    }
    readonly property string _pickedDir: ts.swingDir || ""
    on_PickedDirChanged: { trustSubject = ""; trustOrigin = "" }
    readonly property real em: roles ? roles.em : 14
    readonly property bool rail: !replay && !!roles && roles.shows("traceRail", width)
    readonly property real railW: Math.round(27 * em)
    readonly property real gut: roles ? roles.gutter(width) : 14
    readonly property real mainW: rail ? width - railW - 2 * gut : width
    spacing: roles ? roles.u(8) : 8

    // ── sizes from the data and the height given, never from our own laid-out height ──
    readonly property int nRows: body && body.overlay ? 1 : Math.max(1, rows.length)
    readonly property real headEst: !roles ? 0 : replay
        ? (cast ? 0 : roles.u(20)) + roles.u(30) + ((rs.keyFigures && (rs.keyFigures.items || []).length) ? roles.u(70) : 0)
          + (hasSeq ? roles.u(26) + (ts.note && ts.note.text ? roles.u(36) : 0) : 0) + roles.u(76)
        : rail ? roles.u(110) : roles.u(300)
    readonly property real plotH: !roles ? 60
        : body && body.overlay ? Math.max(roles.u(90), Math.min(roles.u(200), (pageH - headEst) * 0.8))
        : Math.max(roles.u(replay ? 44 : 56), Math.min(roles.u(replay ? 150 : 130), (pageH - headEst) / nRows - roles.u(14)
                   - (body && body.difference && tr.active ? roles.u(36) : 0)))

    // ── the controls, as quiet lines (Analyse) ──
    function againstLabel() { const a = (tc.against || []).filter(function (x) { return x.current }); return a.length ? a[0].label : "" }
    function windowLabel() { const a = (tc.windows || []).filter(function (x) { return x.current }); return a.length ? a[0].label : "" }
    readonly property var sets: {
        if (!roles || !tc) return []
        const s = []
        s.push({ key: "preset", label: "Metrics", opens: true, open: openPicker === "preset" || pickerOpen === "preset",
                 options: [{ key: "open", label: tc.presetTitle || "" }] })
        if ((tc.metrics || []).length > 1)
            s.push({ key: "metric", label: tc.custom ? "Custom" : "Showing", multi: true,
                     options: (tc.metrics || []).map(function (m) { return { key: m.key, label: m.label, on: m.on, swatch: roles.hue(m.key) } }) })
        // the window (F11-F13): Full recording, the swing, the data window, back/downswing and Custom as chips;
        // the adjacent P-position pairs open IN PLACE (the current one, if any, named on the opener)
        const hasWindows = (tc.windows || []).length > 0
        const segs = (tc.windows || []).filter(function (w) { return String(w.key).indexOf("seg:") === 0 })
        const segCur = segs.filter(function (w) { return w.current })[0]
        if (hasWindows) s.push({ key: "window", label: "Window", current: tc.window || "",
                 options: (tc.windows || []).filter(function (w) { return String(w.key).indexOf("seg:") !== 0 }).map(function (w) { return { key: w.key, label: w.label } })
                          .concat(segs.length ? [{ key: "_segs", label: (segCur ? segCur.label + "  " : "between P-positions  ") + (pickerOpen === "segs" ? "▴" : "▾") }] : []) })
        s.push({ key: "against", label: "Against", opens: true, open: openPicker === "against" || pickerOpen === "against",
                 options: [{ key: "open", label: againstLabel() }] })
        if (tr && tr.active) s.push({ key: "difference", label: "Difference", current: tc.difference ? "on" : "off",
                                      options: [{ key: "off", label: "off" }, { key: "on", label: tr.diffLabel || "on" }] })
        s.push({ key: "draw", label: "Draw", current: body && body.overlay ? "overlay" : "split",
                 options: [{ key: "split", label: "small multiples" }, { key: "overlay", label: "overlaid" }] })
        s.push({ key: "toggles", label: "Show", multi: true,
                 options: [{ key: "dots", label: "P dots", on: !!body && body.showDots }, { key: "cursor", label: "cursor", on: !!body && body.showCursor }] })
        return s
    }
    property string pickerOpen: ""
    function picked(set, opt) {
        if (!body) return
        if (set === "preset" || set === "against") { pickerOpen = pickerOpen === set ? "" : set; return }
        if (set === "window" && opt === "_segs") { pickerOpen = pickerOpen === "segs" ? "" : "segs"; return }
        if (set === "metric") body.toggleMetric(opt)
        else if (set === "window") body.choose("window", opt)
        else if (set === "difference") body.choose("difference", opt === "on")
        else if (set === "draw") body.choose("overlay", opt === "overlay")
        else if (set === "toggles") { if (opt === "dots") body.choose("showDots", !body.showDots); else body.choose("showCursor", !body.showCursor) }
    }

    // ═══ the head block: in the left rail at wide, above the figure otherwise ═══
    Column {
        id: headCol
        parent: sv.rail ? railHead : headSlot
        width: parent ? parent.width : 0
        spacing: sv.roles ? sv.roles.u(8) : 8
        // nothing to read: the presenter's own sentence (S1 no swing, S2 no traces)
        PpChText { objectName: "chShotEmpty"; visible: !sv.available && present; roles: sv.roles; sentence: sv.ts ? sv.ts.empty : null
                   font: sv.roles ? sv.roles.fBody : Qt.font({}); tone: sv.roles ? sv.roles.ink2 : "grey" }
        Column {
            objectName: "chHead"
            visible: sv.available
            width: parent.width; spacing: sv.roles ? sv.roles.u(4) : 4
            Text { visible: !sv.cast && !!sv.ts.kicker; width: parent.width; wrapMode: Text.WordWrap; text: sv.ts.kicker || ""
                   font: sv.roles ? sv.roles.fKicker : Qt.font({}); color: sv.roles ? sv.roles.ink3 : "grey" }
            PpChText { roles: sv.roles; sentence: sv.ts.head; measure: false; font: sv.roles ? sv.roles.fTitle : Qt.font({}); tone: sv.roles ? sv.roles.ink : "black"; lineHeight: 1.08 }
            PpChText { visible: !sv.replay && present; roles: sv.roles; sentence: sv.ts.standfirst; font: sv.roles ? sv.roles.fStand : Qt.font({}); lineHeight: sv.roles ? sv.roles.r.leading : 1 }
        }
        PpChKeyFigures { width: parent.width; roles: sv.roles; items: sv.available && sv.rs && sv.rs.keyFigures ? (sv.rs.keyFigures.items || []) : []
                         onTrustClicked: (subject) => sv.openTrust(subject, "figures") }
        PpTrustInline { roles: sv.roles; readings: sv.trustReadings("figures"); swingDir: sv._pickedDir; onClosed: sv.openTrust(sv.trustSubject, "figures") }
        // the controls (Analyse): foldable to one line that still says what is chosen
        PpChFoldHead {
            visible: !sv.replay && sv.available
            width: parent.width; roles: sv.roles; key: "controls"; title: "Controls"; interactive: sv.interactive
            collapsed: !!sv.body && sv.body.collapsed.controls === true
            state_: [sv.tc.presetTitle || "", sv.windowLabel(), sv.tr && sv.tr.active ? sv.againstLabel() : ""].filter(function (x) { return x !== "" }).join("  ·  ")
            onToggled: if (sv.body) sv.body.toggleSection("controls")
        }
        PpChChoices {
            objectName: "chPresetPicker"
            visible: !sv.replay && sv.available && !(sv.body && sv.body.collapsed.controls === true)
            width: parent.width; roles: sv.roles; interactive: sv.interactive
            sets: sv.sets
            onPicked: (set, opt) => sv.picked(set, opt)
        }
        PpInlinePicker {
            objectName: "chPresetList"
            visible: !sv.replay && (sv.openPicker === "preset" || sv.pickerOpen === "preset")
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
            objectName: "chWindowPicker"
            visible: !sv.replay && sv.pickerOpen === "segs"
            open: visible; width: Math.min(parent.width, sv.roles ? sv.roles.u(320) : 320)
            roles: sv.roles; interactive: sv.interactive
            title: "Between two P-positions"
            items: visible ? (sv.tc.windows || []).filter(function (w) { return String(w.key).indexOf("seg:") === 0 }).map(function (w) { return { id: w.key, label: w.label } }) : []
            current: sv.tc.window || ""
            clearLabel: "The default: around the data"
            itemObjectName: "chWindowOption"; clearObjectName: "chWindowDefault"
            onPicked: (id) => { if (sv.body) sv.body.choose("window", id === "" ? "auto" : id); sv.pickerOpen = "" }
        }
        PpInlinePicker {
            objectName: "chAgainstPicker"
            visible: !sv.replay && (sv.openPicker === "against" || sv.pickerOpen === "against")
            open: visible; width: Math.min(parent.width, sv.roles ? sv.roles.u(420) : 420)
            roles: sv.roles; interactive: sv.interactive
            title: "Read this shot against"
            items: visible ? (sv.tc.against || []).filter(function (a) { return a.id !== "none" })
                                .map(function (a) { return { id: a.id, label: a.label, note: a.enabled ? "" : (a.why || "") } }) : []
            current: sv.tc.against ? ((sv.tc.against.filter(function (a) { return a.current })[0] || {}).id || "") : ""
            clearLabel: "nothing"
            itemObjectName: "chAgainstOption"; clearObjectName: "chAgainstNone"
            onPicked: (id) => { if (sv.body) sv.body.choose("against", id === "" ? "none" : id); sv.pickerOpen = "" }
        }
        // the ★ reference (Against "reference ★", any session): this swing becomes it, or stops being it. The
        // words are the wrist presenter's (choices.reference); the setting is the one the wrist section's ★ writes.
        Text {
            objectName: "chRefLink"
            readonly property var ref: sv.body && sv.body.wChoices ? (sv.body.wChoices.reference || ({})) : ({})
            visible: !sv.replay && !sv.cast && sv.available && ref.canSet === true && !!sv.roles && !(sv.body && sv.body.collapsed.controls === true)
            text: ref.linkText || ""
            font: !sv.roles ? Qt.font({}) : ref.isThisShot ? sv.roles.fSmallM : (sv.interactive ? sv.roles.fSmallL : sv.roles.fSmall)
            color: !sv.roles ? "grey" : ref.isThisShot ? sv.roles.ink : (sv.interactive ? sv.roles.link : sv.roles.ink3)
            MouseArea { anchors.fill: parent; enabled: sv.interactive; cursorShape: Qt.PointingHandCursor
                        onClicked: appSettings.wristReferenceSwingDir = parent.ref.isThisShot ? "" : (sv.ts.swingDir || "") }
        }
        PpChSeqChain {
            visible: sv.hasSeq
            width: parent.width; roles: sv.roles
            sequence: sv.available ? sv.ts.sequence : null; lateNote: sv.available ? sv.ts.note : null
            // the glance keeps the late-pelvis note (rule 5: wherever it is said late) and drops the order note
            showOrderNote: !sv.replay
            onTrustClicked: (subject) => sv.openTrust(subject, "strip")
        }
        PpTrustInline { roles: sv.roles; readings: sv.trustReadings("strip"); swingDir: sv._pickedDir; onClosed: sv.openTrust(sv.trustSubject, "strip") }
        // the window header (F15): which span, how long, how many metrics, which P-positions
        Flow {
            objectName: "chWindowHeader"
            visible: !sv.replay && sv.available && !!sv.ts.window && sv.rows.length > 0
            width: parent.width; spacing: sv.roles ? sv.roles.u(10) : 10
            Text { text: sv.ts.window ? (sv.ts.window.header || "") : ""; font: sv.roles ? sv.roles.fSmallM : Qt.font({}); color: sv.roles ? sv.roles.ink2 : "grey" }
            Text { text: sv.ts.window ? (sv.ts.window.spanText || "") : ""; font: sv.roles ? sv.roles.fFigS : Qt.font({}); color: sv.roles ? sv.roles.ink3 : "grey" }
            Text { text: sv.ts.window ? (sv.ts.window.metricsText || "") : ""; font: sv.roles ? sv.roles.fFigS : Qt.font({}); color: sv.roles ? sv.roles.ink3 : "grey" }
            Text { text: sv.ts.window ? (sv.ts.window.tagsText || "") : ""; font: sv.roles ? sv.roles.fFigS : Qt.font({}); color: sv.roles ? sv.roles.ink3 : "grey" }
        }
        // the P dots' key, in words (F23): only the kinds present
        Flow {
            objectName: "chDotKey"
            visible: !sv.replay && sv.available && sv.rows.length > 0 && !!sv.body && sv.body.showDots && (sv.ts.dotKey || []).length > 0
            width: parent.width; spacing: sv.roles ? sv.roles.u(14) : 14
            Text { text: "P dots"; font: sv.roles ? sv.roles.fLabel : Qt.font({}); color: sv.roles ? sv.roles.ink3 : "grey" }
            Repeater {
                model: sv.roles && sv.available ? (sv.ts.dotKey || []) : []
                delegate: Row {
                    required property var modelData
                    spacing: sv.roles.u(5)
                    Item {
                        width: sv.roles.u(10); height: sv.roles.u(10); anchors.verticalCenter: parent.verticalCenter
                        readonly property string vd: modelData.verdict || ""
                        readonly property real d: 2 * Math.round(sv.roles.u(vd === "" ? 4 : 6) / 2)
                        Rectangle { anchors.centerIn: parent; width: parent.d; height: parent.d; radius: sv.roles.square ? 0 : parent.d / 2
                                    color: parent.vd === "good" ? sv.roles.ink : parent.vd === "" ? sv.roles.ink3 : "transparent"
                                    border.width: parent.vd === "attention" || parent.vd === "warn" ? 1.25 : 0
                                    border.color: parent.vd === "warn" ? sv.roles.warn : sv.roles.ink }
                        Rectangle { visible: parent.vd === "warn"; anchors.centerIn: parent; width: 3; height: 3; radius: 1.5; color: sv.roles.warn }
                    }
                    Text { text: modelData.word; font: sv.roles.fLabel; color: sv.roles.ink3; anchors.verticalCenter: parent.verticalCenter }
                }
            }
        }
    }

    // ═══ the page ═══
    Item {
        width: sv.width
        height: sv.rail ? Math.max(railCol.height, mainCol.height) : mainCol.height
        Column {
            id: railCol; visible: sv.rail; width: sv.railW; spacing: sv.roles ? sv.roles.u(14) : 14
            Column { id: railHead; width: parent.width }
        }
        Rectangle { visible: sv.rail; x: sv.railW + sv.gut - 1; width: sv.roles ? sv.roles.hw : 1; height: parent.height; color: sv.roles ? sv.roles.hair : "grey" }
        Column {
            id: mainCol
            x: sv.rail ? sv.railW + 2 * sv.gut : 0
            width: sv.mainW
            spacing: sv.roles ? sv.roles.u(8) : 8
            Column { id: headSlot; width: parent.width; visible: !sv.rail }
            PpChWindowBrush {
                visible: !sv.replay && sv.available && sv.spanUs > 0 && sv.rows.length > 0
                width: parent.width; roles: sv.roles; interactive: sv.interactive
                spanUs: sv.spanUs
                winStartUs: sv.ts.window ? sv.ts.window.startUs : 0; winEndUs: sv.ts.window ? sv.ts.window.endUs : 0
                ticks: sv.available ? (sv.ts.recordingTicks || []) : []
                overview: sv.available ? (sv.ts.overview || []) : []
                playUs: sv.playUs
                onCustomRequested: (a, b) => { if (sv.body) sv.body.chooseCustom(a, b) }
            }
            // Against: the largest gap said, or why the reference is refused
            PpChText { objectName: "chAgainstHead"; visible: !sv.replay && present; roles: sv.roles; sentence: sv.tr && sv.tr.active ? sv.tr.head : null; tone: sv.roles ? sv.roles.ink : "black"; font: sv.roles ? sv.roles.fBody : Qt.font({}) }
            PpChText { visible: !sv.replay && present; roles: sv.roles; sentence: sv.tr && sv.tr.refused ? sv.tr.why : null; tone: sv.roles ? sv.roles.ink3 : "grey" }
            // a preset with no drawable curve (Club delivery, S6): its cards carry it
            PpChText { objectName: "chNoTraces"; visible: present; roles: sv.roles; sentence: sv.available ? sv.ts.noTraces : null; font: sv.roles ? sv.roles.fBody : Qt.font({}) }
            PpChTraceFigure {
                visible: sv.available && sv.rows.length > 0
                width: parent.width
                roles: sv.roles; brush: sv.brush; interactive: sv.interactive
                presenter: sv.body ? sv.body.tracesP : null
                rows: sv.rows; ticks: sv.available ? (sv.ts.ticks || []) : []; msTicks: sv.available ? (sv.ts.msTicks || []) : []
                x0: sv.ts.window ? sv.ts.window.t0Ms : -300; x1: sv.ts.window ? sv.ts.window.t1Ms : 100
                overlay: !!sv.body && sv.body.overlay; overlayAxis: sv.available ? sv.ts.overlay : null
                showDots: !sv.body || sv.body.showDots; showCursor: !sv.body || sv.body.showCursor
                reference: sv.tr; nowRows: sv.tn && sv.tn.valid ? (sv.tn.rows || []) : []
                playMs: sv.tn && sv.tn.valid ? sv.tn.shotMs : NaN
                playText: sv.replay && sv.tn && sv.tn.valid ? (sv.tn.when || "") : ""
                figMode: sv.replay ? "now" : "figures"
                plotH: sv.plotH
                demoHoverMs: sv.demoHoverMs
                onTrustClicked: (subject) => sv.openTrust(subject, "chart")
            }
            PpTrustInline { roles: sv.roles; readings: sv.trustReadings("chart"); swingDir: sv._pickedDir; onClosed: sv.openTrust(sv.trustSubject, "chart") }
            // Replay: the curve-less members of the preset, read at their instants, as one quiet ledger
            PpChSummary {
                visible: sv.replay && sv.available && (sv.ts.cards || []).length > 0
                width: parent.width; roles: sv.roles; interactive: sv.interactive
                rows: []; cards: sv.available ? (sv.ts.cards || []) : []
            }
        }
    }

    // ═══ the wrist section, when it means something (a wrist reading AND a wrist curve on show) ═══
    Loader {
        objectName: "chWristShotSlot"
        active: !!sv.body && sv.body.wristShotShown && !!sv.roles
        visible: active
        width: sv.width
        sourceComponent: PpChWristShotSection { roles: sv.roles; brush: sv.brush; body: sv.body; reg: sv.reg
                                                interactive: sv.interactive; cast: sv.cast }
    }

    // ═══ Analyse: the readings below the figure ═══
    PpChFoldHead {
        visible: !sv.replay && sv.available && (sv.rows.length + (sv.ts.cards || []).length) > 0
        width: sv.width; roles: sv.roles; key: "summary"; title: "Summary"; sub: sv.ts.window ? (sv.ts.window.header || "") : ""
        interactive: sv.interactive
        collapsed: !!sv.body && sv.body.collapsed.summary === true
        state_: sv.ts.window ? (sv.ts.window.metricsText || "") : ""
        onToggled: if (sv.body) sv.body.toggleSection("summary")
    }
    PpChSummary {
        visible: !sv.replay && sv.available && !(sv.body && sv.body.collapsed.summary === true)
        width: sv.width; roles: sv.roles; interactive: sv.interactive
        rows: sv.rows; cards: sv.available ? (sv.ts.cards || []) : []
    }
    PpChFoldHead {
        visible: !sv.replay && sv.hasSeq && (sv.ts.sequence.table || []).length > 0
        width: sv.width; roles: sv.roles; key: "sequence"; title: "The kinematic sequence"; sub: sv.hasSeq ? (sv.ts.sequence.verdictText || "") : ""
        interactive: sv.interactive
        collapsed: !!sv.body && sv.body.collapsed.sequence === true
        state_: sv.hasSeq ? (sv.ts.sequence.verdictText || "") : ""
        onToggled: if (sv.body) sv.body.toggleSection("sequence")
    }
    PpChSeqTable {
        visible: !sv.replay && sv.hasSeq && !(sv.body && sv.body.collapsed.sequence === true)
        width: sv.width; roles: sv.roles; sequence: sv.hasSeq ? sv.ts.sequence : null
    }
    // Apart from the session (B): the readings past the session's whiskers lead
    Column {
        objectName: "chApart"
        visible: !sv.replay && !!sv.rs && sv.rs.available === true
        width: sv.width; spacing: sv.roles ? sv.roles.u(8) : 8
        PpSectionHead { width: parent.width; roles: sv.roles; title: sv.rs && sv.rs.head ? (sv.rs.head.text || "") : "" }
        PpChText { roles: sv.roles; sentence: sv.rs ? sv.rs.standfirst : null; font: sv.roles ? sv.roles.fBody : Qt.font({}) }
        PpPageKey {
            id: chKey1
            width: parent.width; roles: sv.roles; kinds: ["box", "ring", "clean", "fired"]
            // the Charts words for the marks (PpPageKey.extraWords, live from the final build; until then the
            // module's PpPageKey has no such property and keeps its own words)
            Component.onCompleted: if (chKey1.extraWords !== undefined) chKey1.extraWords = { clean: "a shot past the whiskers", fired: "a condition reading it fired" }
        }
        PpChProfile {
            objectName: "chApartList"
            visible: (sv.rs.apart || []).length > 0
            width: parent.width; roles: sv.roles; brush: sv.brush; interactive: sv.interactive
            presenter: sv.body ? sv.body.relationsP : null
            rows: sv.rs.apart || []
            headLabel: "Apart from the session"; shotLabel: sv.shotWord; countLabel: sv.countWord
            openId: sv.body ? sv.body.openId : ""; openRow: sv.body ? sv.body.rOpen : null
            playMs: sv.relPlayMs; playText: sv.body && sv.body.rPlayhead ? (sv.body.rPlayhead.openValueText || "") : ""
            onRowActivated: (id) => { if (sv.body) sv.body.openReading(id) }
            onShotActivated: (o) => { if (sv.body) sv.body.pickOrdinal(o) }
        }
        PpNotes { width: parent.width; roles: sv.roles; text: sv.rs ? sv.roles.esc(sv.rs.notes || "") : "" }
        // the fired conditions that read no reading on the profile, said once
        Flow {
            visible: (sv.rs.firedElsewhere || []).length > 0
            width: parent.width; spacing: sv.roles ? sv.roles.u(10) : 10
            Text { text: "Also fired on this shot"; font: sv.roles ? sv.roles.fLabel : Qt.font({}); color: sv.roles ? sv.roles.ink3 : "grey" }
            Repeater {
                model: sv.roles ? (sv.rs.firedElsewhere || []) : []
                delegate: Row {
                    required property var modelData
                    spacing: sv.roles.u(4)
                    Text { text: modelData.name; font: sv.roles.fSmall; color: sv.roles.warn }
                    Text { text: modelData.instant || ""; font: sv.roles.fSmall; color: sv.roles.ink3 }
                }
            }
        }
        PpChQuestions {
            width: parent.width; roles: sv.roles; interactive: sv.interactive
            questions: sv.rs.asks || []
            active: sv.body ? sv.body.askedId : ""
            onAsked: (q) => { if (sv.body) sv.body.ask(q, sv.brush) }
        }
    }
    // Every reading against the session (D5): by family, two columns while each keeps its minimum
    Column {
        objectName: "chProfileAll"
        visible: !sv.replay && !!sv.rs && sv.rs.available === true && (sv.rs.groups || []).length > 0
        width: sv.width; spacing: sv.roles ? sv.roles.u(8) : 8
        PpSectionHead { width: parent.width; roles: sv.roles; title: "Every reading against the session"; sub: "by family; a row opens its curve, or its run" }
        Flow {
            id: pf
            width: parent.width; spacing: sv.roles ? sv.roles.u(28) : 28
            readonly property bool two: !!sv.roles && sv.roles.sizeClass(width) === "wide"
            readonly property real colW: two ? Math.floor((width - (sv.roles ? sv.roles.u(28) : 28)) / 2) : width
            // two balanced columns, never splitting a family
            readonly property var cols: {
                const gs = sv.rs.groups || []
                if (!two) return [gs]
                let total = 0; for (const g of gs) total += (g.rows || []).length + 1
                let acc = 0, cut = 0, bd = 1e9
                for (let i = 0; i < gs.length; ++i) { acc += (gs[i].rows || []).length + 1; const d = Math.abs(acc - total / 2); if (d < bd) { bd = d; cut = i + 1 } }
                return [gs.slice(0, cut), gs.slice(cut)]
            }
            Repeater {
                model: pf.cols
                delegate: PpChProfile {
                    required property var modelData
                    width: pf.colW; roles: sv.roles; brush: sv.brush; interactive: sv.interactive
                    presenter: sv.body ? sv.body.relationsP : null
                    groups: modelData
                    shotLabel: sv.shotWord; countLabel: sv.countWord
                    openId: sv.body ? sv.body.openId : ""; openRow: sv.body ? sv.body.rOpen : null
                    playMs: sv.relPlayMs; playText: sv.body && sv.body.rPlayhead ? (sv.body.rPlayhead.openValueText || "") : ""
                    onRowActivated: (id) => { if (sv.body) sv.body.openReading(id) }
                    onShotActivated: (o) => { if (sv.body) sv.body.pickOrdinal(o) }
                }
            }
        }
    }

    // ── the playhead and the recording, from shotReplay (pixels only) ──
    readonly property real spanUs: body ? body.spanUs : 0
    readonly property real playUs: body ? body.playUs : -1
    readonly property real relPlayMs: body && body.rStatus && body.rStatus.pickedImpactUs >= 0 && playUs >= 0 ? (playUs - body.rStatus.pickedImpactUs) / 1000 : NaN
    // the profile's column heads, the presenter's ("Shot 8", "15 shots, low to high" / "15 shots")
    readonly property string shotWord: body && body.rStatus ? (body.rStatus.shotLabel || "") : ""
    readonly property string countWord: !body || !body.rStatus ? "" : roles && !roles.atLeast(width, "compact") ? (body.rStatus.countLabelShort || "") : (body.rStatus.countLabel || "")
}
