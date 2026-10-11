// Historical example (11 Oct 2026): the test wrapper that synthesised every live state of the
// Session diagnostics panel from real data for the mock harness (demoState → cold, after-shot,
// quiet, declared miss, 23-shot session, casts…). It targets the round-4 mock registers archived at
// /mnt/swingdata/scratch/typeset-harness/archive/2026-10-11/mock_reg_round4; adapt its pattern for another panel's live states.
import QtQuick
import PinPointStudio

// LIVESTATES — a TEST WRAPPER for the render harness. It never ships.
// It takes the real data the harness feeds (a reviewed 15-shot session, shot 8 picked) and, from
// `demoState`, SYNTHESISES each live state the model can publish by overriding the fields INVENTORY §3
// names (source.stage / quiet / afterShotDelta / thisShot / expectations / declaredMiss / focus,
// header.coldLine / countLine / reviewBadge / closed, the readout, the cards), then composes Panel.qml
// with them. Real data is the base: an empty demoState passes everything straight through.
// The synthetic source is a plain object: the model's fields copied, its invokables forwarded.
// States: name[@replay|@analyse]; the suffix forces the register (Panel.forceRegister).
//   noSource cold0 cold2 forming established afterShot quiet liveNoPick liveOlderPick closing
//   reviewNoPick zeroFired missDeclared missPicker focus detailInReplay filterBall filterEmpty
//   watchingEmpty long23 castAuto castPersistent, and the registers' own demo states (hoverShot12, …),
//   passed through.
// Every synthetic session is cut from the real one (its first N shots; a 23-shot session repeats shots
// 1–8 as 16–23 and skips swing id 6, as a deleted swing would). Where a state needs a value the real
// data cannot give (a quiet shot), the pattern firings of the newest shot are cleared: noted as synthetic.
Item {
    id: ls
    // ── the harness contract ──
    property string view: "shot"
    property var readout: null
    property var cards: []
    property var source: null
    property var header: null
    property var detail: null
    property var body: null
    property string demoState: ""
    property string surface: "panel"
    property string medium: "screen"
    property string printOrientation: ""
    property int printPage: 0
    readonly property int printPageCount: 1
    readonly property var demoStates: ["noSource", "cold0", "cold2", "forming", "established", "afterShot", "quiet", "liveNoPick",
        "liveOlderPick", "closing", "reviewNoPick", "zeroFired", "missDeclared", "missPicker", "focus", "detailInReplay",
        "filterBall", "filterEmpty", "watchingEmpty", "long23", "castAuto", "castPersistent", "measureTabs", "screenedRoot"]

    readonly property string stateName: demoState.split("@")[0]
    readonly property string forced: { const p = demoState.split("@")[1] || ""; return p === "replay" ? "Replay" : p === "analyse" ? "Analyse" : "" }
    readonly property var syn: build(stateName, readout, cards, source, header, detail)

    // Follow the body we lie over (the harness sizes us once).
    Component.onCompleted: {
        if (!parent) return
        const kids = parent.children || []
        for (let i = 0; i < kids.length; ++i) {
            const b = kids[i]
            if (b !== ls && b.objectName === "sdBody") {
                ls.width = Qt.binding(() => b.width); ls.height = Qt.binding(() => b.height)
                ls.x = Qt.binding(() => b.x); ls.y = Qt.binding(() => b.y)
                break
            }
        }
    }

    Panel {
        id: panel
        anchors.fill: parent
        body: ls.body
        medium: ls.medium
        view: ls.syn.view !== undefined ? ls.syn.view : ls.view
        readout: ls.syn.has ? ls.syn.readout : ls.readout
        cards: ls.syn.has ? ls.syn.cards : ls.cards
        source: ls.syn.has ? ls.syn.source : ls.source
        header: ls.syn.has ? ls.syn.header : ls.header
        detail: ls.syn.has ? ls.syn.detail : ls.detail
        demoState: ls.syn.regDemo || ""
        surface: ls.syn.surface || ls.surface
        forceRegister: ls.syn.register || ls.forced
        interactive: ls.syn.interactive !== undefined ? ls.syn.interactive : (ls.body && ls.body.interactive !== undefined ? ls.body.interactive : true)
        cardFilter: ls.syn.cardFilter !== undefined ? ls.syn.cardFilter : (ls.body && ls.body.cardFilter !== undefined ? ls.body.cardFilter : "all")
        pulseCue: ls.syn.pulseCue !== undefined ? ls.syn.pulseCue : (ls.body ? ls.body.pulseCue : null)
    }

    // ═══════════════════════════ synthesis ═══════════════════════════
    readonly property var srcKeys: ["objectName", "cadence", "gradePolicy", "reviewing", "selectedShotId", "selectedSwingDir", "sessionDir",
        "stage", "closed", "shotCount", "patternCount", "busy", "focusConditionId", "focusFromShot", "declaredMiss", "declaredMissName",
        "missChain", "headerInfo", "thisShot", "quiet", "afterShotDelta", "cards", "watching", "chains", "unchained", "unchainedLine",
        "driver", "coverageLine", "bookends", "expectations", "detailConditionId", "detail"]
    function srcCopy(s, over) {
        if (!s) return null
        const o = {}
        for (const k of srcKeys) o[k] = s[k]
        o.missCandidates = () => (typeof s.missCandidates === "function" ? s.missCandidates() : [])
        o.shotReadout = id => s.shotReadout(id)
        return Object.assign(o, over || {})
    }
    function recOf(ticks) {
        const m = ticks.filter(t => t.state === "fired" || t.state === "clean").length, f = ticks.filter(t => t.state === "fired").length
        return f + " of " + m + " measurable shots"
    }
    function fig(v) { const m = String(v || "").match(/^\s*([−\-+]?\d[\d.,]*)/); return m ? m[1] : "-" }
    // a readout of the shot at index i, cut to the first n shots (ids per idOf)
    function readoutAt(s, r0, i, n, idOf) {
        const base = s && typeof s.shotReadout === "function" ? s.shotReadout(((r0.conditions[0] || {}).ticks || [])[i % 15].shotId) : r0
        const r = Object.assign({}, base)
        r.shotIndex = i; r.shotCount = n; r.shotId = idOf(i)
        r.conditions = (base.conditions || []).map(c => {
            const t = cut(c.ticks || [], n, idOf, i)
            return Object.assign({}, c, { ticks: t, recurrence: recOf(t), firingsAfterText: "", selectedIndex: i })
        })
        return r
    }
    // the first n ticks (repeating 1–15 past 15), relabelled
    function cut(ticks, n, idOf, pick) {
        const out = []
        for (let j = 0; j < n; ++j) { const t = ticks[j % ticks.length] || { state: "clean" }; out.push(Object.assign({}, t, { shotId: idOf(j), selected: j === pick })) }
        return out
    }
    // a card re-pointed at shot i of a session cut to n shots
    function cardAt(c, i, n, idOf, live) {
        const t = cut(c.ticks || [], n, idOf, i)
        const run = (c.spread && c.spread.run) || []
        const rr = []
        for (let j = 0; j < n; ++j) { const r = run[j % run.length]; if (r) rr.push(Object.assign({}, r, { index: j, shotId: idOf(j), current: j === i, selected: j === i })) }
        const cur = rr[i]
        const sp = Object.assign({}, c.spread || {}, { run: rr, currentNumber: cur && cur.assessable ? fig(cur.valueText) : "-",
                                                       currentReadout: cur ? String(cur.readout || "").replace(/^shot \d+/, "shot " + (i + 1)) : "" })
        const o = Object.assign({}, c, { ticks: t, recurrence: recOf(t), spread: sp,
                                          thisShot: t[i] ? t[i].state : "notAssessable",
                                          valueText: cur && cur.assessable ? cur.valueText : "not measurable" })
        if (live) o.firingsAfterText = ""
        return o
    }
    function deltaOf(r, surfaced) {
        const fired = (r.conditions || []).filter(c => c.stateKind === "fired"), clean = (r.conditions || []).filter(c => c.stateKind === "clean")
        const na = (r.conditions || []).filter(c => c.stateKind === "notAssessable"), pats = fired.filter(c => c.tierTag === "pattern").length
        return { shotId: r.shotId, shotIndex: r.shotIndex, firedCount: fired.length, cleanCount: clean.length, notAssessableCount: na.length,
                 firedPatternCount: pats, fired: fired.map(chip), newPatterns: [], streakBroken: false, surfaced: surfaced,
                 headline: fired.length + " of " + (fired.length + clean.length) + " conditions fired on this swing · " + pats + " of them " + (pats === 1 ? "pattern" : "patterns"),
                 note: na.length ? na.length + " " + (na.length === 1 ? "measure" : "measures") + " not assessable on this capture" : "" }
    }
    function chip(c) { return { id: c.id, name: c.name, kind: "fired", tier: c.tierTag === "pattern" ? "pattern" : "watching", strength: c.strength,
                                strengthExcess: c.strengthExcess, strengthKnown: c.strengthKnown, strengthText: c.strengthText, focus: false } }
    function thisShotOf(r) {
        const d = deltaOf(r, true), out = (r.conditions || []).filter(c => c.stateKind === "fired").map(chip)
        if (d.notAssessableCount) out.push({ kind: "notAssessable", name: d.notAssessableCount + " not assessable on this capture" })
        return out
    }
    function watchingOf(r, cardsList) {
        const pats = {}
        for (const c of cardsList) pats[c.id] = true
        return (r.conditions || []).filter(c => !pats[c.id] && c.ticks.some(t => t.state === "fired"))
                                   .map(c => ({ id: c.id, name: c.name, recurrence: c.recurrence }))
    }
    function liveHeader(h, over) {
        return Object.assign({}, h || {}, { reviewBadge: "", reviewNote: "", reviewing: false, closed: false, closingLine: "", reviewFootLine: "",
                                            cadenceNote: "", coldLine: "" }, over || {})
    }
    // a live session cut to its first n shots, newest picked (or nothing picked)
    function liveSession(r0, c0, s, h, n, stage, picked, opts) {
        const idOf = j => j + 1, i = n - 1
        const r = readoutAt(s, r0, i, n, idOf)
        // with no cards (Cold) nothing is a pattern yet
        if (opts && opts.noCards) r.conditions = r.conditions.map(c => c.tierTag === "pattern" ? Object.assign({}, c, { tierTag: "watching" }) : c)
        const cs = (opts && opts.noCards) ? [] : c0.map(c => cardAt(c, i, n, idOf, true))
        if (opts && opts.fresh) for (const k of opts.fresh) if (cs[k]) cs[k].fresh = true
        const ws = watchingOf(r, cs)
        const src = srcCopy(s, { stage: stage, closed: false, reviewing: false, shotCount: n, patternCount: cs.length, cards: cs, watching: ws,
                                 bookends: [], afterShotDelta: deltaOf(r, true), thisShot: thisShotOf(r), quiet: false,
                                 selectedShotId: picked ? r.shotId : -1, expectations: (opts && opts.expectations) || [],
                                 declaredMiss: "", declaredMissName: "", focusConditionId: "" })
        const hdr = liveHeader(h, { stage: stage, stageLabel: stage.toUpperCase(), shotCount: n, patternCount: cs.length,
                                    countLine: cs.length + (cs.length === 1 ? " pattern" : " patterns") + " · counted over all " + n + (n === 1 ? " shot" : " shots"),
                                    shotLabel: "shot " + n })
        return { has: true, readout: picked ? r : null, cards: cs, source: src, header: hdr, detail: ls.detail, newest: r }
    }
    readonly property var expectationsDemo: [
        { id: "over_the_top", name: "Over the top", text: "pattern in 9 of your last 10 sessions", caveat: "an expectation to test, not a finding" },
        { id: "early_extension", name: "Early extension", text: "pattern in 7 of your last 10 sessions", caveat: "an expectation to test, not a finding" },
        { id: "hanging_back", name: "Hanging back", text: "pattern in 4 of your last 9 sessions", caveat: "an expectation to test, not a finding" }]

    function build(name, r0, c0, s, h, d) {
        const none = { has: false }
        if (!name) return none
        c0 = c0 || []
        switch (name) {
        case "noSource":
            return { has: true, readout: null, cards: [], source: null, header: null, detail: null }
        case "cold0": {
            const src = srcCopy(s, { stage: "cold", closed: false, reviewing: false, shotCount: 0, patternCount: 0, cards: [], watching: [], bookends: [],
                                     afterShotDelta: ({}), thisShot: [], quiet: false, selectedShotId: -1, expectations: [], declaredMiss: "",
                                     declaredMissName: "", focusConditionId: "" })
            return { has: true, readout: null, cards: [], source: src, detail: null,
                     header: liveHeader(h, { stage: "cold", stageLabel: "COLD", shotCount: 0, patternCount: 0, coldLine: "Too few swings to call a pattern.",
                                             countLine: "0 patterns · counted over all 0 shots", shotLabel: "no shots yet", reachedEstablished: false }) }
        }
        case "cold2": {
            const o = liveSession(r0, c0, s, h, 2, "cold", true, { noCards: true, expectations: expectationsDemo })
            o.header.coldLine = "Too few swings to call a pattern."
            o.header.reachedEstablished = false
            return o
        }
        case "forming":
            return liveSession(r0, c0, s, h, 6, "forming", true, { fresh: [2, 5] })
        case "established":
            return liveSession(r0, c0, s, h, 12, "established", true)
        case "afterShot": {
            const o = liveSession(r0, c0, s, h, 15, "established", true)
            const fired = (o.newest.conditions || []).filter(c => c.stateKind === "fired").map(c => c.id)
            const order = o.cards.map(c => c.id).filter(id => fired.indexOf(id) >= 0).concat(fired.filter(id => !o.cards.some(c => c.id === id)))
            o.pulseCue = { token: 1, ids: order, focusId: "", hold: true }
            return o
        }
        case "quiet": {
            const o = liveSession(r0, c0, s, h, 15, "established", true)
            // synthetic: a quiet shot needs no pattern firing on the newest swing; the real one has five, so they are cleared
            const r = Object.assign({}, o.readout)
            r.conditions = r.conditions.map(c => c.stateKind === "fired" && c.tierTag === "pattern"
                                             ? Object.assign({}, c, { stateKind: "clean", state: "IN", strengthExcess: 1.2, ticks: c.ticks.map((t, j) => j === 14 ? Object.assign({}, t, { state: "clean" }) : t) }) : c)
            o.readout = r
            o.cards = o.cards.map(c => c.thisShot === "fired" ? Object.assign({}, c, { thisShot: "clean", ticks: c.ticks.map((t, j) => j === 14 ? Object.assign({}, t, { state: "clean" }) : t) }) : c)
            o.source.cards = o.cards
            o.source.quiet = true
            o.source.afterShotDelta = deltaOf(r, false)
            o.source.thisShot = thisShotOf(r)
            o.header.cadenceNote = "BANDWIDTH · QUIET"
            return o
        }
        case "liveNoPick":
            return liveSession(r0, c0, s, h, 15, "established", false)
        case "liveOlderPick": {
            const cs = c0.map(c => Object.assign({}, c, { firingsAfterText: "" }))
            const r = Object.assign({}, r0, { conditions: (r0.conditions || []).map(c => Object.assign({}, c, { firingsAfterText: "" })) })
            return { has: true, readout: r, cards: cs, detail: d,
                     source: srcCopy(s, { stage: "established", closed: false, reviewing: false, cards: cs, bookends: [] }),
                     header: liveHeader(h, { stage: "established", stageLabel: "ESTABLISHED", shotLabel: "shot 15" }) }
        }
        case "closing":
            return { has: true, readout: null, cards: c0, detail: d,
                     source: srcCopy(s, { stage: "closing", closed: true, reviewing: false, selectedShotId: -1 }),
                     header: liveHeader(h, { stage: "closing", stageLabel: "CLOSING", closed: true, shotLabel: "shot 15" }) }
        case "reviewNoPick":
            return { has: true, readout: null, cards: c0, detail: d, source: srcCopy(s, { selectedShotId: -1 }),
                     header: Object.assign({}, h, { reviewBadge: "REVIEWING · " + (h ? h.shotCount : 15) + " shots" }) }
        case "zeroFired": {
            const r = Object.assign({}, r0)
            r.conditions = (r0.conditions || []).map(c => c.stateKind === "fired" ? Object.assign({}, c, { stateKind: "clean", state: "IN", strengthExcess: 1.2,
                                                         ticks: c.ticks.map((t, j) => j === r0.shotIndex ? Object.assign({}, t, { state: "clean" }) : t) }) : c)
            const cs = c0.map(c => c.thisShot === "fired" ? Object.assign({}, c, { thisShot: "clean" }) : c)
            return { has: true, readout: r, cards: cs, detail: d, source: srcCopy(s, { cards: cs }), header: h }
        }
        case "missDeclared":
        case "missPicker": {
            const o = { has: true, readout: r0, cards: c0, detail: d, header: h,
                        source: srcCopy(s, { declaredMiss: "pull", declaredMissName: "Pull" }) }
            if (name === "missPicker") o.regDemo = "missPicker"
            return o
        }
        case "focus": {
            const cs = c0.map(c => Object.assign({}, c, { focused: c.id === "early_extension" }))
            return { has: true, readout: r0, cards: cs, detail: d, header: h, source: srcCopy(s, { cards: cs, focusConditionId: "early_extension" }) }
        }
        case "detailInReplay":
            return { has: false, register: "Replay" }
        case "measureTabs":       // run with the harness detail id early_extension (two measures)
        case "screenedRoot":      // run with a screened root's detail id (poor_core_stability)
            return { has: false }
        case "filterBall":
            return { has: false, cardFilter: "ball" }
        case "filterEmpty": {
            const cs = c0.map(c => Object.assign({}, c, { reachesBall: false }))
            return { has: true, readout: r0, cards: cs, detail: d, header: h, source: srcCopy(s, { cards: cs }), cardFilter: "ball" }
        }
        case "watchingEmpty":
            return { has: true, readout: r0, cards: c0, detail: d, header: h, source: srcCopy(s, { watching: [] }) }
        case "long23": {
            const n = 23, i = r0 ? r0.shotIndex : 7, idOf = j => j < 5 ? j + 1 : j + 2        // swing id 6 deleted
            const r = Object.assign({}, r0, { shotCount: n, shotIndex: i, shotId: idOf(i),
                         conditions: (r0.conditions || []).map(c => { const t = cut(c.ticks || [], n, idOf, i); return Object.assign({}, c, { ticks: t, recurrence: recOf(t) }) }) })
            const cs = c0.map(c => cardAt(c, i, n, idOf, false))
            const bk = ((s && s.bookends) || []).map(b => Object.assign({}, b, { representativeShotId: idOf(b.representativeShotId - 1), worstShotId: idOf(b.worstShotId - 1), bestShotId: idOf(b.bestShotId - 1) }))
            const ws = ((s && s.watching) || []).map(w => { const c = r.conditions.find(x => x.id === w.id); return c ? Object.assign({}, w, { recurrence: c.recurrence }) : w })
            const hdr = Object.assign({}, h, { shotCount: n, reviewBadge: "REVIEWING · shot " + (i + 1) + " of " + n,
                                                countLine: cs.length + " patterns · counted over all " + n + " shots", shotLabel: "shot " + (i + 1) + " of " + n })
            return { has: true, readout: r, cards: cs, detail: d, header: hdr,
                     source: srcCopy(s, { shotCount: n, cards: cs, bookends: bk, watching: ws, selectedShotId: idOf(i) }) }
        }
        case "castAuto":
            // the body's _tab rule on a non-interactive cast: This shot whenever a shot is available
            return { has: false, surface: "cast", interactive: false, view: "shot" }
        case "castPersistent":
            return { has: false, surface: "cast", interactive: true }
        default:
            return { has: false, regDemo: name }          // a register's own demo state (hoverShot12, question-…)
        }
    }

    // The pulse for a still: afterShot's cue carries `hold`, and the register sets Brush.pulseHold, so every
    // row's PulseWash holds at full for the grab instead of fading in half a second.
}
