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

// THE SESSION DIAGNOSTICS HARNESS, shared by the five tst_session_diagnostics_<part>.qml files (not a tst_
// file, so qml_ui does not run it on its own). Each file is this Item, with a SessionDiagnosticsCase (the
// helpers, init() and the theme put back) holding its tests. Split from tst_session_diagnostics.qml (#254);
// its header, which is the whole suite's, follows.
//
// The session diagnostics panel's body, loaded for real and pressed through its states — the TYPESET
// panel (design system §15; INVENTORY of 11 Oct 2026 §4 is the test map this file was rewritten from).
//
// THE BODY HOSTS A REGISTER. PpSessionDiagnosticsBody keeps its data and state API (source, readout,
// tab, cardFilter, detailOpen, pulseCue, _openDetail … shotRequested) and loads ONE of two registers:
//   · PpDiagReplay  — the glance: Capture, Replay, and the second-screen cast (surface "cast", D4);
//   · PpDiagAnalyse — the read: 2 /* Analyse */.
// So every test says which register it presses (useRegister), and asserts both where the behaviour
// belongs to both. A view (This shot / Session / Watching / the Analyse detail) is its own named
// Flickable, built on first show and never torn down (D8), so a view is found by its objectName only
// after it has been shown.
//
// THE ASSERTIONS THAT EARN THIS FILE are still the ones about RESTRAINT, the ones a screenshot cannot make:
//   · Cold with an empty fault profile omits the "usually yours" block ENTIRELY;
//   · a shot the capture could not measure is a STUB — present, distinct from a clean tick, never a gap;
//   · a quiet shot says so in one calm line and keeps its ruler; the read in Analyse is never gated;
//   · the two strong link grades carry an arrowhead and the three weak ones never do;
//   · a zero count is a word ("none"), never "no of";
//   · type never grows with the panel (house rule 2), and nothing is ellipsised.
//
// FIXTURES. Two kinds, both in the shape SessionDiagnosticsModel publishes:
//   · small hand-written sources below (cold, forming, established, closing, review, the detail), for
//     the behaviours that need exact strings and counts;
//   · the REAL session in data/diag_session.js (the 4 July Wrist session, 78 conditions a shot, eight
//     cards with their spreads, a detail with two measures), and the live states cut from it the way the
//     register round's harness cut them — for the layout rules, which only mean something on real data.
//
// TEST MAP (old → new; the full table, with the reasons, is in the scratch TESTS_NOTES.md).
//   KEPT (same behaviour, new structure) — the old numbers are kept as the function names' prefixes:
//     04 05b 05c 05d 07 07b 07c 07d 08 09 11 11c 12 14 16b 19 20 21 22 23 24 25 26 28 36 38 39 40 42
//     43 44 45 47 48
//   REWRITTEN FROM "NEEDS NEW DESIGN" (the design now exists: BRIEF4 D1-D11, the registers' NOTES4):
//     01 02 03 (Cold, expectations) · 05 (the not-measured stub, D5) · 06 (the quiet shot, D2)
//     15 (five link grades, D6) · 16 (node kinds, the screened-root request, F56/F64) · 27 (focus on the
//     rows, F33) · 29 (a screened root asks for its screen) · 30 (the declared miss, retired)
//     32 33 (the after-shot pulse and reduceMotion, D1)
//   NEW (what the redesign promises): 50-61, see each function. The shot outcomes: 65-74.
//   OBSOLETE, deleted — each with its reason:
//     test_31_theMissPickerOpensInPlaceAndDeclaresOne — the declared-miss picker is retired; corrections
//         replace it (test_67).
//     test_10_designSizeIsScaleOneAndAWiderStageScalesUp — the panel fit factor `k` is gone (house rule
//         2: type never scales with the panel). Replaced by test_55 (a font role's size is the same in a
//         480 and a 1522 panel).
//     test_11b_theViewportHoldsWholeRowsOfCards — there is no card grid: ledger rows are atoms (RULES 6),
//         and the grid's "whole rows" arithmetic has nothing left to divide (D11).
//     test_34_theStripOrdersByInformationAndFoldsTheSilentTail — the silent-tail fold is replaced by
//         closeness ordering + a count (S37, D11). Its one surviving claim ("what fired comes first") moved
//         into test_23.
//     test_35_theTailOpensInPlaceIntoTheSameCells — there is no tail to open (S37, D11).
//     test_41_theDetailGoesVerticalWhenCompact — the vertical rails are gone; below medium the causes
//         CHANGE FORM into PpPathChains (covered by test_19); Back at narrow is in test_39.
//     test_46_theFiguresFitTheCardsAtEverySize — there are no card figures; the ledger folds its columns
//         instead. Replaced by test_58 (fold, don't drop).
//   Also gone with the components they asserted (no test of their own): the strength meter's 5-step
//   ladder (an "out by" figure that refuses to guess replaces it, test_05b), the verdict pills
//   FIRED/CLEAN (the value's colour carries "outside", RULES 7), "scroll for the rest" (the scroll bar
//   says it), the newest-swing after-shot strip as a separate drawing (one shot read for every pick),
//   the corridor strip's "← better / worse →" words and nearest-dot picking (every mark is its own
//   pick target now), and the "twice its first height" run (no fit factor).
//
// It loads PpSessionDiagnosticsBody rather than PpSessionDiagnosticsPanel deliberately: the body takes a
// `source` and owns none of it, so the fixture is the exact shape the model publishes and nothing here
// stands in for the thing under test. Silent: nothing here plays a sound or opens a window.
Item {
    id: probe
    // The body and the pip row under test, by the names every test file reads (an id does not cross files).
    readonly property alias body: diagBody
    readonly property alias pipProbe: pipRow
    width: 1168; height: 560

    // app.state, faked (refactor step 4-a): the mode is the test's to set, as SessionMode.mode was; nothing is selected
    // and nothing plays. Put on this file's engine by the fixture (chartsFixture.provide), as the charts tests do.
    QtObject {
        id: fakeState
        readonly property int captureMode: 0
        readonly property int replayMode: 1
        readonly property int analyseMode: 2
        property int mode: 0
        property bool autoReturnToCapture: false
        readonly property QtObject selection: QtObject {
            property string shotRef: ""
            property string swingDir: ""
            readonly property bool valid: swingDir !== ""
            signal changed()
        }
        readonly property QtObject castSelection: selection
        readonly property QtObject replay: QtObject {
            property bool active: false
            property string swingDir: ""
            property real positionUs: 0
        }
        property string sessionOnScreenFolder: ""
        readonly property var sessionOnScreen: null
    }
    QtObject { id: fakeApp; readonly property var state: fakeState; readonly property var session: null; function sessionAt(f) { return null } }
    Component.onCompleted: chartsFixture.provide("app", fakeApp)

    property var src: null
    // shotReadout() is an invokable, so the panel fetches it and hands it down; the body takes it as a
    // second input. Null is "no shot selected", which is a state under test in its own right.
    property var readout: null

    // What the panel asked for, and never what it did about it.
    property string lastScreenRef: ""
    property string lastScreenCondition: ""
    property int    screenCalls: 0
    property string lastShotDir: ""
    property int    lastShotId: -1
    property int    shotCalls: 0

    // The carousel's half (the pip row), pressed on its own; declared first so the body is on top.
    PpShotPipRow {
        id: pipRow
        width: Theme.sp(127); height: Theme.sp(14)
        pips: probe.pipFixture
        firedCount: probe.pipFired
    }
    property var pipFixture: []
    property int pipFired: 0

    PpSessionDiagnosticsBody {
        id: diagBody
        anchors.fill: parent
        source: probe.src
        readout: probe.readout
        onScreenRequested: (ref, cond) => {
            probe.screenCalls += 1
            probe.lastScreenRef = ref
            probe.lastScreenCondition = cond
        }
        onShotRequested: (dir, id) => { probe.shotCalls += 1; probe.lastShotDir = dir; probe.lastShotId = id }
    }

    // ── the declarations, spied ──────────────────────────────────────────────
    // declareFocus / clearFocus / missCandidates / correctOutcome / setShotIntent / setIntentFrom /
    // flagForReview / openDetail / closeDetail / setDetailMeasure are INVOKABLES on the model, reached through `source`. A fixture supplies spies,
    // so the test asserts what the panel ASKED FOR. The render is asserted against fixtures that already
    // carry the model's answer (a plain JS object's properties are not bindable: a spy that mutated the
    // fixture would change nothing on screen).
    property int    focusCalls: 0
    property string lastFocusId: ""
    property int    clearFocusCalls: 0
    property int    correctCalls: 0
    property int    lastCorrectShot: -1
    property string lastCorrectFamily: ""
    property string lastCorrectTo: "\u0000"   // distinguishable from correctOutcome(id, family, "")
    property int    lastIntentShot: -1
    property string lastIntentShape: "\u0000"
    property int    lastIntentFromShot: -1
    property string lastIntentFromShape: "\u0000"
    property int    flagCalls: 0
    property int    lastFlagShot: -1
    property int    detailCalls: 0
    property string lastDetailId: ""
    property int    closeDetailCalls: 0
    property int    measureCalls: 0
    property string lastMeasureId: ""

    readonly property var missRows: [
        { id: "chunk", name: "Fat",   family: "strike" },
        { id: "thin",  name: "Thin",  family: "strike" },
        { id: "slice", name: "Slice", family: "direction" },
        { id: "hook",  name: "Hook",  family: "direction" }
    ]

    function spied(s) {
        if (!s) return s
        s.declareFocus     = function (id) { probe.focusCalls += 1; probe.lastFocusId = id }
        s.clearFocus       = function ()   { probe.clearFocusCalls += 1 }
        s.correctOutcome   = function (id, fam, to) { probe.correctCalls += 1; probe.lastCorrectShot = id; probe.lastCorrectFamily = fam; probe.lastCorrectTo = to }
        s.setShotIntent    = function (id, shape)   { probe.lastIntentShot = id; probe.lastIntentShape = shape }
        s.setIntentFrom    = function (id, shape)   { probe.lastIntentFromShot = id; probe.lastIntentFromShape = shape }
        s.flagForReview    = function (id)          { probe.flagCalls += 1; probe.lastFlagShot = id }
        s.missCandidates   = function ()   { return probe.missRows }
        s.openDetail       = function (id) { probe.detailCalls += 1; probe.lastDetailId = id }
        s.closeDetail      = function ()   { probe.closeDetailCalls += 1 }
        s.setDetailMeasure = function (id) { probe.measureCalls += 1; probe.lastMeasureId = id }
        return s
    }

    // ═══════════════════════════ hand-written fixtures ═══════════════════════════

    // A tick carries its shot ID (ids, not positions, are what the brush and the carousel share).
    function tick(state, i, sel) { return { state: state, shotId: i + 1, selected: sel === true } }
    function ticksOf(states, sel) {
        const out = []
        for (let i = 0; i < states.length; ++i) out.push(tick(states[i], i, i === sel))
        return out
    }
    // A condition on a shot readout, as shotReadout() publishes it. strengthExcess is the dot plot's x:
    // 0 the middle of the corridor, 2 the pass edge (where a condition fires), 3 the fault line.
    function cond(id, name, kind, val, band, tier, excess, ticks) {
        return { id: id, name: name, stateKind: kind,
                 state: kind === "fired" ? "OUT" : kind === "clean" ? "IN" : "—",
                 valueText: val, corridorText: band, reason: kind === "notAssessable" ? band : "",
                 strengthKnown: kind !== "notAssessable", strength: kind === "fired" ? 5 : 1,
                 strengthExcess: excess, strengthText: "", tierTag: tier, tail: false,
                 recurrence: "", ticks: ticks || [], firingsAfter: 0,
                 firingsAfterText: "no firings after this shot" }
    }

    readonly property string coverage: "23 of 140 characteristics measurable with current capture"

    // The two pattern cards. Card 0's run holds all three cell states, including the one this file is
    // here to check (the not-measured shot 2).
    function patternCards() {
        return [
            { id: "casting", name: "Casting", consequence: "", tier: "pattern", phase: "downswing",
              rankText: "#1", reachesBall: true, rootHere: true,
              recurrence: "4 of 5 measurable shots", fired: 4, assessable: 5,
              fresh: true, resolving: false, focused: false, thisShot: "fired", statePill: "FIRED",
              valueText: "15% hand rise", corridorText: "pass up to 8.7 · fault at 13% hand rise",
              strengthKnown: true, strength: 4, strengthExcess: 2.2,
              strengthText: "4 of 5 · well outside the corridor · 2.2× the distance to the corridor edge",
              directionClaimed: true,
              directionText: "Consistent direction: the high side on 92% of its firings.",
              trend: "worsening", trendKnown: true, trendArrow: "↑", trendText: "worsening",
              recencyText: "fired on the last measurable shot", firingsAfterText: "",
              evidence: "Casting on 4 of the 5 swings where rushed transition fired — on 0 of the 1 where it did not.",
              ticks: ticksOf(["fired", "notAssessable", "fired", "clean", "fired"]) },
            { id: "face_roll", name: "Face roll through impact", consequence: "", tier: "pattern", phase: "impact",
              rankText: "#2", reachesBall: false, rootHere: false,
              recurrence: "3 of 5 measurable shots", fired: 3, assessable: 5,
              fresh: false, resolving: false, focused: false, thisShot: "clean", statePill: "CLEAN",
              valueText: "2.1°", corridorText: "pass 0.0 to 4.0°",
              strengthKnown: true, strength: 1, strengthExcess: 0.6,
              strengthText: "1 of 5 · inside the corridor · 0.6× the distance to the corridor edge",
              directionClaimed: false,
              directionText: "Direction agreement 56%, below the 70% gate: dispersion, not a direction.",
              trend: "stable", trendKnown: false, trendArrow: "", trendText: "trend after 6 measurable shots",
              recencyText: "last fired 1 measurable shots ago", firingsAfterText: "",
              evidence: "The face arrives at a different angle each time, which is why the miss has two sides.",
              ticks: ticksOf(["clean", "fired", "fired", "fired", "clean"]) }
        ]
    }

    function coldSource(expectations) {
        return {
            stage: "cold", closed: false, shotCount: 2, patternCount: 0,
            focusConditionId: "",
            headerInfo: {
                stage: "cold", stageLabel: "COLD", shotLabel: "shot 2", shotCount: 2, patternCount: 0,
                reachedEstablished: false, reviewBadge: "", reviewNote: "",
                countLine: "0 patterns · counted over all 2 shots",
                cadenceNote: "", coldLine: "Too few swings to call a pattern.",
                formingLine: "", closingLine: ""
            },
            thisShot: [{ kind: "clean", name: "clean on every measurable condition" }],
            quiet: false,
            afterShotDelta: { headline: "0 of 7 conditions fired on this swing · 0 of them patterns", note: "", surfaced: true },
            cards: [], watching: [], bookends: [], chains: [],
            expectations: expectations,
            unchainedLine: "",
            coverageLine: probe.coverage,
            detailConditionId: "", detail: null
        }
    }

    readonly property var expectationRows: [
        { id: "casting", name: "Casting",
          text: "pattern in 4 of your last 6 sessions", caveat: "an expectation to test, not a finding" },
        { id: "early_extension", name: "Early extension",
          text: "pattern in 3 of your last 6 sessions", caveat: "an expectation to test, not a finding" },
        { id: "over_the_top", name: "Over the top",
          text: "pattern in 2 of your last 6 sessions", caveat: "an expectation to test, not a finding" }
    ]

    // Two patterns, five shots, live. The newest swing (id 5, index 4) is published as the after-shot
    // delta, so with nothing picked This shot reads it (D3): casting (a pattern) and sway (watched) fired.
    function formingSource(quiet) {
        return {
            stage: "forming", closed: false, shotCount: 5, patternCount: 2,
            focusConditionId: "",
            headerInfo: {
                stage: "forming", stageLabel: "FORMING", shotLabel: "shot 5", shotCount: 5, patternCount: 2,
                recordedStage: "forming", reachedEstablished: false, reviewBadge: "", reviewNote: "",
                countLine: "2 patterns · counted over all 5 shots",
                cadenceNote: quiet ? "BANDWIDTH · QUIET" : "",
                coldLine: "",
                formingLine: "No chain is drawn: the model authors no edge between these patterns.",
                closingLine: ""
            },
            thisShot: [
                { id: "casting", name: "Casting", kind: "fired", tier: "pattern", focus: false,
                  strengthKnown: true, strength: 4, strengthExcess: 2.2,
                  strengthText: "4 of 5 · well outside the corridor · 2.2× the distance to the corridor edge" },
                { id: "sway", name: "Lateral sway", kind: "fired", tier: "watching", focus: false,
                  strengthKnown: true, strength: 2, strengthExcess: 2.05,
                  strengthText: "2 of 5 · at the corridor edge · 1.1× the distance to the corridor edge" },
                { kind: "notAssessable", name: "2 not assessable on this capture" }
            ],
            quiet: quiet,
            afterShotDelta: { shotId: 5, shotIndex: 4, firedCount: 2, cleanCount: 1, notAssessableCount: 2,
                              firedPatternCount: 1, fired: ["casting", "sway"], focusId: "", newPatterns: [],
                              streakBroken: false,
                              headline: "2 of 3 conditions fired on this swing · 1 of them pattern",
                              note: "2 measures not assessable on this capture", surfaced: !quiet },
            cards: patternCards(),
            watching: [
                { id: "sway", name: "Lateral sway", recurrence: "1 of 5 measurable shots" },
                { id: "flat_shoulder", name: "Flat shoulder plane", recurrence: "1 of 4 measurable shots" }
            ],
            bookends: [], expectations: [], chains: [],
            unchainedLine: "Face roll through impact · no authored edge to any other pattern this session",
            coverageLine: probe.coverage,
            detailConditionId: "", detail: null
        }
    }
    // The newest swing, PICKED (the readout of shot 5 of 5): the same facts as the delta, every condition.
    function newestReadout() {
        return { shotId: 5, shotIndex: 4, shotCount: 5, club: "7i",
                 firedCount: 2, cleanCount: 1, notAssessableCount: 1, measurableSetCount: 3,
                 headline: "2 of 3 conditions fired on this swing · 1 of them pattern", note: "",
                 conditions: [
                     cond("casting", "Casting", "fired", "15% hand rise", "pass up to 8.7 · fault at 13% hand rise", "pattern", 2.2,
                          ticksOf(["fired", "notAssessable", "fired", "clean", "fired"], 4)),
                     cond("sway", "Lateral sway", "fired", "84 mm", "pass 20 to 55 mm", "watching", 2.05,
                          ticksOf(["clean", "clean", "clean", "clean", "fired"], 4)),
                     cond("face_roll", "Face roll through impact", "clean", "2.1°", "pass 0.0 to 4.0°", "pattern", 0.6,
                          ticksOf(["clean", "fired", "fired", "fired", "clean"], 4)),
                     cond("x_factor", "X-factor", "notAssessable", "not measurable", "ball not tracked", "watching", 0, [])
                 ] }
    }

    // ── the detail rails (the model's marshallers' shapes) ──
    function chainNode(id, name, kind, mark, extra) {
        const n = { id: id, name: name, kind: kind, mark: mark, measure: "", phase: "", focused: false,
                    recurrence: "", ticks: [], trend: "stable", trendArrow: "",
                    state: "notAssessable", statePill: "NOT MEASURED", evidence: "" }
        for (const k in extra) n[k] = extra[k]
        return n
    }
    function liveNode(id, name, phase, measure, recurrence, state, evidence) {
        return chainNode(id, name, "live", "", { phase: phase, measure: measure, recurrence: recurrence,
                                                 state: state, statePill: state === "fired" ? "FIRED" : "CLEAN",
                                                 evidence: evidence, strengthKnown: true,
                                                 ticks: ticksOf(["fired", "notAssessable", "fired", "clean", "fired", "fired"]) })
    }
    function chainLink(from, to, grade, word, stroke, width, arrow, opacity, note) {
        return { from: from, to: to, grade: grade, word: word, stroke: stroke,
                 width: width, arrow: arrow, opacity: opacity, note: note, pairs: 12 }
    }
    function detailRail(nodes, links, primary, liveCount) {
        const end = nodes[nodes.length - 1]
        return { nodes: nodes, links: links, primary: primary === true, liveCount: liveCount, anyLive: liveCount > 0,
                 endId: end.id, endName: end.name, endKind: end.kind, endRecurrence: end.recurrence || "",
                 endLmAnchored: end.lmAnchored === true }
    }

    function establishedSource(eligible) {
        const driver = eligible
            ? { eligible: true, rootId: "rushed_transition", rootName: "Rushed transition", coverage: 3,
                whyText: "Rushed transition — would explain 3 of your patterns",
                explains: [{ id: "casting", name: "Casting" }],
                screenConditionId: "pelvic_disassociation",
                screenRef: "screen.pelvic_disassociation",
                screenLabel: "Seated trunk rotation",
                screenCta: "Seated trunk rotation would anchor this chain",
                screenReason: "it would explain 4 of your patterns",
                screenProtocol: "Sit, cross the arms, rotate.",
                rival: null, offered: [] }
            : { eligible: false, waitingText: "the driver appears once the pattern set has held still for 3 shots" }
        return {
            stage: "established", closed: false, shotCount: 11, patternCount: 2,
            focusConditionId: "",
            headerInfo: {
                stage: "established", stageLabel: "ESTABLISHED", shotLabel: "shot 11", shotCount: 11, patternCount: 2,
                recordedStage: "established", reachedEstablished: true, reviewBadge: "", reviewNote: "",
                countLine: "2 patterns · counted over all 11 shots",
                cadenceNote: "", coldLine: "", formingLine: "", closingLine: ""
            },
            thisShot: [
                { id: "casting", name: "Casting", kind: "fired", tier: "pattern", focus: false,
                  strengthKnown: true, strength: 4, strengthExcess: 2.2, strengthText: "" },
                { kind: "notAssessable", name: "2 not assessable on this capture" }
            ],
            quiet: false,
            // No shot id: the model has published no newest swing to read (This shot says "No shot yet").
            afterShotDelta: { headline: "", note: "", surfaced: true },
            cards: patternCards(), expectations: [], bookends: [], chains: [],
            unchained: [{ id: "face_roll", name: "Face roll through impact", recurrence: "3 of 11 measurable shots" }],
            unchainedLine: "Face roll through impact is a pattern the model authors no edge for this session — reported on its own.",
            driver: driver,
            watching: [{ id: "sway", name: "Lateral sway", recurrence: "1 of 11 measurable shots" }],
            coverageLine: probe.coverage,
            detailConditionId: "", detail: null
        }
    }

    // SessionDiagnosticsModel::detail() for `casting`. Two cause paths and two effect paths, and between
    // them ALL FIVE link grades — the strong two (moved together, conditionally dependent) and the weak
    // three (coherent, present together, unanchored) — and every node kind: live, ghost, screened root,
    // outcome.
    function detailPayload() {
        const castingCard = {
            id: "casting", name: "Casting", consequence: "", tier: "pattern", phase: "downswing",
            measure: "wrist release rate", kind: "live", mark: "", kindText: "", rankText: "#1",
            recurrence: "4 of 6 measurable shots", fired: 4, assessable: 6,
            fresh: false, resolving: false, focused: false,
            thisShot: "fired", statePill: "FIRED", valueText: "15% hand rise",
            directionClaimed: true, directionText: "The high side on 4 of its 4 firings.",
            trend: "worsening", trendKnown: true, trendArrow: "↑", trendText: "worsening",
            recencyText: "fired on the last measurable shot", firingsAfterText: "",
            evidence: "Casting on 4 of the 5 swings where rushed transition fired — on 0 of the 1 where it did not.",
            ticks: ticksOf(["fired", "notAssessable", "fired", "clean", "fired", "fired"])
        }
        const rushed = liveNode("rushed_transition", "Rushed transition", "transition", "transition tempo",
                                "5 of 6 measurable shots", "fired", "Rushed transition on 5 of the 6 swings measured.")
        const casting = liveNode("casting", "Casting", "downswing", "wrist release rate",
                                 "4 of 6 measurable shots", "fired", castingCard.evidence)
        const shaftLean = liveNode("shaft_lean", "Not enough shaft lean", "impact", "shaft lean at impact",
                                   "4 of 6 measurable shots", "clean", "Shaft lean sits behind the ball on 4 of 6.")
        const scooping = liveNode("scooping", "Scooping", "impact", "lead wrist extension",
                                  "3 of 6 measurable shots", "fired", "Scooping on 3 of the 6 swings measured.")
        const slice = chainNode("slice", "Slice", "outcome", "outcome · launch-monitor verified",
                                { recurrence: "5 of 6 measurable shots", state: "fired", statePill: "FIRED", lmAnchored: true })
        const pelvic = chainNode("pelvic_disassociation", "Poor pelvic disassociation", "screenedRoot",
                                 "screened root · needs a physical screen",
                                 { evidence: "A thirty-second physical test would settle whether this is the root." })
        const overTop = chainNode("over_the_top", "Over the top", "ghost", "ghost · authored as asserted, no measure", {})
        return {
            id: "casting", name: "Casting",
            header: castingCard,
            causeHeadline: "Likely driver: Rushed transition — would explain 3 of your patterns.",
            outcomeHeadline: "Most likely outcome: Slice — 5 of 6 measurable shots.",
            causes: [
                detailRail([rushed, casting],
                           [chainLink("rushed_transition", "casting", "movedTogether", "Moved together",
                                      "solidAccent", 2.0, true, 1.0, "baseline 5 · 6 since focus")], true, 2),
                detailRail([pelvic, overTop, casting],
                           [chainLink("pelvic_disassociation", "over_the_top", "unanchored", "Unanchored",
                                      "dottedFaint", 1.0, false, 0.5, "screen would confirm"),
                            chainLink("over_the_top", "casting", "presentTogether", "Present together",
                                      "dotted", 1.0, false, 1.0, "neither measured")], false, 1)
            ],
            effects: [
                detailRail([casting, shaftLean, scooping, slice],
                           [chainLink("casting", "shaft_lean", "conditionallyDependent", "Conditionally dependent",
                                      "solid", 1.5, true, 1.0, "Fisher exact · 12 paired shots"),
                            chainLink("shaft_lean", "scooping", "coherent", "Coherent",
                                      "dashed", 1.5, false, 1.0, "no variance to test"),
                            chainLink("scooping", "slice", "conditionallyDependent", "Conditionally dependent",
                                      "solid", 1.5, true, 1.0, "Fisher exact · 6 paired shots")], true, 3),
                detailRail([casting, scooping],
                           [chainLink("casting", "scooping", "coherent", "Coherent",
                                      "dashed", 1.5, false, 1.0, "no variance to test")], false, 2)
            ],
            causesCapped: false, effectsCapped: false,
            rivals: [{ childId: "casting", childName: "Casting",
                       chosenParentId: "rushed_transition", chosenName: "Rushed transition",
                       rivalParentId: "grip_strength", rivalName: "Grip too strong", adjudicated: false,
                       text: "Grip too strong could also explain Casting — not adjudicated: it is not measurable on this capture." }],
            noCausesLine: "", noEffectsLine: ""
        }
    }
    // The panel with a detail open on it — the model publishes both, and the body reads the pair.
    function detailSource() {
        const s = spied(establishedSource(true))
        s.detailConditionId = "casting"
        s.detail = probe.detailPayload()
        return s
    }

    // The live session ended: bookends published (Closing only), with the shot each one names.
    function closingSource() {
        const s = formingSource(false)
        s.stage = "closing"
        s.closed = true
        s.headerInfo.stage = "closing"
        s.headerInfo.stageLabel = "CLOSING"
        s.headerInfo.closed = true
        s.headerInfo.closingLine = "Every count covers all 6 shots, and this shot is highlighted wherever it appears. At this number of shots a percentage would look more exact than it is, so each pattern says how many shots it came back on."
        s.headerInfo.closingLineNoShot = "Every count covers all 6 shots. At this number of shots a percentage would look more exact than it is, so each pattern says how many shots it came back on."
        s.bookends = [
            { id: "casting", name: "Casting", representativeShotId: 4, worstShotId: 3, bestShotId: 5,
              worstText: "worst 3", bestText: "best 5", representativeText: "shot 4 · most representative" },
            { id: "face_roll", name: "Face roll through impact", representativeShotId: 2, worstShotId: 1, bestShotId: 5,
              worstText: "worst 1", bestText: "best 5", representativeText: "shot 2 · most representative" }
        ]
        return s
    }

    // ── review: a finished 14-shot session, reviewed at shot 9 ──
    function reviewSource() {
        const s = establishedSource(true)
        s.stage = "closing"
        s.closed = true
        s.shotCount = 14
        s.headerInfo.stage = "closing"
        s.headerInfo.stageLabel = "CLOSING"
        s.headerInfo.shotCount = 14
        s.headerInfo.shotLabel = "shot 9 of 14"
        s.headerInfo.countLine = "2 patterns · counted over all 14 shots"
        s.headerInfo.closingLine = "Every count covers all 14 shots, and this shot is highlighted wherever it appears. At this number of shots a percentage would look more exact than it is, so each pattern says how many shots it came back on."
        s.headerInfo.closingLineNoShot = "Every count covers all 14 shots. At this number of shots a percentage would look more exact than it is, so each pattern says how many shots it came back on."
        s.headerInfo.reviewBadge = "REVIEWING · shot 9 of 14"
        s.headerInfo.reviewNote  = "final session state · this shot read inside the finished ledger"
        // The review tense: what happened HERE and what happened AFTER, off one row, and the run 14 long.
        s.cards[0].thisShot = "fired"
        s.cards[0].firingsAfterText = "3 more firings after this shot"
        s.cards[0].ticks = ticksOf(["fired", "notAssessable", "fired", "clean", "fired", "fired", "clean",
                                    "fired", "fired", "clean", "fired", "clean", "fired", "fired"], 8)
        s.cards[1].thisShot = "clean"
        s.cards[1].firingsAfterText = "no firings after this shot"
        s.cards[1].ticks = ticksOf(["clean", "fired", "fired", "fired", "clean", "clean", "clean",
                                    "fired", "clean", "clean", "clean", "fired", "clean", "clean"], 8)
        s.bookends = [
            { id: "casting", name: "Casting", representativeShotId: 7, worstShotId: 3, bestShotId: 11,
              worstText: "worst 3", bestText: "best 11", representativeText: "shot 7 · most representative" }
        ]
        return s
    }
    // The reviewed shot: five fired, two clean and two the capture could not answer — which are the ones
    // this fixture exists for. The ids are not the ordinals: shot 9 of 14 is swing id 23 here, because
    // swings were deleted (D7: a label is the ORDINAL).
    function reviewReadout() {
        return {
            shotId: 23, shotIndex: 8, shotCount: 14, club: "7i",
            firedCount: 5, cleanCount: 2, notAssessableCount: 2, measurableSetCount: 7,
            headline: "5 of 7 conditions fired on this swing · 4 of them patterns",
            note: "2 measures not assessable on this capture",
            tailCount: 0, tailSummary: "",
            conditions: [
                // The true minus (U+2212) is the model's; nothing in the QML reformats it.
                cond("casting", "Casting", "fired", "−4.4°", "pass −2.0 to +2.0°", "pattern", 3.4),
                cond("rushed_transition", "Rushed transition", "fired", "18 ms", "pass 30 to 70 ms", "pattern", 2.9),
                cond("scooping", "Scooping", "fired", "12.4°", "pass 0.0 to 8.0°", "pattern", 2.6),
                cond("path_out_to_in", "Path too far out-to-in", "fired", "−5.1°", "pass −2.0 to +2.0°", "pattern", 2.4),
                cond("sway", "Lateral sway", "fired", "84 mm", "pass 20 to 55 mm", "watching", 2.2),
                cond("shaft_lean", "Not enough shaft lean", "clean", "+6.2°", "pass +4.0 to +12.0°", "watching", 1.6),
                cond("face_roll", "Face roll through impact", "clean", "2.1°", "pass 0.0 to 4.0°", "pattern", 0.6),
                cond("x_factor", "X-factor", "notAssessable", "not measurable", "ball not tracked", "watching", 0),
                cond("head_height", "Head height Δ", "notAssessable", "not measurable", "shaft occluded at P6", "clean all session", 0)
            ]
        }
    }

    // A six-shot session that recorded FORMING (two patterns, no authored edge), closed and reviewed at
    // shot 6: the flat rows with their full runs are the body, and the reviewed shot is one picked cell.
    function formingReviewSource() {
        const s = formingSource(false)
        s.stage = "closing"
        s.closed = true
        s.shotCount = 6
        s.headerInfo.stage = "closing"
        s.headerInfo.stageLabel = "CLOSING"
        s.headerInfo.recordedStage = "forming"
        s.headerInfo.reachedEstablished = false
        s.headerInfo.shotCount = 6
        s.headerInfo.shotLabel = "shot 6 of 6"
        s.headerInfo.countLine = "2 patterns · counted over all 6 shots"
        s.headerInfo.reviewBadge = "REVIEWING · shot 6 of 6"
        s.headerInfo.reviewNote  = "final session state · this shot read inside the finished ledger"
        s.cards[0].name = "Too much shaft lean at impact"
        s.cards[0].recurrence = "6 of 6 measurable shots"
        s.cards[0].thisShot = "fired"
        s.cards[0].ticks = ticksOf(["fired", "fired", "fired", "fired", "fired", "fired"], 5)
        s.cards[1].name = "Head drifts toward the target going back"
        s.cards[1].recurrence = "5 of 6 measurable shots"
        s.cards[1].thisShot = "fired"
        s.cards[1].ticks = ticksOf(["fired", "clean", "fired", "fired", "fired", "fired"], 5)
        s.driver = { eligible: false, final: true, finalText: "No driver: this session's patterns share no authored cause." }
        return s
    }
    function sixShotReadout() {
        return {
            shotId: 6, shotIndex: 5, shotCount: 6, club: "Driver",
            firedCount: 3, cleanCount: 1, notAssessableCount: 1, measurableSetCount: 4,
            headline: "3 of 4 conditions fired on this swing · 2 of them patterns", note: "",
            conditions: [
                cond("casting", "Too much shaft lean at impact", "fired", "40.0°", "pass −4.0 to 6.0°", "pattern", 3.1,
                     ticksOf(["fired", "fired", "fired", "fired", "fired", "fired"], 5)),
                cond("face_roll", "Head drifts toward the target going back", "fired", "−41.6cm", "pass 1.0 to 7.0cm", "pattern", 2.8,
                     ticksOf(["fired", "clean", "fired", "fired", "fired", "fired"], 5)),
                cond("sway", "Lateral sway", "fired", "84 mm", "pass 20 to 55 mm", "watching", 2.3,
                     ticksOf(["clean", "clean", "clean", "clean", "clean", "fired"], 5)),
                cond("flat_shoulder", "Flat shoulder plane", "clean", "31°", "pass 25 to 40°", "watching", 0.9,
                     ticksOf(["clean", "fired", "clean", "notAssessable", "clean", "clean"], 5)),
                cond("x_factor", "X-factor", "notAssessable", "not measurable", "ball not tracked", "watching", 0, [])
            ]
        }
    }

    // ── how far: a spread in the shape SessionDiagnosticsModel::spreadFor publishes ──
    // A ceiling, the fault line at 13 %. Five shots: the second not measured, the fourth the one read,
    // the fifth off the scale.
    function spreadFixture() {
        const run = []
        const states = ["fired", "notAssessable", "clean", "fired", "fired"]
        const fy = [0.45, 0, 0.2, 0.5, 1.0]
        // z: the common scale (SessionDiagnosticsModel::ladderZ) at −2 + 32 fy, rungs 0 → 0, 4.33 → 1, 8.67 → 2,
        // 13 → 3, a ceiling folded high; none for the shot not measured
        const z = [2.861, undefined, 1.016, 3.231, 6.926]
        for (let i = 0; i < 5; ++i) {
            const q = { index: i, shotId: i + 1, swingDir: "/lib/a/s/swing_000" + (i + 1),
                        state: states[i], assessable: i !== 1, fx: (i + 0.5) / 5, fy: fy[i],
                        clipped: i === 4 ? 1 : 0, current: i === 3, selected: i === 3,
                        valueText: i === 1 ? "-" : "13% hand rise",
                        readout: i === 1 ? "shot 2 · - · not measured"
                                         : "shot " + (i + 1) + " · 13% hand rise · inside the pass band · " + states[i] }
            if (z[i] !== undefined) q.z = z[i]
            run.push(q)
        }
        return {
            measure: "Hand path loop, through the transition", unit: "% hand rise",
            axisLo: -2, axisHi: 30, decimals: 0, corridorKnown: true, shape: "ceiling",
            bands: [
                { grade: "open",   lo: -2,    hi: 0,     f0: 0,      f1: 0.0625, edge: "" },
                { grade: "ideal",  lo: 0,     hi: 4.33,  f0: 0.0625, f1: 0.198,  edge: "aim" },
                { grade: "good",   lo: 4.33,  hi: 8.67,  f0: 0.198,  f1: 0.333,  edge: "grid" },
                { grade: "watch",  lo: 8.67,  hi: 13,    f0: 0.333,  f1: 0.469,  edge: "pass" },
                { grade: "action", lo: 13,    hi: 30,    f0: 0.469,  f1: 1,      edge: "fault" }
            ],
            faultLines: [{ value: 13, f: 0.469, text: "fault at 13% hand rise", shortText: "13% hand rise" }],
            muF: 0.0625, muText: "0% hand rise",
            normSource: "heuristic", normTag: "norm · coaching judgement",
            normTagLong: "The corridor is the norm's claim, and the norm is coaching judgement (heuristic), not measured data. It is not fitted to your shots.",
            normCitation: "",
            dots: [
                { index: 0, shotId: 1, swingDir: "/lib/a/s/swing_0001", f: 0.45, clipped: 0, stack: 0, state: "fired", current: false, recent: true, valueText: "14% hand rise" },
                { index: 2, shotId: 3, swingDir: "/lib/a/s/swing_0003", f: 0.2,  clipped: 0, stack: 0, state: "clean", current: false, recent: true, valueText: "4% hand rise" },
                { index: 3, shotId: 4, swingDir: "/lib/a/s/swing_0004", f: 0.5,  clipped: 0, stack: 0, state: "fired", current: true,  recent: true, valueText: "15% hand rise" },
                { index: 4, shotId: 5, swingDir: "/lib/a/s/swing_0005", f: 1.0,  clipped: 1, stack: 0, state: "fired", current: false, recent: true, valueText: "200% hand rise" }
            ],
            stackMax: 0, clipHiCount: 1, clipHiText: "200% hand rise", clipLoCount: 0, clipLoText: "",
            // the model's box (SessionDiagnosticsModel boxOf, from typeset/box_stats.h): f 0.2 0.45 0.5 1.0 →
            // Q1 0.3875, median 0.475, Q3 0.625; no upper whisker (0.5 sits inside the box, 1.0 is past the fence)
            box: { n: 4, fLo: 0.2, fQ1: 0.3875, fMed: 0.475, fQ3: 0.625, fHi: 0.625, outliers: [5], outlierCount: 1,
                   texts: { lo: "4", q1: "10", med: "13", q3: "18", hi: "18" } },
            run: run,
            median: [{ index: 2, fx: 0.5, fy: 0.42, value: 13, z: 2.64 }, { index: 3, fx: 0.7, fy: 0.45, value: 14, z: 2.861 },
                     { index: 4, fx: 0.9, fy: 0.47, value: 14, z: 3.009 }],
            medianWindow: 5, notAssessable: 1, pastFault: 3, placed: 4,
            medianText: "14% hand rise",
            caption: "median 14% hand rise · 3 past the fault line · 3 of 4 outside · 1 not measured · 1 off scale, at 200% hand rise",
            captionShort: "median 14% hand rise · 3 past the fault line · 1 not measured · 1 off scale, at 200% hand rise",
            currentReadout: "shot 4 · 13% hand rise · inside the pass band · fired",
            yTicks: [{ value: 0, f: 0.0625, text: "0" }, { value: 10, f: 0.375, text: "10" },
                     { value: 20, f: 0.6875, text: "20" }],
            medianNumber: "14", currentNumber: "15", currentState: "fired",
            dirLeft: "← better", dirCentre: "", dirRight: "worse →",
            highMeans: "the hands came down further outside the path they went up on"
        }
    }
    function historyFixture() {
        const sp = spreadFixture()
        function txt(f) { return String(Math.round(-2 + 32 * f)) }   // the axis −2…30, whole units
        function col(label, cur, fs, med) {
            return { label: label, current: cur, n: fs.length, sessionDir: "/lib/a/" + label,
                     values: fs.map(function (f, i) { return { f: f, clipped: 0, stack: i % 3, value: 0 } }),
                     stackMax: 2, fQ1: med - 0.05, fQ3: med + 0.05, fMedian: med, median: 0,
                     box: fs.length ? { n: fs.length, fLo: med - 0.05, fQ1: med - 0.05, fMed: med, fQ3: med + 0.05, fHi: med + 0.05,
                                        outliers: [], outlierCount: 0,
                                        texts: { lo: txt(med - 0.05), q1: txt(med - 0.05), med: txt(med), q3: txt(med + 0.05), hi: txt(med + 0.05) } }
                                    : { n: 0 },
                     medianText: label === "4 Jul" ? "22" : (cur ? "13" : "19") }
        }
        return { axisLo: sp.axisLo, axisHi: sp.axisHi, bands: sp.bands, faultLines: sp.faultLines,
                 columns: [col("4 Jul", false, [0.7, 0.75, 0.8], 0.75),
                           col("5 Oct", false, [0.6, 0.66, 0.7], 0.66),
                           col("7 Oct", true,  [0.4, 0.45, 0.5], 0.45)],
                 yTicks: sp.yTicks, unit: "% hand rise", measure: sp.measure,
                 caption: "median 22 → 19 → 13% hand rise", loading: false, missing: 0,
                 hiddenEarlier: 0, hiddenLater: 0 }
    }
    // The forming source with card 0 carrying a spread (session median 14, this shot 15).
    function spreadSource() {
        const s = spied(formingSource(false))
        s.cards[0].spread = probe.spreadFixture()
        return s
    }
    // The detail of casting with its spread, its five-shot run and its history across sessions.
    function spreadDetailSource() {
        const s = detailSource()
        s.detail.header.spread = probe.spreadFixture()
        s.detail.header.ticks = ticksOf(["fired", "notAssessable", "clean", "fired", "fired"])
        s.detail.history = probe.historyFixture()
        return s
    }
    // WHAT CHANGED (posture_metrics_design.md §8): a composite's parts, in the shape
    // SessionDiagnosticsModel::whatChangedOf publishes — four read (most of it, some, a little, against
    // it) and one not read, whose reason is the trust fold's line and never the table's.
    function whatChangedFixture(mode) {
        const distance = mode === "distance"
        const row = function (part, name, read, start, end, change, value, ideal, grade, word, dom) {
            return { part: part, name: name, measureId: "m_" + part, read: read, unit: "°", unitNote: "",
                     startText: start, endText: end, changeText: distance ? "" : change, valueText: value,
                     idealText: ideal, grade: grade, shareWord: word, dominant: dom, reason: read ? "" : "not measured on this swing" }
        }
        return {
            mode: distance ? "distance" : "coherent", startWord: "address", endWord: distance ? "address" : "impact",
            rows: [ row("trunk", "forward bend lost", true, "34°", "25°", "9°", "34°", "32 to 48°", "action", "most of it", true),
                    row("pelvisDepth", "hips toward the ball", true, "", "", "7 cm", "7 cm", "", "watch", "some", false),
                    row("pelvisHeight", "hips higher", true, "", "", "3°", "3°", "", "good", "a little", false),
                    row("knee", "trail-knee bend lost", true, "", "", "−2°", "−2°", "", "ideal", "against it", false),
                    row("head", "head higher", false, "-", "-", "-", "-", "", "", "-", false) ],
            dominant: { name: "forward bend lost", measureId: "m_trunk", unit: "°", figure: "9", changeText: "9°",
                        valueText: "9°" },
            partsRead: 4, partsTotal: 5,
            coverageText: "Read from 4 of 5 parts: head higher, not measured on this swing.",
            trustLines: [{ kind: "coverage", text: "Read from 4 of 5 parts: head higher, not measured on this swing." }]
        }
    }
    function compositeDetailSource(mode) {
        const s = detailSource()
        s.detail.composite = true
        s.detail.whatChanged = whatChangedFixture(mode)
        s.detail.whatChangedSwingDir = ""
        s.detail.header.valueText = "9°"
        s.detail.header.corridorText = ""
        s.detail.header.spread = { composite: true, currentNumber: "9", medianNumber: "8", medianText: "8°", unit: "°",
                                   measure: "forward bend lost", placed: 5, pastFault: 3, notAssessable: 0,
                                   corridorKnown: false, caption: "forward bend lost, median 8° · 4 of 5 outside" }
        return s
    }

    // The picked shot for the spread detail: shot `idx` of 5 (ids 1-5).
    function spreadReadout(idx) {
        const states = ["fired", "notAssessable", "clean", "fired", "fired", "clean"]
        const n = idx >= 5 ? 6 : 5
        return { shotId: idx + 1, shotIndex: idx, shotCount: n, club: "7i",
                 firedCount: states[idx] === "fired" ? 1 : 0, cleanCount: states[idx] === "clean" ? 1 : 0,
                 notAssessableCount: states[idx] === "notAssessable" ? 1 : 0, measurableSetCount: 1,
                 headline: "", note: "",
                 conditions: [cond("casting", "Casting", states[idx],
                                   states[idx] === "notAssessable" ? "not measurable" : "13% hand rise",
                                   "pass up to 8.7 · fault at 13% hand rise", "pattern",
                                   states[idx] === "fired" ? 2.4 : 0.8)] }
    }
}
