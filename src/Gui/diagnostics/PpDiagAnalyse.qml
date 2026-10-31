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
import QtQuick.Controls
import PinPointStudio

// ANALYSE — the read. The ledger leads (A): every view opens on the table that answers its question in
// exact figures; B's sentences carry the "why" (standfirsts, the direction line under each pattern, the
// feature head, the causal headlines); C's figures put the answer among everything (the dot plot of all
// conditions, the barcode, the detail's figure row). One PpBrush: hover anything and it is shown once in
// every block; a click goes somewhere real through `body`. Each view ends its lead table with at most
// three SHARPER QUESTIONS, computed from the data, answered in place (co-occurrence, never cause).
// Round 4 (the old panel's states and features, INVENTORY §1-2):
//   • tense: an empty This shot says why in the old words (F19); live with nothing picked it reads the
//     newest swing (D3); a quiet shot is said in one calm line and the read is never gated (D2, F21);
//     zero fired is one line, not an empty table (S38).
//   • Session: Cold says the n is too small + expectations (F41-42); the filter is body.cardFilter (D8)
//     and an emptying filter explains itself (F35); focus on the rows (F33); evidence line (F30).
//   • Watching: empty, the old sentence (F45).
//   • Detail: measure tabs (F49), "Run the screen" (F64 → body.screenRequested), Esc closes (F47). The
//     honesty mark (F56), the norm's source (F51) and the hover readout (F53) are the lib's (PpFeatureHead,
//     PpFigureRow) from round 4.
//   • the declared-miss chip and its inline picker (F66-67); the after-shot pulse (D1).
//   • each view is its own named Flickable, built on first show and kept (D8); demo code runs only when
//     demoState is non-empty, so a republish never scrolls or clears a hover.
// demoState: "" | hoverShot12 | hoverCondition | question-clean | question-together | question-worse | missPicker
Item {
    id: root
    objectName: "sdRegister"
    readonly property string register: "Analyse"
    // ── contract (round 2 + round 3 + round 4) ──
    property string view: "shot"
    property var readout: null
    property var cards: []
    property var source: null
    property var header: null
    property var detail: null
    property string medium: "screen"
    property string printOrientation: ""
    property int printPage: 0
    readonly property int printPageCount: 1          // Analyse is a screen register; PpDiagPrint makes the report
    property var body: null
    property string demoState: ""
    property string surface: "panel"
    property bool interactive: true                  // body.interactive
    property string cardFilter: "all"                // body.cardFilter (navigation-saved)
    property var pulseCue: null                      // body.pulseCue
    readonly property var demoStates: ["hoverShot12", "hoverCondition", "question-clean", "question-together", "question-worse", "missPicker"]

    // ── data ──
    readonly property var shot: ss.shot
    readonly property var conds: (shot && shot.conditions) || []
    readonly property var fired: conds.filter(c => c.stateKind === "fired")
    readonly property var notMeasured: conds.filter(c => c.stateKind === "notAssessable")
    // NOT JUDGED, AND WHY (trust_and_gates_design.md §4.4): the findings a gate kept from being judged,
    // grouped by the reading the gate acted on — "Late pelvis rotation, Sequence order: pelvis peak out of
    // sight". The rest of the not-measurable list stays as it was.
    readonly property var notMeasuredPlain: notMeasured.filter(c => !c.trustSubject)
    readonly property var notJudgedGroups: {
        const out = [], at = {}
        for (const c of notMeasured) {
            if (!c.trustSubject) continue
            if (at[c.trustSubject] === undefined) {
                at[c.trustSubject] = out.length
                out.push({ subject: c.trustSubject, swingDir: c.trustSwingDir || "", clause: c.reason || "", names: [] })
            }
            out[at[c.trustSubject]].names.push(c.name)
        }
        return out
    }
    readonly property var watching: ((source && source.watching) || []).slice()
                                    .sort((a, b) => (ro.rate(b.recurrence) - ro.rate(a.recurrence)) || a.name.localeCompare(b.name))
    readonly property var detailCard: detail ? (ro.byId(cards || [], detail.id) || detail.header || null) : null
    readonly property var shownCards: (cards || []).filter(c => cardFilter === "all" || (cardFilter === "ball" && c.reachesBall)
                                                          || (cardFilter === "roots" && c.rootHere))
    readonly property bool filterEmpty: !ss.cold && (cards || []).length > 0 && shownCards.length === 0
    readonly property string shotWord: shot ? "shot " + ro.shotNo(shot) : ""
    readonly property string shotNo: shot ? String(ro.shotNo(shot)) : ""
    readonly property string focusShotLine: {
        if (!ss.focusId || !shot) return ""
        const c = ro.byId(conds, ss.focusId), card = ro.byId(cards || [], ss.focusId)
        const name = card ? card.name : (c ? c.name : "")
        return name ? ss.focusLine(name, c ? c.stateKind : (ss.partial ? "clean" : "notAssessable")) : ""
    }

    // ── the distance to travel (PpKeyFigures on Detail): what PpFeatureHead does not already state ──
    readonly property var travelItems: {
        const h = (detail && detail.header) || null, sp = (h && h.spread) || null
        if (!sp) return []
        const cur = (sp.run || []).find(r => r.current && r.assessable)
        const pass = (sp.bands || []).filter(b => b.grade === "ideal" || b.grade === "good" || b.grade === "open")
        if (!cur || !pass.length) return []
        const v = cur.value
        // an OPEN end has no pass edge (#16: a one-way condition opens the end it never fires on): its band
        // stops where the axis does, which is no distance the golfer can travel
        const bands = sp.bands || [], graded = pass.filter(b => b.grade !== "open")
        if (!graded.length) return []
        const lo = bands[0].grade === "open" ? -Infinity : Math.min(...graded.map(b => b.lo))
        const hi = bands[bands.length - 1].grade === "open" ? Infinity : Math.max(...graded.map(b => b.hi))
        const above = v > hi, below = v < lo
        const fmt = x => (Math.round(Math.abs(x) * 10) / 10).toFixed(1).replace("-", "−")
        const lab = "Shot " + (cur.index !== undefined ? cur.index + 1 : shotNo)
        const out = []
        if (above || below) {
            const edge = above ? hi : lo
            const fl = (sp.faultLines || []).filter(f => above ? f.value >= hi : f.value <= lo)
                        .sort((a, b) => Math.abs(a.value - edge) - Math.abs(b.value - edge))[0]
            if (fl) {
                const past = above ? v - fl.value : fl.value - v
                out.push({ label: lab + " past the fault line", figure: past > 0 ? fmt(past) : "inside it" , word: past <= 0 })
            }
            out.push({ label: lab + " from the pass edge", figure: fmt(above ? v - hi : lo - v) })
        } else {
            out.push({ label: lab + " inside the pass edge by", figure: fmt(Math.min(v - lo, hi - v)) })
        }
        const rc = detail ? ro.byId(conds, detail.id) : null
        if (rc && rc.strengthKnown && rc.stateKind === "fired" && !rc.composite) out.push({ label: lab + " out by", figure: rc.strengthSharp ? "sharp" : rc.strengthExcess.toFixed(1) + "×" })
        return out
    }
    // F64: the screened roots on the open condition's paths, and the condition itself when it is one
    readonly property var driver: source ? source.driver : null
    readonly property var screenedRoots: {
        if (!detail) return []
        const out = [], seen = {}
        const add = (id, name) => { if (id && !seen[id]) { seen[id] = true; out.push({ id: id, name: name }) } }
        if (detail.header && detail.header.kind === "screenedRoot") add(detail.id, detail.name || detail.header.name)
        for (const side of ["causes", "effects"])
            for (const p of (detail[side] || [])) for (const nd of (p.nodes || [])) if (nd.kind === "screenedRoot") add(nd.id, nd.name)
        return out
    }
    function requestScreen(id) {
        if (!root.body || !root.interactive || !id) return
        const ref = root.driver && root.driver.screenConditionId === id ? (root.driver.screenRef || "") : ""
        root.body.screenRequested(ref, id)
    }

    // ── wiring: every click goes somewhere real ──
    function openCondition(id) { if (body && id && interactive) body._openDetail(id) }
    // A tab is the body's own view state (the panel and the cast read it), so a press goes to the body.
    function pickView(key) { if (!interactive) return; if (root.body) root.body.tab = key; else root.view = key }
    function pickShot(dir, id) { if (body && interactive && id > 0) body.shotRequested(dir || br.dirOf(id), id) }
    function setFilter(k) { if (!interactive) return; if (root.body) root.body.cardFilter = k; else root.cardFilter = k }
    function stepThrough(ids) {           // "step through them": the next answering shot after the picked one
        if (!ids || !ids.length) return
        let nxt = ids.find(s => s > br.pickedShot)
        if (nxt === undefined) nxt = ids[0]
        pickShot("", nxt)
    }
    property bool missOpen: false
    property int _pulseToken: -1
    onPulseCueChanged: firePulse()
    function firePulse() {
        const c = root.pulseCue
        if (!c) { br.pulseHold = false; br.pulseIds = []; _pulseToken = -1; return }   // a cue withdrawn: nothing held
        if (c.token === undefined || c.token === _pulseToken) return
        _pulseToken = c.token
        br.pulseHold = !!c.hold                   // renders only: hold the wash for a still
        // ids may arrive as afterShotDelta chips ({ id, … }) rather than ids: take the id either way
        br.pulse((c.ids || []).map(x => x && typeof x === "object" ? x.id : x).filter(x => !!x), c.focusId || "")
    }

    // ═══════════════════════════ SHARPER QUESTIONS (computed; shown only when answerable) ═══════════════════════════
    // the lib's question engine, phrased with the subject's name ("<name>: …"), restricted to what is on screen
    function q(card, id, only) {
        if (!card || !shot || ss.partial) return null
        return qh.forCondition(card, shot, source, { subject: true, only: only || null }).find(x => x.id === id) || null
    }
    function cleanQ(card) { return q(card, "clean") }
    function togetherQ(card, only) { return q(card, "together", only) }
    readonly property var shotQuestions: {
        if (!shot || ss.partial) return []
        const fc = (cards || []).filter(c => c.thisShot === "fired")
        const out = []
        const cq = cleanQ(fc.find(c => qh.cleanShots(c).length > 0))
        if (cq) out.push(cq)
        const tq = togetherQ(fc[0] || null, null)
        if (tq) out.push(tq)
        const w = fc.find(c => c.trendKnown && c.trend === "worsening")
        if (w) out.push({ id: "worse", text: w.name + ": is it getting worse?", shots: qh.thirds(w),
                          answer: "The first and last thirds of the run are lit: " + ro.lower1(w.trendText || "") + "." })
        return out.slice(0, 3)
    }
    readonly property var sessionQuestions: {
        const cs = shownCards
        if (!cs.length || !shot || ss.partial) return []
        const out = []
        const byClean = cs.slice().sort((a, b) => qh.cleanShots(b).length - qh.cleanShots(a).length)
        const cq = cleanQ(byClean[0])
        if (cq) out.push(cq)
        const tq = togetherQ(byClean[0], cs.map(c => c.id))
        if (tq) out.push(tq)
        const ws = cs.filter(c => c.trendKnown && c.trend === "worsening")
        if (ws.length) out.push({ id: "worse", text: "Which patterns are getting worse?", conditions: ws.map(c => c.id),
                                  answer: qh.list(ws.map(c => c.name)) + ": the misses are growing over the session." })
        return out.slice(0, 3)
    }
    readonly property var watchQuestions: {
        const ws = watching
        if (!ws.length || !shot || ss.partial) return []
        const out = []
        const here = ws.filter(w => { const c = ro.byId(conds, w.id); return c && c.stateKind === "fired" })
        if (here.length) out.push({ id: "thisshot", text: "Which of these fired on " + shotWord + "?", conditions: here.map(w => w.id),
                                    answer: qh.list(here.map(w => w.name)) + "." })
        const top = ro.byId(conds, ws[0].id)
        const tq = top ? togetherQ(top, ws.map(w => w.id)) : null
        if (tq) out.push(tq)
        return out.slice(0, 3)
    }
    readonly property var detailQuestions: detailCard && shot && !ss.partial ? qh.forCondition(detailCard, shot, source, { subject: false }) : []
    readonly property var currentQuestions: view === "shot" ? shotQuestions : view === "session" ? sessionQuestions
                                          : view === "watching" ? watchQuestions : view === "detail" ? detailQuestions : []

    // ── demo states: only ever when demoState is non-empty (D8: a republish never scrolls or clears a hover) ──
    property string _lastDemo: ""
    onDemoStateChanged: Qt.callLater(applyDemo)
    onViewChanged: if (demoState !== "") Qt.callLater(applyDemo)
    function applyDemo() {
        if (demoState === "" && _lastDemo === "") return
        _lastDemo = demoState
        revealTimer.stop()
        br.clear()
        missOpen = demoState === "missPicker"
        if (demoState === "hoverShot12") br.hoveredShot = 12
        else if (demoState === "hoverCondition")
            br.hoveredCondition = view === "detail" && detail ? (detail.id || "") : (ro.byId(conds, "early_extension") ? "early_extension" : (fired[0] ? fired[0].id : ""))
        else if (demoState.indexOf("question-") === 0) {
            const q = currentQuestions.find(x => x.id === demoState.slice(9))
            if (q) { qh.ask(q); revealTimer.restart() }   // a question the data cannot answer is not offered: nothing lights
        }
    }
    // demo only: scroll so the asked question sits at the foot of the view, as it would after the reader clicked it
    Timer {
        id: revealTimer; interval: 400
        onTriggered: {
            const f = [shotFlick, sessionFlick, watchingFlick, detailFlick].find(x => x.on)
            const a = f && f.content && f.content.asks
            if (!a) return
            const y = a.mapToItem(f.contentItem, 0, 0).y + a.height + ro.u(16)
            f.contentY = Math.max(0, Math.min(f.contentHeight - f.height, y - f.height))
        }
    }

    // ── one named Flickable per view, built on first show and never torn down (D8, §0.2-0.3) ──
    component ViewFlick: Flickable {
        id: vf
        property string key: ""
        property Component view: null
        readonly property bool on: root.view === key
        readonly property Item content: ld.item
        property bool built: false
        onOnChanged: if (on) built = true
        Component.onCompleted: if (on) built = true
        visible: on
        anchors { top: under.bottom; left: screen.left; right: screen.right; bottom: screen.bottom; topMargin: ro.u(2) }
        contentWidth: width; contentHeight: (ld.item ? ld.item.height : 0) + ro.u(24)
        clip: true; boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Loader { id: ld; active: vf.built; width: vf.width - ro.u(8); sourceComponent: vf.view }
    }
    // a small crosshead between the lead table and the figure that puts it among everything
    component Crosshead: Text {
        width: parent ? parent.width : 0
        topPadding: ro.u(22); bottomPadding: ro.u(6)
        font: ro.fSection; color: ro.ink
    }
    // the view's questions, after its lead table
    component Asks: PpQuestions {
        objectName: "sdQuestions"
        roles: ro; brush: br; interactive: root.interactive
        topPadding: ro.u(12)
        onStepRequested: ids => root.stepThrough(ids)
    }
    // the review note, folded to the foot of a view below medium (F7), or when the miss chip takes the right
    component ReviewFoot: PpNotes {
        objectName: "sdReviewNote"
        roles: ro
        visible: !!root.header && String(root.header.reviewNote || "") !== "" && (!ro.shows("reviewNote", width) || ss.missShown)
        text: root.header ? ro.cap1(root.header.reviewNote) + "." : ""
    }

    // ═══════════════════════════ SCREEN ═══════════════════════════
    Item {
        id: screen
        anchors.fill: parent
        visible: root.medium !== "print"
        PpTabs {
            id: tabs
            objectName: "sdTabBar"
            visible: root.view !== "detail"
            width: parent.width; height: visible ? implicitHeight : 0
            roles: ro; brush: br
            current: root.view
            note: root.header && !ss.missShown ? ro.cap1(root.header.reviewNote) : ""
            interactive: root.interactive
            // F66: the declared-miss chip at the right of the tab row; it stands down on the detail
            chip: ss.missShown ? ss.missText : ""
            chipOn: root.missOpen
            onChipActivated: root.missOpen = !root.missOpen
            // F2: the shot tab's note is its fired count, "quiet" on a quiet shot, nothing with nothing to read
            tabs: [{ key: "shot", label: "This shot", count: !ss.shot ? undefined : ss.quiet ? "quiet" : root.fired.length },
                   { key: "session", label: "Session", count: root.shownCards.length },
                   { key: "watching", label: "Watching", count: root.watching.length }]
            onTabActivated: k => root.pickView(k)
        }
        PpDetailBar {
            id: dbar
            visible: root.view === "detail"
            anchors.top: parent.top
            width: parent.width; height: visible ? implicitHeight : 0
            roles: ro; brush: br; interactive: root.interactive
            crumb: root.detail ? root.detail.name || "" : ""
            focused: !!(root.detailCard && root.detailCard.focused)
            onBackActivated: if (root.body && root.interactive) root.body._closeDetail()
            onFocusActivated: if (root.body && root.detail && root.interactive) root.body._declareFocus(root.detail.id, !focused)
        }
        Column {
            id: under
            anchors.top: root.view === "detail" ? dbar.bottom : tabs.bottom
            width: parent.width
            // F67: the picker opens IN PLACE under the tab row; a pick goes to the model, "" clears
            PpInlinePicker {
                objectName: "sdMissPicker"
                visible: root.missOpen && ss.missShown && root.view !== "detail"
                open: visible
                width: under.width
                roles: ro; brush: br; interactive: root.interactive
                items: visible ? ss.missCandidates().map(m => ({ id: m.id, label: m.name || m.id })) : []
                current: ss.declaredMiss
                onPicked: id => { if (root.body && typeof root.body._declareMiss === "function") root.body._declareMiss(id); root.missOpen = false }
            }
        }
        ViewFlick { id: shotFlick; objectName: "sdShotCardsFlick"; key: "shot"; view: shotC }
        ViewFlick { id: sessionFlick; objectName: "sdCardsFlick"; key: "session"; view: sessionC }
        ViewFlick { id: watchingFlick; objectName: "sdWatchingFlick"; key: "watching"; view: watchingC }
        ViewFlick { id: detailFlick; objectName: "sdDetailFlick"; key: "detail"; view: detailC }
    }

    // ── THIS SHOT: what went wrong on this swing ──
    Component {
        id: shotC
        Column {
            id: sc
            objectName: "sdShotView"
            readonly property Item asks: asks1
            // nothing to read: the tense says why, in the old panel's words (F19)
            Column {
                objectName: "sdShotEmpty"
                visible: !ss.shot && ss.tense !== "none"
                width: sc.width
                topPadding: ro.u(10)
                spacing: ro.u(2)
                Text { visible: ss.emptyTitle !== ""; text: ss.emptyTitle; font: ro.fTitle; color: ro.ink }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: ro.cap1(ss.emptyLine) + (ss.emptyTitle !== "" ? "." : "")
                       font: ss.emptyTitle !== "" ? ro.fSmall : ro.fBody; color: ss.emptyTitle !== "" ? ro.ink3 : ro.ink2 }
            }
            // D2: a quiet shot says so; Analyse is never gated (a read is not feedback), so the read follows
            Column {
                objectName: "sdQuietState"
                visible: ss.quiet
                width: sc.width
                topPadding: ro.u(10); bottomPadding: ro.u(4)
                spacing: ro.u(2)
                Text { objectName: "sdQuietLabel"; width: parent.width; wrapMode: Text.WordWrap
                       text: "Quiet shot — nothing new surfaced on this swing"; font: ro.fBodyM; color: ro.ink }
                Text { objectName: "sdQuietLine"; width: parent.width; wrapMode: Text.WordWrap
                       text: (ss.delta ? ss.delta.headline : "") + " · the ledger kept counting; cadence gates surfacing, never the engine"
                       font: ro.fSmall; color: ro.ink3 }
            }
            PpStandfirst {
                objectName: "sdShotHead"
                visible: !!ss.shot
                width: sc.width; roles: ro; brush: br; interactive: root.interactive
                kicker: !ss.shot ? "" : [ (ss.partial ? "Newest swing" : ""), ro.cap1(ss.label), ss.club, root.focusShotLine ]
                                         .filter(s => s !== "").map((s, i) => i === 0 ? ro.cap1(s) : s).join(" · ")
                head: root.fired.length ? ro.cap1(ro.word(root.fired.length)) + " outside the corridor" : "Inside every corridor"
                // zero counts are worded by the helper ("All 78 … stayed inside", "none of them is a session pattern yet")
                standfirst: !ss.shot ? "" : ss.partial ? ro.esc(ro.sentence(ss.shot.headline)) + (ss.shot.note ? " " + ro.esc(ro.sentence(ss.shot.note)) : "")
                          : ro.shotStandfirst(root.shot, ro.shows("longStandfirst", sc.width))
            }
            // "How far can I trust this?" (§7.4): closed by default, nothing more until opened
            PpTrustSummaryFold {
                visible: !!ss.shot && !ss.partial && !!root.readout && String(root.readout.swingDir || "") !== "" && lines.length > 0
                width: sc.width; roles: ro; scope: "shot"
                topPadding: ro.u(6)
                swingDir: root.readout ? String(root.readout.swingDir || "") : ""
                sessionDir: root.source ? String(root.source.sessionDir || "") : ""
            }
            Item { visible: !!ss.shot; width: 1; height: ro.u(10) }
            // S38 / F20: zero fired is one line, never an empty table
            PpNotes {
                objectName: "sdFiredEmpty"
                visible: !!ss.shot && root.fired.length === 0
                width: sc.width; roles: ro; small: true; tone: 2
                text: (root.cards || []).length > 0 ? "None of this session's patterns fired on this swing."
                                                     : "No patterns yet — the plot below is everything this swing said."
            }
            PpLedgerTable {
                visible: root.fired.length > 0
                width: sc.width; roles: ro; brush: br; kind: "fired"; rows: root.fired; readout: root.shot
                interactive: root.interactive
                // the newest swing (D3) publishes no value or run for a watched condition: its columns go, out by stays
                spec: ss.partial ? ro.tiers.fired.filter(c => ["value", "unit", "strip"].indexOf(c.key) < 0) : ro.tiers.fired
                title: "Outside the corridor"
                onConditionActivated: id => root.openCondition(id)
                onShotActivated: (d, s) => root.pickShot(d, s)
            }
            PpNotes {
                visible: root.fired.length > 0
                width: sc.width; roles: ro; keepPrev: true
                text: ss.partial ? "The newest swing, as the session read it. Out by: distance past the corridor edge, as a multiple of the distance to it.  Pick the shot in the carousel to read every condition."
                                 : "Out by: distance past the corridor edge, as a multiple of the distance to it.  Fired: shots outside the corridor, of those measurable.  A row opens its detail; a strip cell puts that shot in the carousel."
            }
            Asks { id: asks1; width: sc.width; questions: root.shotQuestions }
            Crosshead { visible: !!ss.shot; text: ss.partial ? "Outside, on one scale" : "Every condition on one scale" }
            PpPageKey { visible: !!ss.shot; width: sc.width; roles: ro; readout: root.shot; set: "shot" }
            PpDotPlot {
                visible: !!ss.shot
                width: sc.width; roles: ro; brush: br; readout: root.shot
                filter: ss.partial ? "fired" : "all"
                interactive: root.interactive
                onConditionActivated: id => root.openCondition(id)
            }
            PpNotes {
                objectName: "sdNotMeasuredNote"
                visible: !!ss.shot
                width: sc.width; roles: ro
                text: ss.partial ? (ss.shot.cleanCount + " clean · " + ss.shot.notAssessableCount + " not measurable on this swing.")
                      : root.notMeasuredPlain.length ? "Not measurable on this shot (" + root.notMeasuredPlain.length + "): "
                                                  + root.notMeasuredPlain.map(c => ro.esc(c.name) + " — " + ro.esc(c.reason)).join("; ") + "."
                      : root.notJudgedGroups.length ? ""
                      : "Every condition was measurable on this shot."
            }
            // the findings a gate kept from being judged, each group opening the gate's reason — in the
            // Trust panel when it is on, else in place under the line (TrustFocus, §7.0)
            Column {
                id: nj
                objectName: "sdNotJudged"
                visible: !!ss.shot && !ss.partial && root.notJudgedGroups.length > 0
                width: sc.width
                spacing: ro.u(5)
                topPadding: ro.u(5)
                property string openSubject: ""
                Text {
                    text: "Not judged on this shot, because a gate refused the reading they need ("
                          + root.notJudgedGroups.reduce((n, g) => n + g.names.length, 0) + ")"
                    font: ro.fMicro; color: ro.ink3
                }
                Repeater {
                    model: root.notJudgedGroups
                    delegate: Column {
                        id: grp
                        required property var modelData
                        objectName: "sdNotJudged:" + modelData.subject
                        width: nj.width
                        spacing: ro.u(4)
                        Text {
                            width: Math.min(parent.width, ro.flows.measure * ro.em * 1.3)
                            wrapMode: Text.WordWrap; textFormat: Text.StyledText
                            text: ro.esc(grp.modelData.names.join(", ")) + ": " + ro.esc(grp.modelData.clause)
                                  + "  <font color='" + ro.link + "'>Why ›</font>"
                            font: ro.fSmall; color: ro.ink2
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: if (!TrustFocus.open(grp.modelData.subject, grp.modelData.swingDir))
                                                       nj.openSubject = nj.openSubject === grp.modelData.subject ? "" : grp.modelData.subject }
                        }
                        PpTrustInlineFor {
                            width: grp.width; roles: ro
                            subject: nj.openSubject === grp.modelData.subject ? grp.modelData.subject : ""
                            swingDir: grp.modelData.swingDir
                            onClosed: nj.openSubject = ""
                        }
                    }
                }
            }
            ReviewFoot { width: sc.width }
        }
    }

    // ── SESSION: what keeps happening ──
    Component {
        id: sessionC
        Column {
            id: ssc
            objectName: "sdSessionView"
            readonly property Item asks: asks2
            PpDiagReplayCold {
                visible: ss.cold
                width: ssc.width
                topPadding: ro.u(10)
                roles: ro; header: root.header; expectations: root.source ? root.source.expectations : []
            }
            PpStandfirst {
                objectName: "sdSessionHead"
                visible: !ss.cold && ss.tense !== "none"
                width: ssc.width; roles: ro; brush: br; interactive: root.interactive
                kicker: "The session · " + ss.sessionShots + (ss.sessionShots === 1 ? " shot" : " shots")
                head: ro.cap1(ro.word((root.cards || []).length)) + ((root.cards || []).length === 1 ? " pattern" : " patterns")
                standfirst: !root.shot || ss.partial ? "Counted over " + ro.figHtml(ss.sessionShots) + " shots."
                          : ro.sessionStandfirst(root.cards, root.shot, ro.shows("longStandfirst", ssc.width))
            }
            // "How far can I trust this?" (§7.4) — its open body says what a gate kept from being judged
            // (the not-judged line lived here, below the coverage line, until stage 5)
            PpTrustSummaryFold {
                visible: !ss.cold && ss.tense !== "none" && !!root.source && String(root.source.sessionDir || "") !== "" && lines.length > 0
                width: ssc.width; roles: ro; scope: "session"
                topPadding: ro.u(6); bottomPadding: ro.u(6)
                sessionDir: root.source ? String(root.source.sessionDir || "") : ""
            }
            Item { visible: !ss.cold; width: 1; height: ro.u(10) }
            PpLedgerTable {
                visible: !ss.cold && (root.cards || []).length > 0
                width: ssc.width; roles: ro; brush: br; kind: "session"; rows: root.shownCards; readout: root.shot; source: root.source
                interactive: root.interactive
                // bookends are published at Closing only: before then their columns are not drawn at all (a "-" would read as unmeasured)
                spec: root.source && (root.source.bookends || []).length ? ro.tiers.session
                      : ro.tiers.session.filter(c => ["typical", "worst", "best"].indexOf(c.key) < 0)
                showEvidence: true
                showFocus: root.interactive
                title: "Pattern"
                // F34: the filter only when it can choose, and only on an interactive surface
                tools: root.interactive && (root.cards || []).length > 1
                       ? [{ t: "All", key: "all", on: root.cardFilter === "all" },
                          { t: "Reaches the ball", key: "ball", on: root.cardFilter === "ball" },
                          { t: "Roots", key: "roots", on: root.cardFilter === "roots" }] : []
                onToolActivated: k => root.setFilter(k)
                onConditionActivated: id => root.openCondition(id)
                onShotActivated: (d, s) => root.pickShot(d, s)
                onFocusActivated: (id, on) => { if (root.body && root.interactive) root.body._declareFocus(id, on) }
            }
            // F35: a filter that empties the list says which claim it is, in the old words
            PpNotes {
                objectName: "sdCardFilterEmpty"
                visible: root.filterEmpty
                width: ssc.width; roles: ro; small: true; tone: 2
                text: root.cardFilter === "ball"
                      ? "No pattern this session has an authored path to the ball. That is a statement about what the model has been written to claim, not about what these faults cost you."
                      : "Every pattern this session has another pattern authored as causing it."
            }
            PpNotes {
                visible: !ss.cold && (root.cards || []).length > 0 && !root.filterEmpty
                width: ssc.width; roles: ro; keepPrev: true
                text: "A pattern opens its detail; a strip cell or a typical / worst / best figure puts that shot in the carousel."
            }
            // the session has closed: what the counts cover and, with a shot read, which one is this (the
            // model's words, #17), under the table they explain
            PpNotes {
                objectName: "sdClosingLine"
                visible: !ss.cold && !!root.header && String(root.header.closingLine || "") !== ""
                width: ssc.width; roles: ro; keepPrev: true
                text: root.header ? ro.esc((root.shot ? root.header.closingLine : root.header.closingLineNoShot) || "") : ""
            }
            // F39: the capture's coverage, its own line, never omitted while there is a session
            PpNotes {
                objectName: "sdCoverageLine"
                visible: !!root.source && String(root.source.coverageLine || "") !== ""
                width: ssc.width; roles: ro
                text: root.source ? ro.cap1(root.source.coverageLine) + "." : ""
            }
            Asks { id: asks2; width: ssc.width; questions: root.sessionQuestions }
            ReviewFoot { width: ssc.width }
        }
    }

    // ── WATCHING: what is emerging ──
    Component {
        id: watchingC
        Column {
            id: wc
            objectName: "sdWatchingView"
            readonly property Item asks: asks3
            readonly property bool valuesInFigure: ro.shows("barcodeValue", wc.width)
            readonly property bool has: root.watching.length > 0
            PpStandfirst {
                objectName: "sdWatchingHead"
                visible: wc.has
                width: wc.width; roles: ro; brush: br; interactive: root.interactive
                kicker: "Watching · " + root.watching.length + " conditions"; head: "Seen, not yet a pattern"
                standfirst: root.shot && !ss.partial ? ro.watchStandfirst(root.source, root.shot, ro.shows("longStandfirst", wc.width))
                                                     : "Outside at least once, not yet a habit."
            }
            PpNotes {
                objectName: "sdWatchingEmpty"
                visible: !wc.has && ss.tense !== "none"
                width: wc.width; roles: ro; small: true; tone: 2
                topPadding: ro.u(10)
                text: "Nothing below the pattern gate yet — a condition that fires but has not recurred enough to be a pattern is listed here."
            }
            Item { visible: wc.has; width: 1; height: ro.u(6) }
            PpPageKey { visible: wc.has && !!ss.shot && !ss.partial; width: wc.width; roles: ro; readout: root.shot; set: "watch" }
            // with no shot picked, or live with nothing picked (D3), there is no run per watched condition to
            // draw: its names and counts (a barcode of blanks would be a gap, never "not measured")
            PpLedgerTable {
                visible: wc.has && (!ss.shot || ss.partial)
                width: wc.width; roles: ro; brush: br; readout: null
                rows: visible ? root.watching : []
                spec: ss.watchSpec
                getter: (w, key) => key === "name" ? { t: w.name } : { t: ro.frac(w.recurrence) }
                interactive: root.interactive
                onConditionActivated: id => root.openCondition(id)
            }
            PpBarcode {
                visible: wc.has && !!ss.shot && !ss.partial
                width: wc.width; roles: ro; brush: br; rows: root.watching; readout: root.shot; source: root.source
                interactive: root.interactive
                onConditionActivated: id => root.openCondition(id)
                onShotActivated: (d, s) => root.pickShot(d, s)
            }
            Asks { id: asks3; width: wc.width; questions: root.watchQuestions }
            ReviewFoot { width: wc.width }
        }
    }

    // ── DETAIL: how far, how often, and why ──
    Component {
        id: detailC
        Item {
            id: dRoot
            objectName: "sdDetailBody"
            readonly property Item asks: dc.asks
            height: dc.height
            // F47: Esc closes the detail where focus allows (the BACK link is the affordance that always works)
            focus: root.view === "detail"
            Keys.onEscapePressed: if (root.body && root.interactive) root.body._closeDetail()
            Connections {
                target: root
                function onViewChanged() { if (root.view === "detail" && root.interactive && root.medium === "screen") dRoot.forceActiveFocus() }
            }
            Component.onCompleted: if (root.view === "detail" && root.interactive && root.medium === "screen") forceActiveFocus()
            Column {
                id: dc
                width: dRoot.width
                readonly property Item asks: asks4
                // causes: a merged graph from medium up; below medium B's typeset chains
                readonly property bool graph: ro.atLeast(dc.width, "medium")
                readonly property var hdr: (root.detail && root.detail.header) || ({})
                readonly property var sp: hdr.spread || ({})
                readonly property var measures: (root.detail && root.detail.measures) || []
                // A COMPOSITE (posture_metrics_design.md §8): its number is never shown, so the page opens on
                // What changed and draws no figure row — a run of composite readings would be that number.
                readonly property var wc: (root.detail && root.detail.whatChanged) || ({})
                readonly property bool hasWc: (wc.rows || []).length > 0
                readonly property bool composite: !!(root.detail && root.detail.composite)
                PpFeatureHead {
                    objectName: "sdFeatureHead"
                    width: dc.width; roles: ro; brush: br; detail: root.detail; readout: root.shot; source: root.source
                    interactive: root.interactive
                    showPull: false              // PpKeyFigures carries this shot and the median
                    onShotActivated: (d, s) => root.pickShot(d, s)
                }
                Item { width: 1; height: ro.u(10) }
                // WHAT CHANGED, open on arrival; the coverage ("read from 3 of 4 parts") is said once, in the
                // trust fold under it, never on the table
                PpWhatChanged {
                    visible: dc.hasWc
                    width: dc.width; roles: ro; info: dc.wc; interactive: root.interactive
                }
                PpTrustSummaryFold {
                    objectName: "sdWhatChangedTrust"
                    visible: dc.hasWc && lines.length > 0
                    width: dc.width; roles: ro; scope: "shot"
                    topPadding: ro.u(8)
                    swingDir: root.detail ? String(root.detail.whatChangedSwingDir || "") : ""
                    sessionDir: root.source ? String(root.source.sessionDir || "") : ""
                    extraLines: dc.wc.trustLines || []
                }
                Item { visible: dc.hasWc; width: 1; height: ro.u(14) }
                // A's key figures: this shot, the median and how many went past the fault line (the fact file already
                // says corridor, recurrence, after and trend), then the distance to travel (round 3)
                PpKeyFigures {
                    id: kf
                    width: dc.width; roles: ro; brush: br; detail: root.detail; readout: root.shot
                    exclude: ["pass", "faultLine", "faultOutside", "fired", "after", "trend"]
                    items: kf.defaultItems.concat(root.travelItems)
                }
                Item { width: 1; height: ro.u(18) }
                // F49: one tab per measure, the shown one ruled; the pick goes to the model and survives a rebuild
                Flow {
                    objectName: "sdDetailMeasureTabs"
                    visible: dc.measures.length > 1
                    width: dc.width
                    spacing: ro.u(22)
                    bottomPadding: ro.u(12)
                    Repeater {
                        model: dc.measures.length > 1 ? dc.measures : []
                        delegate: Item {
                            id: mt
                            objectName: "sdDetailMeasureTab"
                            required property var modelData
                            readonly property string measureId: modelData.id || ""
                            readonly property bool on: (root.detail && root.detail.measureId) === measureId
                            width: Math.min(mtc.implicitWidth, dc.width); height: mtc.implicitHeight + ro.u(6)
                            Column {
                                id: mtc
                                width: Math.min(implicitWidth, dc.width)
                                Text { width: Math.min(implicitWidth, dc.width); wrapMode: Text.WordWrap
                                       text: ro.cap1(mt.modelData.label || ""); font: mt.on ? ro.fSmallM : ro.fSmallL
                                       color: mt.on ? ro.ink : ro.link }
                                Text { text: mt.modelData.countText || ""; font: ro.fFigLabel; color: ro.ink3 }
                            }
                            Rectangle { visible: mt.on; anchors.bottom: parent.bottom; width: parent.width; height: 2; color: ro.ink }
                            MouseArea { anchors.fill: parent; enabled: root.interactive && !mt.on; cursorShape: Qt.PointingHandCursor
                                        onClicked: if (root.body) root.body._setDetailMeasure(mt.measureId) }
                        }
                    }
                }
                PpPageKey { visible: !dc.composite; width: dc.width; roles: ro; readout: root.shot; set: "figure" }
                Item { visible: !dc.composite; width: 1; height: ro.u(14) }
                PpFigureRow {
                    objectName: "sdFigureRow"
                    visible: !dc.composite
                    width: dc.width; roles: ro; brush: br; detail: root.detail; readout: root.shot
                    interactive: root.interactive
                    onShotActivated: (d, s) => root.pickShot(d, s)
                }
                Asks { id: asks4; width: dc.width; questions: root.detailQuestions }
                Item { width: 1; height: ro.u(10) }
                PpCauseGraph {
                    visible: dc.graph
                    width: dc.width; roles: ro; brush: br; detail: visible ? root.detail : null; which: "causes"
                    interactive: root.interactive
                    onConditionActivated: id => root.openCondition(id)
                    onScreenRequested: id => root.requestScreen(id)
                }
                PpCauseGraph {
                    visible: dc.graph
                    width: dc.width; roles: ro; brush: br; detail: visible ? root.detail : null; which: "effects"
                    interactive: root.interactive
                    onConditionActivated: id => root.openCondition(id)
                    onScreenRequested: id => root.requestScreen(id)
                }
                PpPathChains {
                    visible: !dc.graph
                    width: dc.width; roles: ro; brush: br; detail: visible ? root.detail : null; readout: root.shot; which: "both"
                    interactive: root.interactive
                    onConditionActivated: id => root.openCondition(id)
                }
                // F64: a screened root is settled by a physical screen, not by this capture. Where the graph draws
                // no CTA (below medium), and always when the open condition IS one, the request is stated once.
                Flow {
                    objectName: "sdScreenRequests"
                    visible: root.screenedRoots.length > 0 && (!dc.graph || dc.hdr.kind === "screenedRoot")
                    width: dc.width
                    spacing: ro.u(14)
                    topPadding: ro.u(14)
                    Text { text: "Settled by a physical screen, not by this capture:"; font: ro.fSmall; color: ro.ink3 }
                    Repeater {
                        model: root.screenedRoots
                        delegate: Text {
                            objectName: "sdScreenCta"
                            required property var modelData
                            readonly property string conditionId: modelData.id
                            text: ro.cap1(modelData.name || modelData.id) + (root.interactive ? " · Run the screen" : "")
                            font: root.interactive ? ro.fSmallL : ro.fSmall; color: root.interactive ? ro.link : ro.ink2
                            MouseArea { anchors.fill: parent; anchors.margins: -ro.u(3); enabled: root.interactive
                                        cursorShape: Qt.PointingHandCursor; onClicked: root.requestScreen(parent.conditionId) }
                        }
                    }
                }
            }
        }
    }

    // the question engine (functions + applying an asked question to the brush); never drawn
    PpQuestions { id: qh; visible: false; roles: ro; brush: br }

    // ── the shared objects, declared LAST so they are destroyed last ──
    PpDiagReplayState { id: ss; source: root.source; header: root.header; readout: root.readout; cards: root.cards || []; interactive: root.interactive }
    PpDiagRoles { id: ro; medium: root.medium }
    PpBrush {
        id: br
        cards: root.cards || []
        pickedShot: ss.shot ? ss.shot.shotId : -1
        pickedCondition: root.view === "detail" && root.detail ? (root.detail.id || "") : ""
    }
}
