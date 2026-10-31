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
import QtTest
import PinPointStudio
import "data/diag_session.js" as Real

// The session diagnostics panel's body, loaded for real and pressed through its states — the TYPESET
// panel (design system §15; INVENTORY of 11 Oct 2026 §4 is the test map this file was rewritten from).
//
// THE BODY HOSTS A REGISTER. PpSessionDiagnosticsBody keeps its data and state API (source, readout,
// tab, cardFilter, detailOpen, pulseCue, _openDetail … shotRequested) and loads ONE of two registers:
//   · PpDiagReplay  — the glance: Capture, Replay, and the second-screen cast (surface "cast", D4);
//   · PpDiagAnalyse — the read: SessionMode.analyse.
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
//     rows, F33) · 29 (a screened root asks for its screen) · 30 31 (declared miss, inline picker, F66-67)
//     32 33 (the after-shot pulse and reduceMotion, D1)
//   NEW (what the redesign promises): 50-61, see each function.
//   OBSOLETE, deleted — each with its reason:
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
    width: 1168; height: 560

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
        id: pipProbe
        width: Theme.sp(127); height: Theme.sp(14)
        pips: probe.pipFixture
        firedCount: probe.pipFired
    }
    property var pipFixture: []
    property int pipFired: 0

    PpSessionDiagnosticsBody {
        id: body
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
    // declareFocus / clearFocus / declareMiss / missCandidates / openDetail / closeDetail /
    // setDetailMeasure are INVOKABLES on the model, reached through `source`. A fixture supplies spies,
    // so the test asserts what the panel ASKED FOR. The render is asserted against fixtures that already
    // carry the model's answer (a plain JS object's properties are not bindable: a spy that mutated the
    // fixture would change nothing on screen).
    property int    focusCalls: 0
    property string lastFocusId: ""
    property int    clearFocusCalls: 0
    property int    missCalls: 0
    property string lastMissId: "\u0000"      // distinguishable from declareMiss("")
    property int    detailCalls: 0
    property string lastDetailId: ""
    property int    closeDetailCalls: 0
    property int    measureCalls: 0
    property string lastMeasureId: ""

    // A stand-in for SessionController's focus half: the panel hands the body the real one only while
    // the session is live (body.focusController), and the body writes a declaration back to it.
    QtObject {
        id: fakeController
        property string focusConditionId: ""
        property int    focusFromShot: 0
        property int    idWrites: 0
        property int    splitWrites: 0
        onFocusConditionIdChanged: idWrites += 1
        onFocusFromShotChanged:    splitWrites += 1
    }

    readonly property var missRows: [
        { id: "slice",       name: "Slice" },
        { id: "hook",        name: "Hook" },
        { id: "thin_strike", name: "Thin strike" }
    ]

    function spied(s) {
        if (!s) return s
        s.declareFocus     = function (id) { probe.focusCalls += 1; probe.lastFocusId = id }
        s.clearFocus       = function ()   { probe.clearFocusCalls += 1 }
        s.declareMiss      = function (id) { probe.missCalls += 1; probe.lastMissId = id }
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
              valueText: "15 % hand rise", corridorText: "pass up to 8.7 · fault at 13 % hand rise",
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
            focusConditionId: "", declaredMiss: "", declaredMissName: "",
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
            focusConditionId: "", declaredMiss: "", declaredMissName: "",
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
                     cond("casting", "Casting", "fired", "15 % hand rise", "pass up to 8.7 · fault at 13 % hand rise", "pattern", 2.2,
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
            focusConditionId: "", declaredMiss: "", declaredMissName: "",
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
            thisShot: "fired", statePill: "FIRED", valueText: "15 % hand rise",
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
        const slice = chainNode("slice", "Slice", "outcome", "declared miss · launch-monitor verified",
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
        for (let i = 0; i < 5; ++i)
            run.push({ index: i, shotId: i + 1, swingDir: "/lib/a/s/swing_000" + (i + 1),
                       state: states[i], assessable: i !== 1, fx: (i + 0.5) / 5, fy: fy[i],
                       clipped: i === 4 ? 1 : 0, current: i === 3, selected: i === 3,
                       valueText: i === 1 ? "-" : "13 % hand rise",
                       readout: i === 1 ? "shot 2 · - · not measured"
                                        : "shot " + (i + 1) + " · 13 % hand rise · inside the pass band · " + states[i] })
        return {
            measure: "Hand path loop, through the transition", unit: "% hand rise",
            axisLo: -2, axisHi: 30, decimals: 0, corridorKnown: true, shape: "ceiling",
            bands: [
                { grade: "open",   lo: -2,    hi: 0,     f0: 0,      f1: 0.0625 },
                { grade: "ideal",  lo: 0,     hi: 4.33,  f0: 0.0625, f1: 0.198 },
                { grade: "good",   lo: 4.33,  hi: 8.67,  f0: 0.198,  f1: 0.333 },
                { grade: "watch",  lo: 8.67,  hi: 13,    f0: 0.333,  f1: 0.469 },
                { grade: "action", lo: 13,    hi: 30,    f0: 0.469,  f1: 1 }
            ],
            faultLines: [{ value: 13, f: 0.469, text: "fault at 13 % hand rise", shortText: "13 % hand rise" }],
            muF: 0.0625, muText: "0 % hand rise",
            normSource: "heuristic", normTag: "norm · coaching judgement",
            normTagLong: "The corridor is the norm's claim, and the norm is coaching judgement (heuristic), not measured data. It is not fitted to your shots.",
            normCitation: "",
            dots: [
                { index: 0, shotId: 1, swingDir: "/lib/a/s/swing_0001", f: 0.45, clipped: 0, stack: 0, state: "fired", current: false, recent: true, valueText: "14 % hand rise" },
                { index: 2, shotId: 3, swingDir: "/lib/a/s/swing_0003", f: 0.2,  clipped: 0, stack: 0, state: "clean", current: false, recent: true, valueText: "4 % hand rise" },
                { index: 3, shotId: 4, swingDir: "/lib/a/s/swing_0004", f: 0.5,  clipped: 0, stack: 0, state: "fired", current: true,  recent: true, valueText: "15 % hand rise" },
                { index: 4, shotId: 5, swingDir: "/lib/a/s/swing_0005", f: 1.0,  clipped: 1, stack: 0, state: "fired", current: false, recent: true, valueText: "200 % hand rise" }
            ],
            stackMax: 0, clipHiCount: 1, clipHiText: "200 % hand rise", clipLoCount: 0, clipLoText: "",
            // the model's box (SessionDiagnosticsModel boxOf, from typeset/box_stats.h): f 0.2 0.45 0.5 1.0 →
            // Q1 0.3875, median 0.475, Q3 0.625; no upper whisker (0.5 sits inside the box, 1.0 is past the fence)
            box: { n: 4, fLo: 0.2, fQ1: 0.3875, fMed: 0.475, fQ3: 0.625, fHi: 0.625, outliers: [5], outlierCount: 1,
                   texts: { lo: "4", q1: "10", med: "13", q3: "18", hi: "18" } },
            run: run,
            median: [{ index: 2, fx: 0.5, fy: 0.42, value: 13 }, { index: 3, fx: 0.7, fy: 0.45, value: 14 },
                     { index: 4, fx: 0.9, fy: 0.47, value: 14 }],
            medianWindow: 5, notAssessable: 1, pastFault: 3, placed: 4,
            medianText: "14 % hand rise",
            caption: "median 14 % hand rise · 3 past the fault line · 3 of 4 outside · 1 not measured · 1 off scale, at 200 % hand rise",
            captionShort: "median 14 % hand rise · 3 past the fault line · 1 not measured · 1 off scale, at 200 % hand rise",
            currentReadout: "shot 4 · 13 % hand rise · inside the pass band · fired",
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
                 caption: "median 22 → 19 → 13 % hand rise", loading: false, missing: 0,
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
                                   states[idx] === "notAssessable" ? "not measurable" : "13 % hand rise",
                                   "pass up to 8.7 · fault at 13 % hand rise", "pattern",
                                   states[idx] === "fired" ? 2.4 : 0.8)] }
    }

    TestCase {
        id: tc
        name: "SessionDiagnostics"
        when: windowShown

        // ── tree walking ─────────────────────────────────────────────────────
        function findAll(item, name, out) {
            out = out || []
            if (!item)
                return out
            const kids = item.children
            for (let i = 0; i < kids.length; ++i) {
                if (kids[i].objectName === name)
                    out.push(kids[i])
                findAll(kids[i], name, out)
            }
            return out
        }
        function visibleAll(item, name) { return findAll(item, name).filter(function (i) { return i.visible }) }
        // `visible` is an item's own flag and says nothing about its ancestors: a view behind another
        // view is hidden by ONE invisible parent and everything in it is still visible: true.
        function shownAll(item, name) { return findAll(item, name).filter(function (i) { return shown(i) }) }
        function one(item, name) {
            const all = findAll(item, name)
            verify(all.length >= 1, "expected a '" + name + "', found none")
            return all[0]
        }
        function oneShown(item, name) {
            const all = shownAll(item, name)
            verify(all.length >= 1, "expected a '" + name + "' on screen, found none (" + findAll(item, name).length + " hidden)")
            return all[0]
        }
        // An item is only really on screen if every ancestor up to the body is too.
        function shown(item) {
            let i = item
            while (i && i !== body) {
                if (!i.visible)
                    return false
                i = i.parent
            }
            return !!i
        }

        // ── the register and its views ───────────────────────────────────────
        function reg() { return one(body, "sdRegister") }
        // Which register the body loads is SessionMode's (or the cast's) choice, never the test's: the
        // test sets the mode the app would be in, and checks the body followed it.
        function useRegister(name) {
            SessionMode.mode = name === "Analyse" ? SessionMode.analyse : SessionMode.replay
            laidOut()
            compare(reg().register, name, "the body loaded the " + name + " register")
        }
        readonly property var viewNames: ({ shot: "sdShotView", session: "sdSessionView",
                                            watching: "sdWatchingView", detail: "sdDetailBody" })
        function showTab(key) { body.tab = key; laidOut() }
        function viewOf(key) { return oneShown(body, viewNames[key]) }
        function tabByKey(key) {
            const ts = shownAll(body, "sdTab").filter(function (t) { return t.key === key })
            verify(ts.length === 1, "one '" + key + "' tab on screen")
            return ts[0]
        }
        function tabCount(key) { return one(tabByKey(key), "sdTabCount") }
        // Replay's session view draws a sparkline row per pattern, or its small multiples when they fit.
        function replaySessionItems(v) { return shownAll(v, "sdSparkRow").concat(shownAll(v, "sdSmallMultiple")) }

        // ── sources ──────────────────────────────────────────────────────────
        function laidOut() {
            // A Row/Column positions on POLISH, not on the binding that sized it: read geometry, or
            // press anything, only after a frame has gone through.
            waitForRendering(body)
            wait(0)
        }
        function setSource(s) {
            probe.src = null
            wait(0)
            probe.src = s
            laidOut()
        }
        // Review is TWO inputs — the surface and the selected shot's readout.
        function setReview(s, r) {
            probe.readout = null
            setSource(s)
            probe.readout = r
            laidOut()
        }
        // A real-session state ({ source, readout }), with the declarations spied.
        function applyState(st) { setReview(st.source ? spied(st.source) : null, st.readout) }
        // A republish: the source REPLACED, never nulled in between, exactly as the model's arrives.
        function republish(s, r) {
            probe.src = s
            if (r !== undefined)
                probe.readout = r
            laidOut()
        }

        // ── text ─────────────────────────────────────────────────────────────
        function isText(i) { return !!i && i.elide !== undefined && i.truncated !== undefined && typeof i.text === "string" }
        function textsIn(item, out) {
            out = out || []
            if (!item) return out
            const kids = item.children
            for (let i = 0; i < kids.length; ++i) {
                if (isText(kids[i])) out.push(kids[i])
                textsIn(kids[i], out)
            }
            return out
        }
        function plain(s) {
            return String(s === undefined || s === null ? "" : s).replace(/<[^>]*>/g, "").replace(/&nbsp;/g, " ")
                       .replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/\s+/g, " ").trim()
        }
        function shownTexts(item) { return textsIn(item).filter(function (t) { return shown(t) && t.text !== "" }) }
        function allText(item) { return shownTexts(item).map(function (t) { return plain(t.text) }).join(" | ") }
        // A view head: Replay's Head (title, sub) or Analyse's PpStandfirst (kicker, head, standfirst).
        function headText(h) {
            const parts = []
            for (const k of ["title", "sub", "kicker", "head", "standfirst", "text"])
                if (h[k] !== undefined && typeof h[k] === "string") parts.push(plain(h[k]))
            return parts.join(" · ")
        }
        // A ledger cell (or its folded twin): an Item carrying `text`, its Text child drawing it.
        function inkOf(cell) {
            const kids = cell.children
            for (let i = 0; i < kids.length; ++i) if (isText(kids[i])) return kids[i].color
            return undefined
        }

        // ── pointer ──────────────────────────────────────────────────────────
        // A press can only land on what is on screen: scroll the item's view so it is inside it.
        function flickOf(item) {
            let f = item ? item.parent : null
            while (f && f !== body && !(f.contentY !== undefined && f.contentItem !== undefined && /Flick$/.test(f.objectName)))
                f = f.parent
            return f && f !== body ? f : null
        }
        function intoView(item) {
            const f = flickOf(item)
            if (!f) return
            const y = item.mapToItem(f.contentItem, 0, 0).y
            const h = Math.max(item.height, 1)
            if (y < f.contentY || y + h > f.contentY + f.height)
                f.contentY = Math.max(0, Math.min(y - Theme.sp(20), f.contentHeight - f.height))
            laidOut()
        }
        // A ledger row opens its condition from anywhere but its own controls: press it on its name.
        function clickRow(row, cellName) {
            intoView(row)
            const c = one(row, cellName || "sdSessionRowName")
            const p = c.mapToItem(row, Math.min(Theme.sp(6), c.width / 2), Math.min(Theme.sp(6), c.height / 2))
            mouseClick(row, p.x, p.y)
            laidOut()
        }
        function park() { mouseMove(body, 1, 1); laidOut() }
        // Hover a ledger row with the real pointer, over its NAME — where a reader points. (The first run
        // found the rich-text name kept the hover from the row's MouseArea; the row hovers by HoverHandler
        // now.) The brush is the row's hover state (rw.hot).
        function hoverRow(row) {
            intoView(row)
            const br = one(row, "sdShotStrip").brush
            const p = one(row, "sdSessionRowName").mapToItem(row, Theme.sp(4), Theme.sp(4))
            mouseMove(row, p.x, p.y)
            laidOut()
            compare(br.hoveredCondition, row.cardId, "the pointer on the row's name hovers the row")
        }

        function init() {
            probe.width = 1168; probe.height = 560
            Theme.themeIndex = 5          // studio dark
            Theme.fontScale = 1.0
            Theme.reduceMotion = false
            SessionMode.mode = SessionMode.capture
            body.surface = "panel"
            body.forceRegister = ""
            body.interactive = true
            body.watchingExpanded = false
            body.cardFilter = "all"
            body.tab = "session"
            body.pulseCue = null
            probe.readout = null
            probe.src = null
            probe.pipFixture = []
            probe.pipFired = 0
            probe.lastScreenRef = ""
            probe.lastScreenCondition = ""
            probe.screenCalls = 0
            probe.lastShotDir = ""
            probe.lastShotId = -1
            probe.shotCalls = 0
            probe.focusCalls = 0
            probe.lastFocusId = ""
            probe.clearFocusCalls = 0
            probe.missCalls = 0
            probe.lastMissId = "\u0000"
            probe.detailCalls = 0
            probe.lastDetailId = ""
            probe.closeDetailCalls = 0
            probe.measureCalls = 0
            probe.lastMeasureId = ""
            body.focusController = null
            fakeController.focusConditionId = ""
            fakeController.focusFromShot = 0
            fakeController.idWrites = 0
            fakeController.splitWrites = 0
            laidOut()
        }
        function cleanupTestCase() {
            SessionMode.mode = SessionMode.capture
            Theme.themeIndex = 5
            Theme.fontScale = 1.0
            Theme.reduceMotion = false
        }

        // ═══════════════════════════ Cold (S2-S4, F41-F42) ═══════════════════════════

        // NEEDS-NEW-DESIGN → rewritten: Cold's head is the model's coldLine, its count line under it, in
        // BOTH registers' Session view; the stage word stays on the body's frame.
        function test_01_coldSaysTheNIsTooSmall() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setSource(spied(coldSource(probe.expectationRows)))
                showTab("session")
                const v = viewOf("session")
                const cold = oneShown(v, "sdColdBody")
                compare(one(cold, "sdColdHeadline").text, "Too few swings to call a pattern.", r + ": the n is too small")
                compare(one(cold, "sdColdSubline").text, "0 patterns · counted over all 2 shots", r + ": and what it counted over")
                verify(!shown(one(v, "sdSessionHead")), r + ": no pattern head over a session too young to have patterns")
                compare(shownAll(v, "sdSessionRow").length + replaySessionItems(v).length, 0, r + ": and no rows")
                verify(shown(one(body, "sdStageChip")), r + ": the stage word is on the body's frame")
                verify(body.cardAside.toLowerCase().indexOf("cold") >= 0, r + ": and in the stage card's aside")
            }

            // The real model's Cold, no shots yet: the same line, and This shot waits for the next swing.
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                applyState(Real.cold0())
                showTab("session")
                compare(one(oneShown(viewOf("session"), "sdColdBody"), "sdColdHeadline").text,
                        "Too few swings to call a pattern.", r + ": Cold with no shots")
                showTab("shot")
                const empty = oneShown(viewOf("shot"), "sdShotEmpty")
                verify(allText(empty).toLowerCase().indexOf("next swing reads here") >= 0,
                       r + ": live with nothing yet, the next swing reads here: " + allText(empty))
            }
        }

        // The fault profile's expectations: dashed, headed as expectations rather than findings.
        function test_02_coldDrawsTheExpectationsDashed() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setSource(spied(coldSource(probe.expectationRows)))
                showTab("session")
                const v = viewOf("session")
                const ex = oneShown(v, "sdExpectations")
                const cards = shownAll(ex, "sdExpectationCard")
                compare(cards.length, 3, r + ": one dashed row per expectation")
                for (let i = 0; i < cards.length; ++i) {
                    verify(cards[i].width > 0 && cards[i].height > 0, r + ": expectation " + i + " has a size")
                    verify(cards[i].mapToItem(v, cards[i].width, 0).x <= v.width + 1, r + ": and fits the view")
                }
                const words = allText(ex)
                verify(words.indexOf("Usually yours") >= 0, r + ": headed as the golfer's own profile")
                verify(words.indexOf("expectations to test, not findings") >= 0, r + ": and never as a finding")
                verify(words.indexOf("pattern in 4 of your last 6 sessions") >= 0, r + ": with the profile's own sentence")
            }
            // The real session's Cold (two shots) carries its profile the same way.
            useRegister("Analyse")
            applyState(Real.cold2(true))
            showTab("session")
            compare(shownAll(viewOf("session"), "sdExpectationCard").length, 3, "the real Cold, three expectations")
        }

        // The one a screenshot never catches: no profile, so no heading either. An expectations heading
        // over nothing reads as "the model looked and found none", a different claim from "no profile yet".
        function test_03_coldWithNoProfileOmitsTheBlockEntirely() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setSource(spied(coldSource([])))
                showTab("session")
                const v = viewOf("session")
                verify(shown(one(v, "sdColdBody")), r + ": still the Cold body")
                compare(one(v, "sdColdHeadline").text, "Too few swings to call a pattern.")
                verify(!shown(one(v, "sdExpectations")), r + ": an empty profile draws no expectations block at all")
                compare(one(v, "sdExpectations").height, 0, r + ": not even its space")
                compare(findAll(v, "sdExpectationCard").length, 0, r + ": and no rows")
                verify(allText(v).indexOf("Usually yours") < 0, r + ": and no heading anywhere in the view")
            }
            useRegister("Replay")
            applyState(Real.cold2(false))
            showTab("session")
            verify(!shown(one(viewOf("session"), "sdExpectations")), "the real Cold with no profile: no block")
        }

        // ═══════════════════════════ Forming: the session rows ═══════════════════════════

        function test_04_formingDrawsTheRowsAndTheirState() {
            useRegister("Analyse")
            setSource(spied(formingSource(false)))
            showTab("session")
            park()
            const v = viewOf("session")
            const rows = shownAll(v, "sdSessionRow")
            compare(rows.length, 2, "a row per pattern")
            compare(rows[0].cardId, "casting")
            compare(rows[1].cardId, "face_roll")
            verify(plain(one(rows[0], "sdSessionRowName").text).indexOf("Casting") === 0, "the row is named")
            compare(findAll(rows[0], "sdSessionRowFresh").length, 1, "the fresh pattern is marked new")
            compare(findAll(rows[1], "sdSessionRowFresh").length, 0, "and the other is not")
            verify(plain(one(rows[0], "sdSessionRowName").text).indexOf("new") > 0, "the word 'new' is on its name line")
            compare(one(rows[0], "sdSessionRowFired").text, "4/5", "a count, never a rate")
            compare(one(rows[1], "sdSessionRowFired").text, "3/5")
            // Below the agreement gate the row reports dispersion, not a direction.
            verify(one(rows[1], "sdSessionRowLine2").text.indexOf("dispersion") >= 0, "the withheld direction claim says so")
            verify(Qt.colorEqual(inkOf(one(rows[0], "sdSessionRowShot")), Theme.colorWarn),
                   "a pattern outside its corridor on this shot reads in the warn colour")
            verify(!Qt.colorEqual(inkOf(one(rows[1], "sdSessionRowShot")), Theme.colorWarn),
                   "and one inside it does not")
            compare(shownAll(v, "sdSessionRowFocus").length, 0, "the focus offer is quiet until the pointer is on a row")

            // The glance: a sparkline row (or a small multiple) per pattern, in the session's order.
            useRegister("Replay")
            showTab("session")
            const items = replaySessionItems(viewOf("session"))
            compare(items.length, 2, "Replay draws each pattern once")
            compare(items.map(function (i) { return i.cardId }).sort().join(","), "casting,face_roll")
        }

        // ═══════════════════════════ the strip (D5) ═══════════════════════════

        function marksOf(cell) {
            const stub = one(cell, "sdShotStub")
            const kids = stub.parent.children
            return { fired: kids[0], clean: kids[1], stub: stub }
        }
        // NEEDS-NEW-DESIGN → rewritten: THREE distinct marks — a fired bar, a clean tick, a not-measured
        // STUB on the baseline — and a cell for every shot. A gap would read as "nothing happened on that
        // swing", and the ledger has no such state.
        function test_05_notMeasuredIsAStubOnTheBaseline_neverAGap() {
            useRegister("Analyse")
            setSource(spied(formingSource(false)))
            showTab("session")
            const row = shownAll(viewOf("session"), "sdSessionRow")[0]
            const strip = oneShown(row, "sdShotStrip")
            const cells = findAll(strip, "sdShotCell")
            compare(cells.length, 5, "one cell per shot, the unmeasured one included")
            compare(cells.map(function (c) { return c.cellState }).join(","), "fired,notMeasured,fired,clean,fired")

            for (let i = 0; i < cells.length; ++i) {
                const m = marksOf(cells[i])
                const on = [m.fired.visible, m.clean.visible, m.stub.visible].filter(function (x) { return x }).length
                compare(on, 1, "cell " + i + " draws exactly one mark — never none")
            }
            const fired = marksOf(cells[0]), na = marksOf(cells[1]), clean = marksOf(cells[3])
            verify(Qt.colorEqual(fired.fired.color, Theme.colorWarn), "the fired bar is the warn colour")
            verify(Qt.colorEqual(clean.clean.color, Theme.colorText3), "the clean tick is ink3, not a good-green")
            verify(Qt.colorEqual(na.stub.color, Theme.colorText3), "the stub is ink3")
            verify(na.stub.height > 0, "the not-measured stub is drawn")
            verify(na.stub.height < clean.clean.height, "and is lower than the clean tick (" + na.stub.height + " vs " + clean.clean.height + ")")
            verify(clean.clean.height < fired.fired.height, "which is shorter than a firing")
            verify(na.stub.width > clean.clean.width, "a flat block, not a hairline: the three never read alike")
            compare(na.stub.y + na.stub.height, cells[1].height, "the stub stands on the baseline")
            compare(clean.clean.y + clean.clean.height, cells[3].height, "as does the clean tick")

            // The glance draws the same three: the row's strip, or the tile's run.
            useRegister("Replay")
            showTab("session")
            const items = replaySessionItems(viewOf("session")).filter(function (i) { return i.cardId === "casting" })
            compare(items.length, 1)
            if (items[0].objectName === "sdSparkRow") {
                const rc = findAll(oneShown(items[0], "sdShotStrip"), "sdShotCell")
                compare(rc.length, 5, "Replay's strip has a cell per shot")
                compare(rc.filter(function (c) { return marksOf(c).stub.visible }).length, 1, "and one stub")
            } else {
                compare(findAll(items[0], "sdTileStub").length, 1, "the tile's run carries the stub")
            }
        }

        // KEPT, narrowed: the meter is gone; "out by" is the measure of how far, and it REFUSES TO GUESS:
        // a reading nobody took is "-", never 0 — 0 would be the best news on the scale.
        function test_05b_outBySaysHowFarAndRefusesToGuess() {
            useRegister("Analyse")
            const r = reviewReadout()
            r.conditions[1].strengthKnown = false             // rushed transition: fired, no reading of how far
            setReview(spied(reviewSource()), r)
            showTab("shot")
            const rows = shownAll(viewOf("shot"), "sdFiredRow")
            compare(rows.length, 5, "a ledger row per firing")
            const byId = {}
            for (let i = 0; i < rows.length; ++i) byId[rows[i].conditionId] = rows[i]
            compare(one(byId["casting"], "sdFiredRowOutBy").text, "3.4×", "how far, as a multiple of the distance to the edge")
            compare(one(byId["rushed_transition"], "sdFiredRowOutBy").text, "-", "and no reading is a dash, never 0")
        }

        // KEPT, narrowed: COLOUR MEANS "OUTSIDE ITS CORRIDOR" AND NOTHING ELSE (RULES 7). A firing reads in
        // the warn colour; a clean value does not; and nothing on the panel is drawn in the alarm red.
        function test_05c_theWarnColourCarriesTheFiringAndOnlyAFiring() {
            useRegister("Analyse")
            setReview(spied(reviewSource()), reviewReadout())
            showTab("shot")
            const fired = shownAll(viewOf("shot"), "sdFiredRow")[0]
            verify(Qt.colorEqual(inkOf(one(fired, "sdFiredRowValue")), Theme.colorWarn), "a fired value reads in warn")
            showTab("session")
            const rows = shownAll(viewOf("session"), "sdSessionRow")
            verify(!Qt.colorEqual(inkOf(one(rows[1], "sdSessionRowShot")), Theme.colorWarn), "a clean one does not")

            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                for (const k of ["shot", "session", "watching"]) {
                    showTab(k)
                    const ts = shownTexts(reg())
                    for (let i = 0; i < ts.length; ++i)
                        verify(!Qt.colorEqual(ts[i].color, Theme.colorError),
                               r + "/" + k + ": nothing is an alarm ('" + plain(ts[i].text) + "' is colorError)")
                }
            }
        }

        // KEPT: the rank leads the row; a tie is marked on both holders; no rank, no number.
        function test_05d_theRankLeadsTheRow() {
            useRegister("Analyse")
            setSource(spied(formingSource(false)))
            showTab("session")
            let rows = shownAll(viewOf("session"), "sdSessionRow")
            compare(one(rows[0], "sdSessionRowRank").text, "1", "the model's rank, without its #")
            compare(one(rows[1], "sdSessionRowRank").text, "2")
            const rank = one(rows[0], "sdSessionRowRank"), name = one(rows[0], "sdSessionRowName")
            verify(name.x >= rank.x + rank.width, "the rank leads the name rather than crowding it")

            // A TIE IS MARKED ON BOTH HOLDERS: the row order must not assert an order the ranking refused.
            const tied = spied(formingSource(false))
            tied.cards[0].rankText = "#1="
            tied.cards[1].rankText = "#1="
            setSource(tied)
            rows = shownAll(viewOf("session"), "sdSessionRow")
            compare(one(rows[0], "sdSessionRowRank").text, "1=")
            compare(one(rows[1], "sdSessionRowRank").text, "1=")

            // ...and no rank means no number — the column is there, empty, and nothing is invented.
            const unranked = spied(formingSource(false))
            unranked.cards[0].rankText = ""
            setSource(unranked)
            rows = shownAll(viewOf("session"), "sdSessionRow")
            compare(one(rows[0], "sdSessionRowRank").text, "", "an unranked pattern draws no rank")
            compare(rows[0].rankText, "")
        }

        // ═══════════════════════════ cadence: the quiet shot (S8, D2) ═══════════════════════════

        // NEEDS-NEW-DESIGN → rewritten (D2): a quiet shot says so in ONE calm line built from the model's
        // cadence caption, keeps its shot ruler, and leaves the tabs live. Analyse states the same line
        // over the full read — a read is never gated (F21).
        function test_06_aQuietShotSaysSoAndKeepsItsRuler() {
            useRegister("Replay")
            setSource(spied(formingSource(false)))
            showTab("shot")
            let v = viewOf("shot")
            verify(!shown(one(v, "sdQuietState")), "a surfaced shot is not quiet")
            verify(shown(one(v, "sdShotHead")), "it has its head")

            setSource(spied(formingSource(true)))
            v = viewOf("shot")
            verify(shown(one(v, "sdQuietState")), "quiet takes This shot")
            compare(one(v, "sdQuietLabel").text, "Quiet shot — nothing new surfaced on this swing")
            verify(one(v, "sdQuietLine").text.indexOf("2 of 3 conditions fired on this swing") >= 0,
                   "the quiet line still says what the shot did")
            verify(one(v, "sdQuietLine").text.indexOf("the ledger is still counting") >= 0, "and that nothing stopped")
            verify(!shown(one(v, "sdShotHead")), "the loud head stands down")
            compare(shownAll(v, "sdDotRow").length, 0, "and the plot with it")

            // The newest swing PICKED: the same quiet line, and the ruler stays — a quiet panel must
            // never read as a stalled one.
            probe.readout = newestReadout()
            laidOut()
            v = viewOf("shot")
            verify(shown(one(v, "sdQuietState")), "picked, the newest swing is still quiet")
            verify(shown(one(v, "sdShotRuler")), "and its shot ruler stays")
            // The tabs stay live.
            mouseClick(tabByKey("session"))
            laidOut()
            compare(body.tab, "session", "the tabs take a press while quiet")

            useRegister("Analyse")
            probe.readout = null
            setSource(spied(formingSource(true)))
            showTab("shot")
            v = viewOf("shot")
            verify(shown(one(v, "sdQuietState")), "Analyse says the shot was quiet")
            verify(one(v, "sdQuietLine").text.indexOf("cadence gates surfacing, never the engine") >= 0, "and why")
            verify(shown(one(v, "sdDotPlot")), "and still draws the read — Analyse is never gated")
            compare(tabCount("shot").text, "quiet", "the tab's note says quiet")

            // The real session's quiet newest swing.
            useRegister("Replay")
            applyState(Real.quiet(true))
            showTab("shot")
            verify(shown(one(viewOf("shot"), "sdQuietState")), "the real quiet shot")
            verify(shown(one(viewOf("shot"), "sdShotRuler")), "with its ruler")
        }

        // ═══════════════════════════ Closing (S13, F19, F38) ═══════════════════════════

        // KEPT, rewritten: the bookends are three columns on each pattern's row (typical, worst, best) that
        // name a shot, folded onto the row's second line below wide; This shot says the session has closed.
        function test_07_closingPutsTheBookendsOnTheRowsAndTheTenseOnThisShot() {
            useRegister("Analyse")
            setSource(spied(closingSource()))
            showTab("session")
            let rows = shownAll(viewOf("session"), "sdSessionRow")
            compare(rows.length, 2, "the rows survive the close")
            const t = oneShown(rows[0], "sdSessionRowTypical"), w = oneShown(rows[0], "sdSessionRowWorst"),
                  b = oneShown(rows[0], "sdSessionRowBest")
            compare([t.text, w.text, b.text].join(","), "4,3,5", "typical, worst and best each name a shot")
            compare(t.shotId, 4, "and carry it, to put that shot in the carousel")

            // Narrower: the columns FOLD onto the row's second line, with their words; they never vanish.
            probe.width = 480
            laidOut()
            rows = shownAll(viewOf("session"), "sdSessionRow")
            const ft = oneShown(rows[0], "sdSessionRowTypical")
            compare(ft.text, "4")
            verify(allText(ft).indexOf("typical shot") >= 0, "folded, it says what it is: " + allText(ft))
            probe.width = 1168
            laidOut()

            // This shot, in the closed tense: no swing is coming, so it says where a shot comes from.
            for (const r of ["Analyse", "Replay"]) {
                useRegister(r)
                showTab("shot")
                const e = oneShown(viewOf("shot"), "sdShotEmpty")
                verify(allText(e).indexOf("The session has closed") >= 0, r + ": the closed tense: " + allText(e))
                verify(allText(e).indexOf("carousel") >= 0, r + ": and where to pick a shot")
                verify(allText(e).toLowerCase().indexOf("next swing") < 0, r + ": never 'the next swing'")
            }
        }

        // ═══════════════════════════ the tabs ═══════════════════════════

        function test_07b_theShotAndTheSessionAreTabsNotAStack() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setSource(spied(formingSource(false)))
                compare(shownAll(body, "sdTab").length, 3, r + ": three tabs")
                compare(body.tab, "session", r + ": Session rests")
                verify(shown(viewOf("session")))
                compare(shownAll(body, "sdShotView").length, 0, r + ": This shot is not stacked over it")

                mouseClick(tabByKey("shot"))
                laidOut()
                compare(body.tab, "shot", r + ": the press is the body's tab")
                verify(shown(viewOf("shot")))
                verify(!shown(one(body, "sdSessionView")), r + ": the session view steps aside (and is kept)")

                // A closed live session has no after-shot moment — but the tab still takes the press and
                // says where a shot comes from, rather than ignoring it.
                setSource(spied(closingSource()))
                body.tab = "session"
                laidOut()
                mouseClick(tabByKey("shot"))
                laidOut()
                compare(body._tab, "shot", r + ": This shot can always be pressed")
                verify(allText(oneShown(viewOf("shot"), "sdShotEmpty")).indexOf("carousel") >= 0)
                body.tab = "session"
            }
            // The read counts beside its tabs; the glance does not (each view's head states its own).
            useRegister("Analyse")
            setSource(spied(formingSource(false)))
            compare(tabCount("shot").text, "2", "This shot: what the newest swing fired")
            compare(tabCount("session").text, "2", "Session: the patterns")
            compare(tabCount("watching").text, "2", "Watching: the watched")
            useRegister("Replay")
            for (const k of ["shot", "session", "watching"])
                verify(!shown(tabCount(k)), "Replay: no count beside " + k)
        }

        // KEPT: the pick edge opens This shot ONCE; the auto-closing cast holds This shot and is the glance.
        function test_07c_pickingAShotOpensItsTabAndTheCastHoldsIt() {
            useRegister("Analyse")
            setReview(spied(reviewSource()), null)
            compare(body._tab, "session", "review with nothing picked rests on the session")
            probe.readout = reviewReadout()
            laidOut()
            compare(body._tab, "shot", "picking a shot off the carousel opens This shot")
            verify(shownAll(viewOf("shot"), "sdDotRow").length > 0, "and reads it")

            // ...once, on the edge: a reader who goes back is not dragged back by a re-fetch.
            body.tab = "session"
            probe.readout = reviewReadout()
            laidOut()
            compare(body._tab, "session")

            // The auto-closing cast: no tabs to press, This shot held, the Replay register whatever the
            // stage is in (D4), and its own head in place of the body's frame (S30).
            SessionMode.mode = SessionMode.analyse
            body.surface = "cast"
            body.interactive = false
            setReview(spied(formingSource(false)), null)
            compare(body._tab, "shot", "the cast is there for the shot")
            compare(reg().register, "Replay", "the cast is the glance, even with the stage in Analyse")
            verify(shown(viewOf("shot")))
            compare(shownAll(body, "sdTab").length, 0, "an auto-closing cast draws no tabs to press")
            verify(shown(one(body, "sdCastHead")), "the cast names its shot in its own head")
            verify(!shown(one(body, "sdTitle")), "and the body's frame stands down")
        }

        // KEPT: live, This shot follows the carousel — and says WHICH shot by its ordinal (D7).
        function test_07d_aLivePickOfAnOlderSwingIsTheShotRead() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                probe.readout = null
                setSource(spied(formingSource(false)))
                body.tab = "session"
                probe.readout = reviewReadout()          // shot 9 of 14 — swing id 23 — not the newest
                laidOut()
                compare(body.reviewing, false, "a live session, not review")
                compare(body._tab, "shot", r + ": the picked swing is read")
                const h = headText(oneShown(viewOf("shot"), "sdShotHead")).toLowerCase()
                verify(h.indexOf("shot 9 of 14") >= 0, r + ": named by its ordinal: " + h)
                verify(h.indexOf("23") < 0, r + ": never by its swing id: " + h)
                verify(!shown(one(body, "sdShotLabel")), r + ": the frame's newest-shot label stands down")
            }
            probe.readout = null
        }

        // ═══════════════════════════ Watching + coverage ═══════════════════════════

        function test_08_watchingIsATabOfItsOwn() {
            useRegister("Analyse")
            setReview(spied(formingSource(false)), newestReadout())
            compare(tabCount("watching").text, "2", "the tab counts them")
            mouseClick(tabByKey("watching"))
            laidOut()
            compare(body._tab, "watching")
            compare(shownAll(viewOf("watching"), "sdBarcodeRow").length, 2, "a barcode row per watched condition")
            verify(!shown(one(body, "sdSessionView")), "and the session view steps aside")

            // Nothing picked: no run per watched condition to draw, so names and counts — a barcode of
            // blanks would be a gap, never "not measured".
            probe.readout = null
            laidOut()
            const words = allText(viewOf("watching"))
            verify(words.indexOf("Lateral sway") >= 0 && words.indexOf("Flat shoulder plane") >= 0, "every watched one is named")
            compare(shownAll(viewOf("watching"), "sdBarcodeRow").length, 0, "with no barcode of blanks")

            useRegister("Replay")
            showTab("watching")
            compare(oneShown(viewOf("watching"), "sdWatchingHead").title, "Two watched", "the glance's head states the count")

            // An empty tab says what would be there rather than going blank.
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                const none = spied(formingSource(false))
                none.watching = []
                setSource(none)
                showTab("watching")
                const e = oneShown(viewOf("watching"), "sdWatchingEmpty")
                verify(plain(e.text).indexOf("Nothing below the pattern gate yet") === 0, r + ": stated, not blank")
            }
        }

        // KEPT: the coverage line is on the Session view at every stage, in BOTH registers (F39).
        function test_09_coverageIsAlwaysStated() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                const sources = [coldSource(probe.expectationRows), formingSource(false), establishedSource(true), closingSource()]
                for (let i = 0; i < sources.length; ++i) {
                    setSource(spied(sources[i]))
                    showTab("session")
                    const cov = oneShown(viewOf("session"), "sdCoverageLine")
                    verify(plain(cov.text).indexOf("140") >= 0, r + " source " + i + ": the whole pack, not the measured few")
                }
            }
        }

        // #17: once the session has closed (live end or review), the Session view states the model's closing
        // line in BOTH registers: the form naming this shot when a shot is read, the form without it when none
        // is (it must not claim a highlight nothing draws). In Analyse it sits under the table it explains,
        // above the coverage line. Not while the session runs, and not on the cast. (The words themselves are
        // pinned against the real model in session_diagnostics_model_test.)
        function test_09b_aClosedSessionSaysWhatItsCountsAre() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                const src = reviewSource()
                setReview(spied(src), reviewReadout())
                showTab("session")
                let cl = oneShown(viewOf("session"), "sdClosingLine")
                compare(plain(cl.text), src.headerInfo.closingLine, r + ": a shot is read, so the line names it")
                if (r === "Analyse") {
                    const cov = oneShown(viewOf("session"), "sdCoverageLine")
                    verify(cl.mapToItem(viewOf("session"), 0, 0).y < cov.mapToItem(viewOf("session"), 0, 0).y,
                           "Analyse: the closing line sits under the table, above the coverage line")
                }
                setReview(spied(reviewSource()), null)
                showTab("session")
                cl = oneShown(viewOf("session"), "sdClosingLine")
                compare(plain(cl.text), src.headerInfo.closingLineNoShot, r + ": no shot read, so the line names none")
                verify(plain(cl.text).indexOf("this shot") < 0, r + ": and claims no highlight")
                setSource(spied(closingSource()))
                showTab("session")
                verify(shownAll(viewOf("session"), "sdClosingLine").length === 1, r + ": a live session that has ended states it")
                setSource(spied(establishedSource(true)))
                showTab("session")
                verify(shownAll(viewOf("session"), "sdClosingLine").length === 0, r + ": no closing line while the session runs")
            }
            // the cast is the golfer's glance: like the review note, the tense is not stated there
            body.surface = "cast"
            setReview(spied(reviewSource()), reviewReadout())
            body.tab = "session"
            laidOut()
            compare(shownAll(body, "sdClosingLine").length, 0, "the cast states no closing line")
        }

        // ═══════════════════════════ the fit ═══════════════════════════

        // KEPT, narrowed: at 396 the panel REDUCES rather than shrinking — every pattern laid out and
        // reachable by scrolling, the frame's title abbreviated, and nothing ellipsised.
        function test_11_narrowReducesRatherThanShrinking() {
            const many = formingSource(false)
            many.cards = []
            for (let i = 0; i < 9; ++i) {
                const c = patternCards()[0]
                c.id = "c" + i
                c.name = "Condition " + i
                c.rankText = "#" + (i + 1)
                c.fresh = false
                many.cards.push(c)
            }
            probe.width = 396; probe.height = 560
            for (const r of ["Analyse", "Replay"]) {
                useRegister(r)
                setSource(spied(JSON.parse(JSON.stringify(many))))
                showTab("session")
                const v = viewOf("session")
                const rows = r === "Analyse" ? shownAll(v, "sdSessionRow") : replaySessionItems(v)
                compare(rows.length, 9, r + ": every pattern is laid out, not just the ones that fit")
                for (let i = 0; i < rows.length; ++i)
                    verify(rows[i].mapToItem(v, rows[i].width, 0).x <= v.width + 1, r + ": row " + i + " fits the width")
                const flick = oneShown(body, "sdCardsFlick")
                // Analyse's two-line rows overflow 560; Replay's one-line rows may all fit (then the last is
                // simply on screen). Either way the ninth must be reachable.
                if (r === "Analyse")
                    verify(flick.contentHeight > flick.height, r + ": there is something below to reach")
                if (flick.contentHeight > flick.height)
                    flick.contentY = flick.contentHeight - flick.height
                laidOut()
                const top = rows[8].mapToItem(flick, 0, 0).y
                verify(top < flick.height && top + rows[8].height > 0, r + ": the ninth is on screen at the bottom of the scroll")
                const ts = shownTexts(reg())
                for (let i = 0; i < ts.length; ++i)
                    verify(ts[i].elide === Text.ElideNone || !ts[i].truncated, r + ": '" + plain(ts[i].text) + "' is not ellipsised")
                verify(plain(one(body, "sdTitle").text).toLowerCase() === "session diag.", "the title abbreviates, never elides")
            }
        }

        // KEPT: two questions, not a score. The filter is the BODY's state (navigation saves it, D8), ranks
        // are never renumbered, an emptying filter explains which claim it is; the glance never filters.
        function test_11c_theFilterAsksTwoQuestionsAndRenumbersNothing() {
            useRegister("Analyse")
            const s = spied(formingSource(false))
            s.cards[0].reachesBall = false; s.cards[0].rootHere = true
            s.cards[1].reachesBall = true;  s.cards[1].rootHere = false
            setSource(s)
            showTab("session")
            let tools = shownAll(viewOf("session"), "sdFilterTool")
            compare(tools.map(function (t) { return t.key }).join(","), "all,ball,roots", "All, Reaches the ball, Roots")
            compare(tools[0].text, "All")
            verify(tools[0].on, "All is the resting state")
            compare(shownAll(viewOf("session"), "sdSessionRow").length, 2, "with nothing filtered out")

            mouseClick(tools[1])
            laidOut()
            compare(body.cardFilter, "ball", "the filter is the body's, so it survives a rebuild")
            let rows = shownAll(viewOf("session"), "sdSessionRow")
            compare(rows.length, 1, "only the pattern with an authored path to the ball")
            compare(rows[0].cardId, "face_roll")
            compare(one(rows[0], "sdSessionRowRank").text, "2", "THE RANK IS NOT RENUMBERED")
            tools = shownAll(viewOf("session"), "sdFilterTool")
            verify(tools[1].on && !tools[0].on, "the tool says which question is open")
            compare(tabCount("session").text, "1", "the tab's count follows the filter")

            mouseClick(tools[2])
            laidOut()
            rows = shownAll(viewOf("session"), "sdSessionRow")
            compare(rows.length, 1, "roots only")
            compare(rows[0].cardId, "casting")
            mouseClick(shownAll(viewOf("session"), "sdFilterTool")[0])
            laidOut()
            compare(shownAll(viewOf("session"), "sdSessionRow").length, 2, "All puts them back")

            // A FILTER THAT EMPTIES THE LIST SAYS SO, and which claim it is making.
            const none = spied(formingSource(false))
            none.cards[0].reachesBall = false
            none.cards[1].reachesBall = false
            setSource(none)
            mouseClick(shownAll(viewOf("session"), "sdFilterTool")[1])
            laidOut()
            compare(shownAll(viewOf("session"), "sdSessionRow").length, 0)
            const e = oneShown(viewOf("session"), "sdCardFilterEmpty")
            verify(plain(e.text).indexOf("what the model has been written to claim") >= 0,
                   "an unwritten edge does not read as a harmless fault")

            // A glance shows every pattern: Replay has no filter, whatever the body's is.
            useRegister("Replay")
            showTab("session")
            compare(shownAll(viewOf("session"), "sdFilterTool").length, 0, "no filter on the glance")
            compare(replaySessionItems(viewOf("session")).length, 2, "and every pattern drawn")
            body.cardFilter = "all"
        }

        // KEPT, adapted: light and dark of one aesthetic (Folio) — the same geometry, a different warn.
        function test_12_bothThemesOfOneAesthetic() {
            useRegister("Analyse")
            setSource(spied(formingSource(false)))
            showTab("session")
            const geometry = [], marks = []
            for (const t of [12, 13]) {
                Theme.themeIndex = t
                laidOut()
                const rows = shownAll(viewOf("session"), "sdSessionRow")
                compare(rows.length, 2, "theme " + t + ": both rows")
                geometry.push(Math.round(rows[0].width) + "x" + Math.round(rows[0].height))
                const cells = findAll(oneShown(rows[0], "sdShotStrip"), "sdShotCell")
                compare(cells.length, 5, "theme " + t + ": the strip is intact")
                verify(marksOf(cells[1]).stub.visible, "theme " + t + ": not measured is still the stub")
                marks.push(String(marksOf(cells[0]).fired.color))
                verify(shown(one(viewOf("session"), "sdCoverageLine")), "theme " + t + ": coverage stated")
            }
            compare(geometry[0], geometry[1], "geometry does not depend on light or dark")
            verify(marks[0] !== marks[1], "but the fired mark does — both themes are real")
            Theme.themeIndex = 5
        }

        // ═══════════════════════════ the condition detail (Analyse; Replay never drills, D9) ═══════════════════════════

        function graphs() { return shownAll(viewOf("detail"), "sdCauseGraph") }
        function graphOf(which) {
            const gs = graphs().filter(function (g) { return g.which === which })
            verify(gs.length === 1, "one " + which + " graph on screen")
            return gs[0]
        }
        // A node other than the open condition itself (that one is in both graphs, and takes no click).
        function nodeById(id) {
            const gs = graphs()
            for (let i = 0; i < gs.length; ++i) {
                const ns = shownAll(gs[i], "sdCauseNode")
                for (let j = 0; j < ns.length; ++j)
                    if (ns[j].conditionId === id && !ns[j].isSubject) return ns[j]
            }
            return null
        }
        function clickNode(n) {
            intoView(n)
            mouseClick(n, Theme.sp(20), Theme.sp(6))     // on its name, inside the node's own pick area
            laidOut()
        }
        // What the model authored: the unique node ids and the unique (from, to) pairs across the paths.
        function authored(paths) {
            const nodes = {}, edges = {}
            for (const p of paths) {
                const ns = p.nodes || []
                for (let i = 0; i < ns.length; ++i) {
                    nodes[ns[i].id] = true
                    if (i + 1 < ns.length) edges[ns[i].id + ">" + ns[i + 1].id] = true
                }
            }
            return { nodes: Object.keys(nodes).length, edges: Object.keys(edges).length }
        }

        // KEPT, rewritten: the detail draws every authored node once and ONLY the edges the model authored
        // — merged into one graph per side from medium up, as numbered paths below it.
        function test_14_theDetailDrawsEveryPathAndOnlyTheEdgesTheModelAuthored() {
            useRegister("Analyse")
            setSource(spied(establishedSource(true)))
            showTab("session")
            compare(shownAll(body, "sdCauseGraph").length, 0, "the session draws no chain: a row opens INTO one")

            setSource(detailSource())
            const d = detailPayload()
            const c = authored(d.causes), e = authored(d.effects)
            compare(shownAll(graphOf("causes"), "sdCauseNode").length, c.nodes, "every cause-side node, once")
            compare(shownAll(graphOf("causes"), "sdCauseEdge").length, c.edges, "and only the authored edges")
            compare(shownAll(graphOf("effects"), "sdCauseNode").length, e.nodes)
            compare(shownAll(graphOf("effects"), "sdCauseEdge").length, e.edges)
            compare(c.edges + e.edges, 7, "seven authored edges across the neighbourhood, and no others")

            // Below medium the graph CHANGES FORM: the paths, numbered, one per authored path.
            probe.width = 480
            laidOut()
            compare(shownAll(viewOf("detail"), "sdCauseGraph").length, 0, "no squeezed graph")
            const pc = oneShown(viewOf("detail"), "sdPathChains")
            compare(shownAll(pc, "sdPathChain").length, d.causes.length + d.effects.length, "every path listed")
        }

        // NEEDS-NEW-DESIGN → rewritten (D6, F60): the five grades keep FIVE distinct strokes, and the
        // arrowhead only where the evidence supports a direction — moved together and conditionally
        // dependent. This is the honesty device of the whole model.
        function test_15_everyLinkGradeGetsItsOwnStroke() {
            useRegister("Analyse")
            setSource(detailSource())
            const edges = shownAll(viewOf("detail"), "sdCauseEdge")
            const byGrade = {}
            for (let i = 0; i < edges.length; ++i) byGrade[edges[i].grade] = edges[i]
            const want = {
                movedTogether:          { stroke: "solid",  width: 2.0, arrow: true,  opacity: 1.0, ink: Theme.colorText },
                conditionallyDependent: { stroke: "solid",  width: 1.5, arrow: true,  opacity: 1.0, ink: Theme.colorText2 },
                coherent:               { stroke: "dashed", width: 1.5, arrow: false, opacity: 1.0, ink: Theme.colorText2 },
                presentTogether:        { stroke: "dotted", width: 1.0, arrow: false, opacity: 1.0, ink: Theme.colorText3 },
                unanchored:             { stroke: "dotted", width: 1.0, arrow: false, opacity: 0.5, ink: Theme.colorText3 }
            }
            const signatures = {}
            for (const g in want) {
                const e = byGrade[g], w = want[g]
                verify(!!e, g + " is drawn")
                compare(e.strokeStyle, w.stroke, g + ": stroke")
                compare(e.hasArrow, w.arrow, g + (w.arrow ? ": a direction the evidence supports gets a head" : ": no head"))
                fuzzyCompare(e.st.width, w.width, 0.01, g + ": weight")
                fuzzyCompare(e.strokeOpacity, w.opacity, 0.01, g + ": opacity")
                verify(Qt.colorEqual(e.st.color, w.ink), g + ": ink, never the accent and never warn (colour means 'out')")
                signatures[e.strokeStyle + "|" + e.st.width + "|" + e.strokeOpacity + "|" + e.st.color] = true
            }
            compare(Object.keys(signatures).length, 5, "five grades, five different strokes")
            for (let i = 0; i < edges.length; ++i)
                verify(!edges[i].hasArrow || edges[i].grade === "movedTogether" || edges[i].grade === "conditionallyDependent",
                       "an arrowhead only on the two strong grades, never on " + edges[i].grade)

            // The paths' form: one glyph per grade, keyed once, the arrow sentence with it.
            probe.width = 480
            laidOut()
            const pc = oneShown(viewOf("detail"), "sdPathChains")
            const glyphs = {}
            for (const g in want) glyphs[pc.linkGlyph(g)] = g
            compare(Object.keys(glyphs).length, 5, "five grades, five glyphs")
            const words = shownTexts(pc).map(function (t) { return t.text }).join(" ")
            for (const gl in glyphs)
                verify(words.indexOf(gl) >= 0, "the " + glyphs[gl] + " glyph " + gl + " is drawn")
            verify(plain(words).indexOf("An arrow only where the evidence gives a direction.") >= 0, "and the key says what the arrow means")
        }

        // NEEDS-NEW-DESIGN → rewritten (F56, F59): every kind is drawn as itself — a live reading, a ghost,
        // a screened root (a REQUEST, never a finding), the declared outcome — and a non-card condition's
        // page says what kind of thing it is.
        function test_16_ghostScreenedRootAndOutcomeAreEachDrawnAsThemselves() {
            useRegister("Analyse")
            setSource(detailSource())
            const live = nodeById("rushed_transition")
            compare(live.kind, "live")
            compare(live.mark, "fired", "a live node is its reading")
            compare(nodeById("shaft_lean").mark, "clean")
            const ghost = nodeById("over_the_top")
            compare(ghost.kind, "ghost")
            compare(ghost.mark, "ghost", "authored, not measured: never a reading")
            const root = nodeById("pelvic_disassociation")
            compare(root.kind, "screenedRoot")
            compare(root.mark, "root")
            compare(oneShown(root, "sdScreenCta").text, "Run the screen", "a screened root is drawn as a request")
            const outcome = nodeById("slice")
            compare(outcome.kind, "outcome")
            compare(outcome.mark, "outcome", "the declared miss is an outcome, not a fired condition")

            // The honesty mark: the page of a condition that is not a pattern card says what it is.
            verify(!shown(one(viewOf("detail"), "sdDetailMark")), "a pattern's own page carries no mark")
            const s = detailSource()
            s.detail.header.kindText = "ghost"
            s.detail.header.mark = "authored as asserted, no measure"
            republish(s)
            compare(oneShown(viewOf("detail"), "sdDetailMark").text, "ghost · authored as asserted, no measure")

            // The paths' form carries the marks as footnotes, keyed once.
            probe.width = 480
            laidOut()
            const words = allText(oneShown(viewOf("detail"), "sdPathChains"))
            verify(words.indexOf("screened root, needs a physical screen") >= 0, "the screened root's mark: " + words)
            verify(words.indexOf("declared miss, launch-monitor verified") >= 0, "and the outcome's")
        }

        // KEPT, narrowed: a measured condition below the gate is a READING, never an absence. A watched
        // node keeps its kind and draws its reading, and the paths say "measured · not yet a pattern".
        function test_16b_aMeasuredConditionBelowTheGateIsDrawnAsAReading() {
            useRegister("Analyse")
            const s = detailSource()
            const rail = s.detail.causes[0]
            rail.nodes[0] = chainNode("hanging_back", "Hanging back", "watched", "measured · not yet a pattern",
                                      { phase: "downswing", measure: "thorax drift", recurrence: "3 of 7 measurable shots",
                                        state: "fired", statePill: "FIRED", strengthKnown: true })
            rail.links[0].from = "hanging_back"
            setSource(s)
            const n = nodeById("hanging_back")
            verify(!!n, "the watched node is drawn")
            compare(n.kind, "watched")
            verify(n.mark !== "ghost", "not drawn as an absence")
            compare(n.mark, "fired", "it is what it read on this shot")

            probe.width = 480
            laidOut()
            const pc = oneShown(viewOf("detail"), "sdPathChains")
            const words = allText(pc)
            verify(words.indexOf("measured, not yet a pattern") >= 0, "the paths say what is true of it: " + words)
            verify(words.indexOf("planned") < 0, "and never that its measure is planned")
        }

        // KEPT, rewritten: below medium every path is listed, and "Open" expands one IN PLACE (F58) —
        // node by node, link by link, with the grade's word and the model's note — and closes again.
        function test_19_narrowListsEveryPathAndOpensOneInPlace() {
            useRegister("Analyse")
            probe.width = 480
            setSource(detailSource())
            const pc = oneShown(viewOf("detail"), "sdPathChains")
            const causes = shownAll(pc, "sdPathChain").filter(function (c) { return c.which === "causes" })
            compare(causes.length, 2, "both cause paths listed")
            const second = causes[1]
            compare(one(second, "sdPathOpen").open, false, "closed at rest")
            const h0 = second.height
            intoView(second)
            mouseClick(second, second.width / 2, Theme.sp(4))
            laidOut()
            compare(one(second, "sdPathOpen").open, true, "Open expands the path")
            verify(second.height > h0, "in place, under itself")
            const words = allText(second)
            verify(words.indexOf("Present together: neither measured") >= 0, "each link with its grade and note: " + words)
            verify(words.indexOf("Over the top") >= 0, "each node with its reading")
            mouseClick(second, second.width / 2, Theme.sp(4))
            laidOut()
            compare(one(second, "sdPathOpen").open, false, "and closes again")
        }

        // NEEDS-NEW-DESIGN → rewritten (F33): the quiet focus link on a session row — shown on hover, and
        // at rest once focused, with the focused row marked; read back off the model.
        function test_27_aPatternRowOffersFocusAndSaysWhichStateItIsIn() {
            useRegister("Analyse")
            setSource(spied(formingSource(false)))
            showTab("session")
            park()
            let rows = shownAll(viewOf("session"), "sdSessionRow")
            compare(shownAll(viewOf("session"), "sdSessionRowFocus").length, 0, "quiet at rest")
            compare(shownAll(viewOf("session"), "sdSessionRowFocusBar").length, 0, "no row marked focused")
            intoView(rows[0])
            hoverRow(rows[0])
            const offer = oneShown(rows[0], "sdSessionRowFocus")
            compare(offer.text, "Focus", "offered while the pointer is on the row")
            compare(offer.focused, false)
            park()

            const s = spied(formingSource(false))
            s.cards[0].focused = true
            s.focusConditionId = "casting"
            setSource(s)
            park()
            rows = shownAll(viewOf("session"), "sdSessionRow")
            const f = oneShown(rows[0], "sdSessionRowFocus")
            compare(f.text, "Focused · clear", "the focused row says so at rest")
            verify(f.focused)
            verify(shown(one(rows[0], "sdSessionRowFocusBar")), "and is marked")
            compare(shownAll(rows[1], "sdSessionRowFocus").length, 0, "the other offers nothing at rest")
            verify(!shown(one(rows[1], "sdSessionRowFocusBar")), "and is not marked")

            useRegister("Replay")
            showTab("session")
            verify(headText(oneShown(viewOf("session"), "sdSessionHead")).indexOf("focus: Casting") >= 0,
                   "the glance names the focus in its head")
        }

        // KEPT: the focus link ASKS the model (declareFocus / clearFocus by the model's state), opens
        // nothing, and the panel waits for the read-back. The detail's own focus toggle does the same.
        function test_28_theFocusLinkAsksTheModelToDeclareOrClearFocus() {
            useRegister("Analyse")
            setSource(spied(formingSource(false)))
            showTab("session")
            park()
            let row = shownAll(viewOf("session"), "sdSessionRow")[0]
            intoView(row)
            hoverRow(row)
            mouseClick(oneShown(row, "sdSessionRowFocus"))
            laidOut()
            compare(probe.focusCalls, 1, "the link reached declareFocus")
            compare(probe.lastFocusId, "casting", "with the row's own condition")
            compare(probe.clearFocusCalls, 0)
            compare(probe.detailCalls, 0, "and opened nothing")
            compare(row.focused, false, "the panel waits for the model rather than assuming")

            const s = spied(formingSource(false))
            s.cards[0].focused = true
            setSource(s)
            park()
            row = shownAll(viewOf("session"), "sdSessionRow")[0]
            mouseClick(oneShown(row, "sdSessionRowFocus"))
            laidOut()
            compare(probe.clearFocusCalls, 1, "a focused row clears")
            compare(probe.focusCalls, 1, "and does not re-declare")

            // The detail's toggle.
            setSource(detailSource())
            const df = oneShown(body, "sdDetailFocus")
            compare(df.focused, false)
            mouseClick(df)
            laidOut()
            compare(probe.focusCalls, 2, "the detail's focus toggle declares")
            compare(probe.lastFocusId, "casting")
            const s2 = detailSource()
            s2.cards[0].focused = true
            setSource(s2)
            const df2 = oneShown(body, "sdDetailFocus")
            verify(df2.focused, "read back off the model")
            compare(df2.text, "Focused · clear")
            mouseClick(df2)
            laidOut()
            compare(probe.clearFocusCalls, 2, "and clears")
        }

        // NEW (four-modes Phase 2, C3): IN A LIVE SESSION THE FOCUS IS SessionController's. A
        // declaration still goes to the model first; the body then writes the model's answer back to the
        // controller the panel handed it — the split before the id, each only on a real difference — so
        // the other surfaces adopt it. With no controller (review, a finished session) nothing is written.
        // The adoption half lives in PpSessionDiagnosticsPanel, which this file does not load.
        function test_28b_aLiveDeclarationIsWrittenBackToTheController() {
            useRegister("Analyse")
            const s = spied(formingSource(false))
            const declare = s.declareFocus
            s.declareFocus = function (id) { declare(id); s.focusFromShot = 4 }   // the model's split
            setSource(s)
            body.focusController = fakeController
            showTab("session")
            park()
            let row = shownAll(viewOf("session"), "sdSessionRow")[0]
            intoView(row)
            hoverRow(row)
            mouseClick(oneShown(row, "sdSessionRowFocus"))
            laidOut()
            compare(probe.focusCalls, 1, "the model is asked first")
            compare(fakeController.focusConditionId, "casting", "the focus reaches the controller")
            compare(fakeController.focusFromShot, 4, "with the split the model recorded")
            compare(fakeController.idWrites, 1)
            compare(fakeController.splitWrites, 1)

            // Declaring the focus already held writes nothing back.
            body._declareFocus("casting", true)
            compare(fakeController.idWrites, 1, "an unchanged id is not rewritten")
            compare(fakeController.splitWrites, 1, "nor an unchanged split")

            // Withdrawn on the panel: withdrawn on the controller, its split back to the default.
            const f = spied(formingSource(false))
            f.cards[0].focused = true
            setSource(f)
            park()
            row = shownAll(viewOf("session"), "sdSessionRow")[0]
            intoView(row)
            hoverRow(row)
            mouseClick(oneShown(row, "sdSessionRowFocus"))
            laidOut()
            compare(probe.clearFocusCalls, 1)
            compare(fakeController.focusConditionId, "", "a clear reaches the controller")
            compare(fakeController.focusFromShot, 0)

            // Not live: the model is still asked, the controller is left alone.
            body.focusController = null
            body._declareFocus("casting", true)
            compare(probe.focusCalls, 3, "the model is asked")
            compare(fakeController.focusConditionId, "", "and nothing is written back")
        }

        // NEEDS-NEW-DESIGN → rewritten (F63-F64): a screened root asks for its SCREEN, never for a page,
        // with the model's screen reference when the model recommended it; a live node opens its page and
        // declares nothing. Below medium (no graph) one line lists the screened roots, each a request.
        function test_29_aScreenedRootAsksForItsScreenNeverForAPage() {
            useRegister("Analyse")
            setSource(detailSource())
            const root = nodeById("pelvic_disassociation")
            clickNode(root)
            compare(probe.detailCalls, 0, "a screened root opens no page")
            compare(probe.screenCalls, 1, "it asks for its screen")
            compare(probe.lastScreenCondition, "pelvic_disassociation")
            compare(probe.lastScreenRef, "screen.pelvic_disassociation", "the model's screen, when it recommended this one")
            compare(probe.focusCalls, 0, "and declares nothing")

            const cta = oneShown(nodeById("pelvic_disassociation"), "sdScreenCta")
            intoView(cta)
            mouseClick(cta)
            laidOut()
            compare(probe.screenCalls, 2, "its 'Run the screen' asks too")

            clickNode(nodeById("rushed_transition"))
            compare(probe.detailCalls, 1, "a live node opens its own page")
            compare(probe.lastDetailId, "rushed_transition")
            compare(probe.focusCalls, 0, "and declares nothing either")

            probe.width = 480
            laidOut()
            const req = oneShown(viewOf("detail"), "sdScreenRequests")
            const ctas = shownAll(req, "sdScreenCta")
            compare(ctas.length, 1, "one request per screened root on the paths")
            compare(ctas[0].conditionId, "pelvic_disassociation")
            intoView(ctas[0])
            mouseClick(ctas[0])
            laidOut()
            compare(probe.screenCalls, 3)
            compare(probe.lastScreenCondition, "pelvic_disassociation")
        }

        // KEPT, rewritten: EVERY CLICK GOES SOMEWHERE REAL. A row opens its condition (and not focus); a
        // strip cell puts its shot in the carousel; a watched row opens its condition; a bookend puts its
        // SHOT in the carousel (F38: the behaviour change); a glance's row opens the condition in Analyse.
        function test_38_everyClickGoesSomewhereReal() {
            useRegister("Analyse")
            setSource(spied(formingSource(false)))
            showTab("session")
            park()
            let row = shownAll(viewOf("session"), "sdSessionRow")[0]
            clickRow(row)
            compare(probe.detailCalls, 1, "the row asked for its condition's page")
            compare(probe.lastDetailId, "casting")
            compare(probe.focusCalls, 0, "and did not declare focus on the way")

            row = shownAll(viewOf("session"), "sdSessionRow")[0]
            const cell = findAll(oneShown(row, "sdShotStrip"), "sdShotCell")[2]
            mouseClick(cell)
            laidOut()
            compare(probe.lastShotId, 3, "a strip cell asks for its shot")
            compare(probe.detailCalls, 1, "and opens nothing")

            setReview(spied(formingSource(false)), newestReadout())
            showTab("watching")
            const wr = shownAll(viewOf("watching"), "sdBarcodeRow").filter(function (r) { return r.conditionId === "sway" })[0]
            intoView(wr)
            mouseClick(wr, 2, wr.height / 2)
            laidOut()
            compare(probe.lastDetailId, "sway", "a watched row opens its own condition")

            probe.readout = null
            setSource(spied(closingSource()))
            showTab("session")
            row = shownAll(viewOf("session"), "sdSessionRow")[1]
            const worst = oneShown(row, "sdSessionRowWorst")
            const kids = worst.children
            let txt = null
            for (let i = 0; i < kids.length; ++i) if (isText(kids[i])) txt = kids[i]
            probe.detailCalls = 0
            intoView(worst)
            mouseClick(txt)
            laidOut()
            compare(probe.lastShotId, 1, "a bookend puts ITS shot in the carousel")
            compare(probe.detailCalls, 0, "rather than opening its condition")

            useRegister("Replay")
            setReview(spied(reviewSource()), reviewReadout())
            showTab("shot")
            const dot = shownAll(viewOf("shot"), "sdDotRow")[0]
            const id = dot.conditionId
            probe.detailCalls = 0
            intoView(dot)
            mouseClick(dot)
            laidOut()
            compare(probe.detailCalls, 1, "a glance's row opens its condition")
            compare(probe.lastDetailId, id)
            compare(SessionMode.mode, SessionMode.replay, "and the stage stays in Replay")
        }

        // KEPT, rewritten: the page itself — Back and the crumb, the feature head, both headlines in the
        // model's words, the rival named and not adjudicated; the tabs and the miss chip stand down.
        function test_39_theDetailDrawsCausesEffectsAndBothHeadlines() {
            useRegister("Analyse")
            const s = detailSource()
            s.stage = "forming"                         // the miss chip would be inviting on the tabs
            setSource(s)
            const d = viewOf("detail")
            compare(shownAll(body, "sdSessionView").length, 0, "the detail is the view")
            compare(shownAll(body, "sdTab").length, 0, "the tabs stand down over one condition's page")
            compare(shownAll(body, "sdMissChip").length, 0, "and so does the session's opening question")
            verify(shown(one(body, "sdDetailBack")), "with a way back")
            verify(plain(oneShown(body, "sdDetailTitle").text).indexOf("Casting") >= 0, "and the condition named")
            verify(shown(one(d, "sdFeatureHead")))
            compare(plain(oneShown(d, "sdDetailCauseHeadline").text),
                    "Likely driver: Rushed transition — would explain 3 of your patterns.", "a count, never a probability")
            compare(plain(oneShown(d, "sdDetailOutcomeHeadline").text),
                    "Most likely outcome: Slice — 5 of 6 measurable shots.")
            verify(shownAll(d, "sdDetailRival").some(function (t) { return t.text.indexOf("not adjudicated") >= 0 }),
                   "the rival, named and explicitly not adjudicated")
            const f = oneShown(body, "sdDetailFlick")
            const br = f.mapToItem(body, f.width, f.height)
            verify(br.x <= body.width + 1 && br.y <= body.height + 1, "the page scrolls inside the panel")

            probe.width = 396
            laidOut()
            verify(shown(one(body, "sdDetailBack")), "Back never goes, at any width")
        }

        // KEPT, rewritten (D8): BACK asks the model to close, and the view it returns to is the view that
        // was left — the same tab, the same filter, the same scroll (each view is kept alive, never rebuilt).
        function manyCards(n) {
            const out = []
            for (let i = 0; i < n; ++i) {
                const c = patternCards()[0]
                c.id = i === 0 ? "casting" : "c" + i
                c.name = i === 0 ? "Casting" : "Condition " + i
                c.rankText = "#" + (i + 1)
                c.fresh = false
                out.push(c)
            }
            return out
        }
        function test_40_backRestoresThePlaceUntouched() {
            useRegister("Analyse")
            probe.height = 420
            const s = spied(establishedSource(true))
            s.cards = manyCards(9)
            setSource(s)
            showTab("session")
            body.watchingExpanded = true
            mouseClick(shownAll(viewOf("session"), "sdFilterTool")[2])        // Roots
            laidOut()
            const flick = oneShown(body, "sdCardsFlick")
            verify(flick.contentHeight > flick.height, "something to scroll")
            flick.contentY = Math.min(Theme.sp(60), flick.contentHeight - flick.height)
            laidOut()
            const before = { tab: body.tab, filter: body.cardFilter, y: flick.contentY }

            const open = spied(establishedSource(true))
            open.cards = s.cards
            open.detailConditionId = "casting"
            open.detail = detailPayload()
            republish(open)
            verify(shown(viewOf("detail")), "the detail took the view")
            mouseClick(oneShown(body, "sdDetailBack"))
            laidOut()
            compare(probe.closeDetailCalls, 1, "Back asked the model to close")

            // The model answers by clearing detailConditionId; the panel reads it back.
            const back = spied(establishedSource(true))
            back.cards = s.cards
            republish(back)
            compare(shownAll(body, "sdDetailBody").length, 0, "the page is gone")
            compare(body.tab, before.tab, "the same tab")
            compare(body.cardFilter, before.filter, "the same filter")
            verify(oneShown(body, "sdCardsFlick") === flick, "the same view, never rebuilt")
            compare(flick.contentY, before.y, "at the same scroll")
            compare(body.watchingExpanded, true, "and the legacy navigation state untouched")
        }

        // KEPT: a cause node RE-TARGETS the page; there is no stack — Back is one step out.
        function test_42_openingAConditionFromTheDetailRetargetsInPlace() {
            useRegister("Analyse")
            setSource(detailSource())
            clickNode(nodeById("rushed_transition"))
            compare(probe.detailCalls, 1, "the cause node asked for its own page")
            compare(probe.lastDetailId, "rushed_transition")
            compare(probe.closeDetailCalls, 0, "without closing the one it is on")
            mouseClick(oneShown(body, "sdDetailBack"))
            laidOut()
            compare(probe.closeDetailCalls, 1, "Back is one step out of the detail")
        }

        // ═══════════════════════════ how far: the figures ═══════════════════════════

        // KEPT, narrowed: a row carries its figures — the session median, this shot's reading in the warn
        // colour when outside, the unit ONCE — and its strip; the charts are the detail's, never a row's.
        function test_43_aRowShowsItsFiguresAndItsStripNotCharts() {
            useRegister("Analyse")
            setSource(spreadSource())
            showTab("session")
            let row = shownAll(viewOf("session"), "sdSessionRow")[0]
            compare(findAll(row, "sdBoxStrip").length, 0, "no box plot on a row")
            compare(findAll(row, "sdRunChart").length, 0, "and no run chart")
            compare(findAll(oneShown(row, "sdShotStrip"), "sdShotCell").length, 5, "the row keeps its strip")
            compare(oneShown(row, "sdSessionRowMedian").text, "14", "the session median")
            compare(oneShown(row, "sdSessionRowShot").text, "15", "this shot's reading")
            verify(Qt.colorEqual(inkOf(oneShown(row, "sdSessionRowShot")), Theme.colorWarn), "outside, so warn")
            compare(oneShown(row, "sdSessionRowUnit").text, "% hand rise", "the unit")
            compare(shownTexts(row).filter(function (t) { return plain(t.text) === "% hand rise" }).length, 1, "once")

            // An unmeasured shot: "-", never 0, in the quiet ink so it never reads as a clean value.
            const s2 = spreadSource()
            s2.cards[0].spread.currentNumber = "-"
            s2.cards[0].thisShot = "notAssessable"
            setSource(s2)
            row = shownAll(viewOf("session"), "sdSessionRow")[0]
            compare(oneShown(row, "sdSessionRowShot").text, "-", "an unmeasured shot reads '-'")
            verify(Qt.colorEqual(inkOf(oneShown(row, "sdSessionRowShot")), Theme.colorText3), "in the quiet ink3")
        }

        // KEPT, rewritten: the run — a mark per measured shot, a STUB on the foot for the unmeasured one
        // (never a gap), round Y labels, higher drawn higher, the picked shot ringed; a click asks for that
        // swing by its folder.
        function test_44_theDetailRunKeepsTheStripsPromises() {
            useRegister("Analyse")
            setReview(spreadDetailSource(), spreadReadout(3))
            const fr = oneShown(viewOf("detail"), "sdFigureRow")
            const rc = oneShown(fr, "sdRunChart")
            const marks = findAll(rc, "sdRunMark")
            compare(marks.length, 4, "a mark per measured shot")
            const stubs = findAll(rc, "sdRunStub").filter(function (s) { return s.visible })
            compare(stubs.length, 1, "the unmeasured shot is a stub, never a gap")
            verify(stubs[0].height > 0 && stubs[0].height < rc.height / 4, "a SHORT stub")
            let lowest = 0
            for (let i = 0; i < marks.length; ++i) lowest = Math.max(lowest, marks[i].y)
            verify(stubs[0].y + stubs[0].height >= lowest, "standing on the foot of the run")
            const ticks = shownAll(fr, "sdRunYTick").map(function (t) { return t.text })
            verify(ticks.length >= 2, "the run has Y labels: " + ticks.join(" "))
            for (let i = 0; i < ticks.length; ++i)
                verify(["0", "10", "20"].indexOf(ticks[i]) >= 0, "round ones: " + ticks[i])
            function markOf(id) { return marks.filter(function (m) { return m.shotId === id })[0] }
            verify(markOf(1).y > markOf(4).y, "a higher reading is drawn higher")
            verify(markOf(4).ringed, "the shot being read is ringed")
            const sel = one(rc, "sdRunSelected")
            verify(sel.visible)
            compare(sel.shotId, 4)

            intoView(markOf(1))
            mouseClick(markOf(1))
            laidOut()
            compare(probe.lastShotDir, "/lib/a/s/swing_0001", "a click on shot 1's mark asks for shot 1 by folder")
            compare(probe.lastShotId, 1)
        }

        // NEW (12 Oct, Mark: the spread chart became a box plot beside the run): ONE box per figure; its
        // quartiles are the session's by the C++ spreadQuantile convention (type 7) over the dots' f —
        // 0.2, 0.45, 0.5, 1.0 → Q1 0.3875, median 0.475, Q3 0.625; whiskers to the furthest shots within
        // 1.5 × IQR, never inside the box (0.2, and Q3 itself above); shot 5, past the fence, is a fired
        // mark that asks for its swing; the picked shot is a ringed mark on the box; the caption is the
        // session median.
        function test_44b_theBoxSummarisesTheSessionBesideTheRun() {
            useRegister("Analyse")
            setReview(spreadDetailSource(), spreadReadout(3))
            const fr = oneShown(viewOf("detail"), "sdFigureRow")
            const boxes = shownAll(fr, "sdBoxStrip")
            compare(boxes.length, 1, "one box per figure")
            park()
            const bx = boxes[0], rc = oneShown(fr, "sdRunChart")
            verify(bx.mapToItem(fr, 0, 0).x > rc.mapToItem(fr, 0, 0).x + rc.width - 1, "standing to the right of the run")
            verify(bx.width < rc.width / 2, "narrow beside it")
            fuzzyCompare(bx.fQ1, 0.3875, 1e-9, "Q1")
            fuzzyCompare(bx.fMedian, 0.475, 1e-9, "the median")
            fuzzyCompare(bx.fQ3, 0.625, 1e-9, "Q3")
            fuzzyCompare(bx.fLoWhisker, 0.2, 1e-9, "the low whisker reaches the lowest shot inside the fence")
            fuzzyCompare(bx.fHiWhisker, 0.625, 1e-9, "no upper whisker: the nearest shot inside the fence (0.5) is inside the box, and 1.0 is past it")
            function py(f) { return Math.round(bx.scale.y(f) * 2) / 2 }
            const med = one(bx, "sdBoxMedian")
            fuzzyCompare(med.y + med.height / 2, py(0.475), 0.01, "the median rule sits at the median")
            const bb = one(bx, "sdBoxBody")
            fuzzyCompare(bb.y, py(0.625), 0.01, "the box's top is Q3")
            fuzzyCompare(bb.y + bb.height, py(0.3875), 0.01, "its foot is Q1")
            const marks = findAll(bx, "sdBoxMark").filter(function (m) { return m.visible })
            const outs = marks.filter(function (m) { return m.role === "outlier" })
            compare(outs.length, 1, "one shot beyond the whiskers")
            compare(outs[0].shotId, 5, "shot 5")
            verify(outs[0].fired && !outs[0].ringed, "drawn as the fired mark it is")
            const picked = marks.filter(function (m) { return m.role === "picked" })
            compare(picked.length, 1, "the picked shot on the box")
            compare(picked[0].shotId, 4)
            verify(picked[0].ringed, "ringed")
            fuzzyCompare(picked[0].cy, py(0.5), 0.01, "at its value")
            compare(marks.filter(function (m) { return m.role === "hovered" }).length, 0, "nothing hovered, no echo")
            compare(plain(oneShown(fr, "sdBoxCaption").text), "median 14", "the caption: the session median, short")

            intoView(outs[0])
            mouseClick(outs[0])
            laidOut()
            compare(probe.lastShotDir, "/lib/a/s/swing_0005", "a click on the outlier asks for shot 5 by folder")
            compare(probe.lastShotId, 5)
        }

        // KEPT, narrowed: the box's caption, the run's readout line, and across sessions one column per
        // session with the current last — each a small box plot with its MEDIAN labelled; no history
        // published, no columns.
        function test_45_theBoxAndTheHistoryCaptionThemselves() {
            useRegister("Analyse")
            setReview(spreadDetailSource(), spreadReadout(3))
            const fr = oneShown(viewOf("detail"), "sdFigureRow")
            compare(plain(oneShown(fr, "sdBoxCaption").text), "median 14", "the box's caption")
            compare(oneShown(fr, "sdDetailHoverReadout").text, "shot 4 · 13 % hand rise · inside the pass band · fired",
                    "the run's line reads the picked shot")
            const hist = oneShown(fr, "sdHistoryStrip")
            const cols = findAll(hist, "sdHistoryColumn")
            compare(cols.length, 3, "one column per session")
            verify(cols[2].current && !cols[0].current, "this session is the last")
            const meds = findAll(hist, "sdHistoryMedian")
            verify(meds[0].y < meds[2].y, "an older, higher median is drawn higher")
            compare(oneShown(fr, "sdHistoryCaption").text, "7 Oct · median 13", "'median' said once, so nobody reads a mean")
            compare(findAll(hist, "sdHistoryBody").length, 3, "each session a small box plot")
            const labs = findAll(hist, "sdHistoryMedianLabel")
            compare(labs.map(function (l) { return l.text }).join(" "), "22 19 13", "each labelled with its median")
            verify(shown(labs[2]), "this session's median is always labelled")
            verify(Qt.colorEqual(labs[2].color, Theme.colorText), "in ink")
            verify(Qt.colorEqual(labs[0].color, Theme.colorText3), "the others in ink3")
            fuzzyCompare(labs[2].y + labs[2].height / 2, Math.round(hist.scale.y(0.45)), 1, "at the median's height")
            verify(allText(viewOf("detail")).indexOf("the hands came down further outside the path they went up on") >= 0,
                   "what 'higher' means is stated")

            const s = spreadDetailSource()
            s.detail.history = ({})
            republish(s)
            compare(findAll(oneShown(viewOf("detail"), "sdFigureRow"), "sdHistoryColumn").length, 0,
                    "no history published, no columns drawn")
        }

        // NEW (12 Oct, Mark's library: a 9 Sep column with a date and nothing in it): a session with no
        // reading of this measure shows "-" in its column — never a blank, never 0 — draws no box, and
        // the caption names it as not measured; the current session unmeasured says so too.
        function test_45b_aSessionWithNoReadingShowsADash() {
            useRegister("Analyse")
            const s = spreadDetailSource()
            s.detail.history.columns[1].values = []
            s.detail.history.columns[1].n = 0
            s.detail.history.columns[1].medianText = "-"
            setReview(s, spreadReadout(3))
            let fr = oneShown(viewOf("detail"), "sdFigureRow")
            let hist = oneShown(fr, "sdHistoryStrip")
            const cols = findAll(hist, "sdHistoryColumn")
            compare(cols.length, 3, "the column is kept")
            const dash = shownAll(cols[1], "sdHistoryNoReading")
            compare(dash.length, 1, "with a dash in it")
            compare(dash[0].text, "-")
            verify(Qt.colorEqual(dash[0].color, Theme.colorText3), "in ink3")
            const midY = hist.scale.plotTop + hist.scale.plotHeight / 2
            fuzzyCompare(dash[0].y + dash[0].height / 2, midY, 1, "at mid-plot")
            fuzzyCompare(dash[0].x + dash[0].width / 2, hist.width / 2, 1, "centred in its column")
            compare(shownAll(cols[1], "sdHistoryBody").length, 0, "and no box")
            compare(shownAll(cols[0], "sdHistoryNoReading").length + shownAll(cols[2], "sdHistoryNoReading").length, 0,
                    "a measured session has no dash")
            compare(oneShown(fr, "sdHistoryCaption").text, "7 Oct · median 13 · 5 Oct not measured", "the caption says so")

            const s2 = spreadDetailSource()
            s2.detail.history.columns[2].values = []
            s2.detail.history.columns[2].medianText = "-"
            republish(s2, spreadReadout(3))
            fr = oneShown(viewOf("detail"), "sdFigureRow")
            compare(oneShown(fr, "sdHistoryCaption").text, "7 Oct · not measured", "this session unmeasured, in words")
            compare(shownAll(findAll(oneShown(fr, "sdHistoryStrip"), "sdHistoryColumn")[2], "sdHistoryNoReading").length, 1, "and a dash")
        }

        // NEW (12 Oct, Mark: "labels appeared on box plots when you hovered over them — that way the ui
        // is clean but you can examine them with the mouse"). At rest the Spread box carries its median
        // number (the model's medianNumber) in ink beside it; the REAL pointer (mouseMove → its
        // HoverHandler) over the column reveals the full reading — upper whisker end, Q3, median, Q1, lower
        // whisker end, top to bottom, the median in ink — and the counts; leaving hides it. Not interactive
        // or in print: the resting label only, whatever `examined` says.
        function readingTexts(rd) {
            return findAll(rd, "sdBoxReadingLabel").filter(function (l) { return l.visible })
                       .sort(function (a, b) { return a.y - b.y }).map(function (l) { return l.text })
        }
        function test_45c_theSpreadBoxLabelsItsMedianAndReadsOutOnHover() {
            useRegister("Analyse")
            setReview(spreadDetailSource(), spreadReadout(3))
            park()
            const fr = oneShown(viewOf("detail"), "sdFigureRow")
            const bx = oneShown(fr, "sdBoxStrip")
            const rd = one(bx, "sdBoxReading")
            const rest = one(bx, "sdBoxMedianLabel")
            verify(shown(rest), "at rest the median is labelled")
            compare(rest.text, "14", "with the session median the model publishes")
            verify(Qt.colorEqual(rest.color, Theme.colorText), "in ink")
            const med = one(bx, "sdBoxMedian"), body0 = one(bx, "sdBoxBody")
            fuzzyCompare(rest.y + rest.height / 2, med.y + med.height / 2, 1, "at the median's height")
            verify(rest.x >= body0.x + body0.width, "right of the box, clear of the picked shot's ring")
            verify(rest.x + rest.width <= bx.width + Theme.sp(10), "and clear of the zone labels")
            verify(!bx.examining && !rd.visible, "no reading at rest")

            // the real pointer, over the column (not on a mark: the plot's empty middle)
            intoView(bx)
            mouseMove(bx, bx.width / 2, bx.scale.plotTop + bx.scale.plotHeight - 4)
            laidOut()
            verify(bx.examining, "the pointer over the box examines it")
            verify(shown(rd), "the reading is up")
            verify(!shown(rest), "and the resting label steps aside for it")
            // dots f 0.2 0.45 0.5 1.0 on −2…30: upper whisker = Q3 0.625 → 18, Q3 18, median 0.475 → 13,
            // Q1 0.3875 → 10, lower whisker 0.2 → 4
            compare(readingTexts(rd).join(" "), "18 18 13 10 4", "whisker end, Q3, median, Q1, whisker end, top to bottom")
            const labs = findAll(rd, "sdBoxReadingLabel").filter(function (l) { return l.visible })
            const medLab = labs.filter(function (l) { return Math.abs(l.f - 0.475) < 1e-9 })[0]
            verify(Qt.colorEqual(medLab.color, Theme.colorText), "the median in ink")
            verify(Qt.colorEqual(labs.filter(function (l) { return Math.abs(l.f - 0.3875) < 1e-9 })[0].color, Theme.colorText2), "the rest in ink2")
            compare(one(rd, "sdBoxReadingCount").text, "4 shots · 1 outlier", "the counts")
            for (let i = 1; i < rd.placed.length; ++i)
                verify(rd.placed[i].ly - rd.placed[i - 1].ly >= rd.lineH - 0.01, "two labels at one value are nudged apart")
            for (let i = 0; i < rd.placed.length; ++i)
                fuzzyCompare(rd.placed[i].y, bx.scale.y(rd.placed[i].f), 0.01, "each tick starts at its true height")
            compare(probe.shotCalls, 0, "examining selects nothing")
            compare(bx.brush.hoveredShot, -1, "and hovers no shot")

            park()
            verify(!bx.examining && !rd.visible, "leaving hides the reading")
            verify(shown(rest), "and the median label is back")

            // not interactive: the pointer examines nothing
            bx.interactive = false
            mouseMove(bx, bx.width / 2, bx.scale.plotTop + bx.scale.plotHeight - 4)
            laidOut()
            verify(!bx.examining && !rd.visible, "not interactive: no reading on hover")
            park()
            bx.interactive = true
            // print: the resting label only, even when asked to examine
            bx.examined = true
            laidOut()
            verify(bx.examining && rd.visible, "examined on screen: the reading (the render switch)")
            bx.medium = "print"
            laidOut()
            verify(!bx.examining && !rd.visible, "in print: no reading")
            verify(rest.visible, "the resting label only")
            bx.medium = "screen"
            bx.examined = false
            laidOut()
        }

        // NEW (12 Oct): a session's mini box reads out the same way. The real pointer over a column
        // examines that session; the other columns' resting labels the reading would cross step aside;
        // the counts are that session's; an unmeasured session cannot be examined; not interactive / in
        // print, no reading.
        function test_45d_aHistoryBoxReadsOutOnHover() {
            useRegister("Analyse")
            setReview(spreadDetailSource(), spreadReadout(3))
            park()
            const fr = oneShown(viewOf("detail"), "sdFigureRow")
            const hist = oneShown(fr, "sdHistoryStrip")
            const rd = one(hist, "sdBoxReading")
            verify(!hist.examining && !rd.visible, "no reading at rest")
            intoView(hist)
            mouseMove(hist, 0.5 * hist.colW - Theme.sp(10), hist.scale.plotTop + hist.scale.plotHeight - 4)
            laidOut()
            compare(hist.examIdx, 0, "the pointer over 4 Jul's column examines it")
            verify(shown(rd), "its reading is up")
            // 4 Jul: shots 0.7 0.75 0.8, Q1 0.7 Q3 0.8 median 0.75 on −2…30 → 24 24 22 20 20
            compare(readingTexts(rd).join(" "), "24 24 22 20 20", "that session's five values, top to bottom")
            compare(one(rd, "sdBoxReadingCount").text, "3 shots", "its count, and no outliers named")
            const rests = findAll(hist, "sdHistoryMedianLabel")
            verify(!rests[0].visible, "its own resting label steps aside")
            for (let i = 1; i < rests.length; ++i)
                verify(!rests[i].visible || rests[i].x >= rd.x1 || rests[i].x + rests[i].width <= rd.x0,
                       "no resting label left under the reading (" + rests[i].text + ")")

            mouseMove(hist, 1.5 * hist.colW - Theme.sp(10), hist.scale.plotTop + hist.scale.plotHeight - 4)
            laidOut()
            compare(hist.examIdx, 1, "moving on examines the next session")
            park()
            compare(hist.examIdx, -1, "leaving examines none")
            verify(!rd.visible)

            // not interactive / print
            hist.interactive = false
            mouseMove(hist, 0.5 * hist.colW, hist.scale.plotTop + hist.scale.plotHeight - 4)
            laidOut()
            compare(hist.examIdx, -1, "not interactive: no reading on hover")
            park()
            hist.interactive = true
            hist.examinedColumn = 2
            laidOut()
            compare(hist.examIdx, 2, "the render switch examines a column")
            hist.medium = "print"
            laidOut()
            compare(hist.examIdx, -1, "in print: no reading")
            verify(!rd.visible)
            hist.medium = "screen"
            hist.examinedColumn = -1
            laidOut()

            // an unmeasured session cannot be examined
            const s = spreadDetailSource()
            s.detail.history.columns[1].values = []
            s.detail.history.columns[1].medianText = "-"
            republish(s, spreadReadout(3))
            const h2 = oneShown(oneShown(viewOf("detail"), "sdFigureRow"), "sdHistoryStrip")
            h2.examinedColumn = 1
            laidOut()
            compare(h2.examIdx, -1, "an unmeasured session has nothing to read out")
            verify(!one(h2, "sdBoxReading").visible)
            h2.examinedColumn = -1
            intoView(h2)
            mouseMove(h2, 1.5 * h2.colW, h2.scale.plotTop + h2.scale.plotHeight / 2)
            laidOut()
            compare(h2.examIdx, -1, "not even under the pointer")
            park()
        }

        // NEW (12 Oct): the reading's placement and nudging, on its own. Right of the box by default; left
        // when the right lacks the room, or when it would run into a zone label at the reading's heights;
        // right again when neither side fits; labels closer than a line pushed apart as a group, each tick
        // starting at its true height.
        function test_45e_theBoxReadingPlacesItself() {
            const host = Qt.createQmlObject('import QtQuick; import PinPointStudio; Item { width: 300; height: 230; '
                + 'property alias rd: r; property alias sc: s; '
                + 'PpFigureScale { id: s; plotTop: 10; plotHeight: 200; axisLo: 0; axisHi: 100 } '
                + 'PpBoxReading { id: r; anchors.fill: parent; roles: PpDiagRoles {} edgeL: 100; edgeR: 110; '
                + 'items: [0.9, 0.75, 0.5, 0.49, 0.1].map(function (f) { return { y: s.y(f), text: String(Math.round(s.valueOf(f))), f: f, strong: f === 0.5 } }); '
                + 'footer: "5 shots" } }', body)
            const rd = host.rd, sc = host.sc
            wait(0)
            compare(rd.side, "right", "right of the box by default")
            compare(readingTexts(rd).join(" "), "90 75 50 49 10", "every value at the axis's precision, top to bottom")
            let labs = findAll(rd, "sdBoxReadingLabel")
            for (let i = 0; i < labs.length; ++i) verify(labs[i].x >= rd.edgeR, "labels right of the box")
            for (let i = 1; i < rd.placed.length; ++i)
                verify(rd.placed[i].ly - rd.placed[i - 1].ly >= rd.lineH - 0.01, "no two labels closer than a line")
            for (let i = 0; i < rd.placed.length; ++i)
                fuzzyCompare(rd.placed[i].y, sc.y(rd.placed[i].f), 0.01, "ticks start at the true heights")
            const mid = rd.placed.filter(function (q) { return q.f === 0.5 || q.f === 0.49 })
            verify(Math.abs((mid[0].ly + mid[1].ly) / 2 - (mid[0].y + mid[1].y) / 2) < 0.01, "a nudged pair stays centred on its values")

            rd.roomRight = 130
            wait(0)
            compare(rd.side, "left", "short of room on the right: left")
            labs = findAll(rd, "sdBoxReadingLabel")
            for (let i = 0; i < labs.length; ++i) verify(labs[i].x + labs[i].width <= rd.edgeL, "labels left of the box")
            rd.roomLeft = 90
            wait(0)
            compare(rd.side, "right", "neither side fits: right, the default")
            rd.roomLeft = -1e6; rd.roomRight = 1e6
            rd.gutterX = 120; rd.gutterYs = [sc.y(0.5)]
            wait(0)
            compare(rd.side, "left", "a zone label at the reading's heights in the gutter: left")
            rd.gutterYs = [sc.y(0.5) + 1000]
            wait(0)
            compare(rd.side, "right", "a zone label clear of its heights: right")
            rd.forceSide = "left"
            wait(0)
            compare(rd.side, "left", "a caller may force the side")
            host.destroy()
        }

        // KEPT, rewritten (F53): one hovered shot, lit in every chart, and the run's line READS it; an
        // unmeasured shot hovered elsewhere reads "-"; leaving restores the picked shot; a hover selects
        // nothing.
        function test_47_aHoverLightsTheSameShotEverywhereAndTheLineReadsIt() {
            useRegister("Analyse")
            setReview(spreadDetailSource(), spreadReadout(3))
            const fr = oneShown(viewOf("detail"), "sdFigureRow")
            const strip = oneShown(fr, "sdBoxStrip"), rc = oneShown(fr, "sdRunChart")
            const line = oneShown(fr, "sdDetailHoverReadout")
            function boxMark(id) { return findAll(strip, "sdBoxMark").filter(function (m) { return m.visible && m.shotId === id })[0] }
            // a shot inside the box, hovered in the run, lights on the box's centre line at its value
            const mk3 = findAll(rc, "sdRunMark").filter(function (m) { return m.shotId === 3 })[0]
            intoView(mk3)
            mouseMove(mk3, 0, 0)
            laidOut()
            compare(strip.brush.hoveredShot, 3, "the pointer on shot 3's run mark hovers shot 3")
            verify(mk3.hovered, "lit in the run")
            const echo = boxMark(3)
            verify(!!echo && echo.hovered && echo.role === "hovered", "and on the box, together")
            fuzzyCompare(echo.cy, Math.round(strip.scale.y(0.2) * 2) / 2, 0.01, "at its value")
            compare(line.text, "shot 3 · 13 % hand rise · inside the pass band · clean", "and the line reads it")

            // the box's own mark (shot 5, past the whisker) hovers its shot in the run
            const out5 = boxMark(5)
            intoView(out5)
            mouseMove(out5, 0, 0)
            laidOut()
            compare(strip.brush.hoveredShot, 5, "the pointer on the box's outlier hovers shot 5")
            verify(out5.hovered, "lit on the box")
            verify(findAll(rc, "sdRunMark").filter(function (m) { return m.shotId === 5 })[0].hovered, "and in the run")
            verify(!boxMark(3), "the echo of shot 3 is gone")

            park()
            strip.brush.hoverShot(2, true)               // shot 2, hovered on a row's strip or the ruler
            laidOut()
            compare(line.text, "shot 2 · - · not measured", "an unmeasured shot reads '-'")
            strip.brush.hoverShot(2, false)
            laidOut()
            compare(line.text, "shot 4 · 13 % hand rise · inside the pass band · fired", "leaving restores the picked shot")
            compare(probe.shotCalls, 0, "hovering selects nothing")
        }

        // KEPT, rewritten: a republish (a pick in the carousel, a shot landing) moves every shot-dependent
        // part of the open page — and a hover in progress SURVIVES it (D8: no demo code runs on a republish).
        function detailAt(idx, extraShot) {
            const s = spreadDetailSource()
            const sp = s.detail.header.spread
            if (extraShot) {
                sp.run.push({ index: 5, shotId: 6, swingDir: "/lib/a/s/swing_0006", state: "clean",
                              assessable: true, fx: 0.95, fy: 0.15, clipped: 0, current: false, selected: false,
                              valueText: "3 % hand rise", readout: "shot 6 · 3 % hand rise · inside the pass band · clean" })
                for (let i = 0; i < 5; ++i) sp.run[i].fx = (i + 0.5) / 6
                sp.dots.push({ index: 5, shotId: 6, swingDir: "/lib/a/s/swing_0006", f: 0.15, clipped: 0, stack: 1,
                               state: "clean", current: false, recent: true, valueText: "3 % hand rise" })
                s.detail.header.ticks = ticksOf(["fired", "notAssessable", "clean", "fired", "fired", "clean"])
                // and the model republishes its box: f 0.15 0.2 0.45 0.5 1.0 → Q1 0.2, median 0.45, Q3 0.5, 1.0 past the fence
                sp.box = { n: 5, fLo: 0.15, fQ1: 0.2, fMed: 0.45, fQ3: 0.5, fHi: 0.5, outliers: [5], outlierCount: 1,
                           texts: { lo: "3", q1: "4", med: "12", q3: "14", hi: "14" } }
            }
            for (let i = 0; i < sp.run.length; ++i) { sp.run[i].current = i === idx; sp.run[i].selected = i === idx }
            for (let i = 0; i < sp.dots.length; ++i) sp.dots[i].current = sp.dots[i].index === idx
            sp.currentReadout = sp.run[idx].readout
            return s
        }
        function test_48_theOpenDetailFollowsTheCurrentShot() {
            useRegister("Analyse")
            setReview(detailAt(3, false), spreadReadout(3))
            function fr() { return oneShown(viewOf("detail"), "sdFigureRow") }
            function ringedDot() {
                const ds = findAll(oneShown(fr(), "sdBoxStrip"), "sdBoxMark").filter(function (d) { return d.visible && d.ringed })
                return ds.length === 1 ? ds[0].shotId : -1
            }
            function check(idx, tag) {
                verify(shown(viewOf("detail")), tag + ": the detail is still open")
                const sel = one(oneShown(fr(), "sdRunChart"), "sdRunSelected")
                const measured = idx !== 1
                compare(sel.visible, measured, tag + ": the run's picked mark")
                if (measured) compare(sel.shotId, idx + 1, tag + ": on the right shot")
                compare(ringedDot(), measured ? idx + 1 : -1, tag + ": the box's ringed mark")
            }
            check(3, "shot 4 picked")
            republish(detailAt(2, false), spreadReadout(2))
            check(2, "shot 3 picked")
            compare(oneShown(fr(), "sdDetailHoverReadout").text, "shot 3 · 13 % hand rise · inside the pass band · clean",
                    "the line follows the pick")
            republish(detailAt(1, false), spreadReadout(1))
            check(1, "the unmeasured shot picked")

            // A HOVER IN PROGRESS survives a republish.
            republish(detailAt(3, false), spreadReadout(3))
            const strip = oneShown(fr(), "sdBoxStrip")
            const mk3 = findAll(oneShown(fr(), "sdRunChart"), "sdRunMark").filter(function (d) { return d.shotId === 3 })[0]
            intoView(mk3)
            mouseMove(mk3, 0, 0)
            laidOut()
            compare(strip.brush.hoveredShot, 3, "hovering shot 3")
            republish(detailAt(0, false), spreadReadout(0))
            compare(oneShown(fr(), "sdBoxStrip").brush.hoveredShot, 3, "the hover survives the shot changing under it")
            const echo = findAll(oneShown(fr(), "sdBoxStrip"), "sdBoxMark").filter(function (m) { return m.visible && m.shotId === 3 })[0]
            verify(!!echo && echo.hovered, "still lit on the box")
            compare(oneShown(fr(), "sdDetailHoverReadout").text, "shot 3 · 13 % hand rise · inside the pass band · clean",
                    "and the line still reads it")
            park()

            // LIVE: a sixth shot lands and the page, following the newest, moves onto it.
            republish(detailAt(5, true), spreadReadout(5))
            compare(findAll(oneShown(fr(), "sdRunChart"), "sdRunMark").length, 5, "the run grows by one")
            const bx6 = oneShown(fr(), "sdBoxStrip")
            fuzzyCompare(bx6.fMedian, 0.45, 1e-9, "and the box takes it in: the median moves")
            compare(findAll(bx6, "sdBoxMark").filter(function (m) { return m.visible && m.ringed }).map(function (m) { return m.shotId }).join(), "6",
                    "the new shot ringed on the box")
            compare(one(oneShown(fr(), "sdRunChart"), "sdRunSelected").shotId, 6, "on the new shot")
            compare(oneShown(fr(), "sdDetailHoverReadout").text, "shot 6 · 3 % hand rise · inside the pass band · clean")
        }

        // ═══════════════════════════ review (13a) ═══════════════════════════

        // KEPT: review states its tense — the badge on the frame, the review note (on the tab row, or at
        // the foot of the view), and the session count heads Session.
        function test_21_reviewStatesItsTense() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setReview(spied(reviewSource()), reviewReadout())
                verify(shown(one(body, "sdReviewBadge")), r + ": the reviewing badge is up")
                verify(body.cardAside.toLowerCase().indexOf("reviewing") >= 0, r + ": and in the stage card's aside")
                verify(!shown(one(body, "sdShotLabel")), r + ": the badge names the shot, so the label does not repeat it")
                showTab("shot")
                const notes = shownAll(body, "sdReviewNote").map(function (n) { return plain(n.text).toLowerCase() })
                verify(notes.some(function (t) { return t.indexOf("final session state") === 0 }),
                       r + ": the review note is stated: " + notes.join(" / "))
                showTab("session")
                verify(headText(oneShown(viewOf("session"), "sdSessionHead")).indexOf("14 shots") >= 0,
                       r + ": the session count says what it counts over")
            }
        }

        // KEPT, rewritten: review with nothing picked is the finished session — This shot says to pick one
        // in the carousel (F19, the review tense), the rows keep their bookends; a pick swaps only This shot.
        function test_22_reviewWithNoShotSelectedIsStillTheFinishedSession() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                body.tab = "session"                    // the previous register's pick left the reader on This shot
                setReview(spied(reviewSource()), null)
                verify(shown(one(body, "sdReviewBadge")), r + ": the tense is still stated")
                compare(body._tab, "session", r + ": review rests on the session")
                showTab("shot")
                const e = oneShown(viewOf("shot"), "sdShotEmpty")
                verify(allText(e).indexOf("Pick a shot in the carousel to read it here.") >= 0, r + ": " + allText(e))
                verify(allText(e).toLowerCase().indexOf("next swing") < 0, r + ": no swing is coming in review")
                compare(shownAll(viewOf("shot"), "sdDotRow").length, 0)
                if (r === "Analyse") {
                    showTab("session")
                    compare(oneShown(shownAll(viewOf("session"), "sdSessionRow")[0], "sdSessionRowTypical").text, "7",
                            "the finished session's bookends hold their place")
                }
                probe.readout = reviewReadout()
                laidOut()
                compare(body._tab, "shot", r + ": the pick opens This shot")
                verify(shownAll(viewOf("shot"), "sdDotRow").length > 0, r + ": and reads it")
                verify(!shown(one(viewOf("shot"), "sdShotEmpty")))
            }
        }

        // KEPT, rewritten: every condition measured on the shot is on the plot; the ones the capture could
        // not answer are NAMED with their reasons; the true minus survives; the session tier is beside each.
        // (And old test_34's surviving claim: in the glance, what fired comes first.)
        function test_23_reviewReadsEveryConditionAndNeverABlankSlot() {
            useRegister("Analyse")
            setReview(spied(reviewSource()), reviewReadout())
            showTab("shot")
            const v = viewOf("shot")
            const dots = shownAll(v, "sdDotRow")
            compare(dots.length, 7, "every measured condition on one scale")
            compare(dots.filter(function (d) { return d.stateKind === "fired" }).length, 5)
            compare(dots.filter(function (d) { return d.stateKind === "clean" }).length, 2)
            const tiers = dots.map(function (d) { return d.tierTag })
            verify(tiers.indexOf("pattern") >= 0 && tiers.indexOf("watching") >= 0, "the session tier beside the read")
            const nm = plain(oneShown(v, "sdNotMeasuredNote").text)
            verify(nm.indexOf("X-factor — ball not tracked") >= 0, "the unmeasured are named with the reason: " + nm)
            verify(nm.indexOf("Head height Δ — shaft occluded at P6") >= 0)
            const casting = shownAll(v, "sdFiredRow").filter(function (r) { return r.conditionId === "casting" })[0]
            compare(oneShown(casting, "sdFiredRowValue").text, "−4.4", "U+2212, not a hyphen")
            const tierCells = shownAll(v, "sdFiredRowTier").map(function (t) { return t.text })
            verify(tierCells.indexOf("pattern") >= 0 && tierCells.indexOf("watching") >= 0, "the ledger's tier column")

            useRegister("Replay")
            showTab("shot")
            const order = shownAll(viewOf("shot"), "sdDotRow").map(function (d) { return d.stateKind })
            verify(order.length >= 5, "the glance draws at least the firings")
            const firstClean = order.indexOf("clean")
            verify(firstClean < 0 || order.lastIndexOf("fired") < firstClean, "what fired comes first: " + order.join(","))
            compare(order.indexOf("notAssessable"), -1, "the unmeasured are counted, not plotted")
            verify(plain(oneShown(viewOf("shot"), "sdQuietCount").text).indexOf("2 not measurable") >= 0,
                   "and the count is there")
        }

        // KEPT, rewritten: the reviewed shot is ONE picked cell per strip, its bar not recoloured, and the
        // pick a column down the ledger at exactly that shot.
        function test_24_theSelectedShotIsThePickColumn() {
            useRegister("Analyse")
            setReview(spied(formingReviewSource()), sixShotReadout())
            showTab("session")
            const table = oneShown(viewOf("session"), "sdSessionTable")
            const rows = shownAll(table, "sdSessionRow")
            const cells = findAll(oneShown(rows[0], "sdShotStrip"), "sdShotCell")
            const picked = cells.filter(function (c) { return c.picked })
            compare(picked.length, 1, "exactly one picked cell")
            compare(picked[0].shotId, 6, "the reviewed shot")
            verify(Qt.colorEqual(marksOf(picked[0]).fired.color, Theme.colorWarn),
                   "not recoloured: the bar still says it fired")
            const col = one(table, "sdPickColumn")
            verify(col.visible, "the pick is a column through the table")
            verify(Math.abs(col.x - picked[0].mapToItem(table, 0, 0).x) <= 1, "at that shot's x")
        }

        // KEPT: the review tense on the rows ("3 more firings after this shot") and, live, the recency
        // (F29); the session counts untouched by the selection.
        function test_25_rowsSayWhatHappenedHereAndAfter() {
            useRegister("Analyse")
            setReview(spied(reviewSource()), reviewReadout())
            showTab("session")
            let rows = shownAll(viewOf("session"), "sdSessionRow")
            verify(one(rows[0], "sdSessionRowLine2").text.indexOf("3 more firings after this shot") >= 0,
                   "was this the end of it, or the middle")
            verify(one(rows[1], "sdSessionRowLine2").text.indexOf("No firings after this shot") >= 0)
            compare(one(rows[0], "sdSessionRowFired").text, "4/5", "the session count, untouched by the selection")

            probe.readout = null
            setSource(spied(formingSource(false)))
            rows = shownAll(viewOf("session"), "sdSessionRow")
            verify(one(rows[1], "sdSessionRowLine2").text.indexOf("Last fired 1 measurable shots ago") >= 0,
                   "live, the recency: " + one(rows[1], "sdSessionRowLine2").text)
        }

        // KEPT unchanged: the carousel's pip row (outside the panel).
        function test_26_thePipRowIsTheSwingsWholeReadAtCarouselSize() {
            probe.pipFixture = [
                { id: "a", state: "fired" }, { id: "b", state: "clean" },
                { id: "c", state: "notAssessable" }, { id: "d", state: "fired" },
                { id: "e", state: "clean" }, { id: "f", state: "fired" },
                { id: "g", state: "clean" }, { id: "h", state: "notAssessable" },
                { id: "i", state: "fired" }
            ]
            probe.pipFired = 4
            wait(0)
            const pips = findAll(pipProbe, "sdPip")
            compare(pips.length, 9, "one pip per tracked condition, and never a gap")
            const fired = pips.filter(p => p.fired)
            const na    = pips.filter(p => p.notAssessable)
            compare(fired.length, 4)
            compare(na.length, 2)
            compare(fired[0].color, Theme.colorWarn)
            compare(pips[1].color, Theme.colorGood)
            verify(na[0].height < fired[0].height, "the unmeasured pip is shorter")
            compare(na[0].border.width, 1, "and outlined rather than filled")
            const count = one(pipProbe, "sdPipCount")
            compare(count.text, "4 fired")
            compare(count.color, Theme.colorWarn)
            probe.pipFired = 0
            wait(0)
            compare(count.color, Theme.colorGood, "a swing that fired nothing says so in green")
        }

        // KEPT: a session that never established is reviewed as flat rows with their full runs — no chain
        // drawn over them — and the reviewed shot one picked cell in each.
        function test_36_aFormingSessionKeepsItsRowsWhenItClosesAndIsReviewed() {
            useRegister("Analyse")
            setReview(spied(formingReviewSource()), sixShotReadout())
            showTab("session")
            compare(body.header.reachedEstablished, false, "the recorded stage is Forming")
            compare(shownAll(body, "sdCauseGraph").length + shownAll(body, "sdPathChains").length, 0, "no chain drawn")
            const rows = shownAll(viewOf("session"), "sdSessionRow")
            compare(rows.length, 2, "both patterns get a row")
            for (let i = 0; i < rows.length; ++i) {
                const cells = findAll(oneShown(rows[i], "sdShotStrip"), "sdShotCell")
                compare(cells.length, 6, "row " + i + ": six shots, six cells")
                verify(cells[0].height >= 6, "row " + i + ": drawn at a readable size")
                compare(cells.filter(function (c) { return c.picked }).length, 1, "row " + i + ": the reviewed shot picked")
            }
        }

        // ═══════════════════════════ the declared miss (F66-F67) ═══════════════════════════

        // NEEDS-NEW-DESIGN → rewritten: a quiet link at the right of the tab row — it invites in Cold and
        // Forming, stands down from Established, reports a declaration by name at any stage, stands down on
        // the detail and on the cast, and never invites on a surface nobody can press.
        function test_30_theMissChipInvitesInColdAndFormingAndReportsOnceDeclared() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setSource(spied(coldSource(probe.expectationRows)))
                compare(plain(oneShown(body, "sdMissChip").text), "Declare miss", r + ": Cold asks the opening question")
                setSource(spied(formingSource(false)))
                compare(plain(oneShown(body, "sdMissChip").text), "Declare miss", r + ": so does Forming")
                setSource(spied(establishedSource(true)))
                compare(shownAll(body, "sdMissChip").length, 0, r + ": Established does not ask again")
                const s = spied(establishedSource(true))
                s.declaredMiss = "slice"
                s.declaredMissName = "Slice"
                setSource(s)
                compare(plain(oneShown(body, "sdMissChip").text), "Miss: Slice", r + ": a declaration is reported, by name")
                body.interactive = false
                setSource(spied(formingSource(false)))
                compare(shownAll(body, "sdMissChip").length, 0, r + ": no invitation on a surface nobody can press")
                body.interactive = true
            }
            useRegister("Analyse")
            const d = detailSource()
            d.stage = "forming"
            setSource(d)
            compare(shownAll(body, "sdMissChip").length, 0, "it stands down over one condition's page")
            body.surface = "cast"
            setSource(spied(formingSource(false)))
            compare(shownAll(body, "sdMissChip").length, 0, "and on the cast")
        }

        // NEEDS-NEW-DESIGN → rewritten: the picker opens INLINE under the tab row — inside the panel, never a
        // pop-up — lists the model's candidates (the current one marked), declares by id, closes on an
        // answer, and "No declared miss" clears with declareMiss("").
        function test_31_theMissPickerOpensInPlaceAndDeclaresOne() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                probe.missCalls = 0
                setSource(spied(formingSource(false)))
                showTab("session")
                compare(shownAll(body, "sdMissPicker").length, 0, r + ": closed until asked for")
                const y0 = oneShown(body, "sdCardsFlick").mapToItem(body, 0, 0).y
                mouseClick(oneShown(body, "sdMissChip"))
                laidOut()
                const pk = oneShown(body, "sdMissPicker")     // shown() walks up to the body: in the panel's own tree
                verify(oneShown(body, "sdCardsFlick").mapToItem(body, 0, 0).y > y0, r + ": it opens in place and pushes the view down")
                compare(plain(one(pk, "sdMissPickerLabel").text), "What is the bad shot?", r + ": it states its question")
                const rows = shownAll(pk, "sdMissCandidate")
                compare(rows.map(function (c) { return c.missId }).join(","), "slice,hook,thin_strike", r + ": the model's candidates")
                mouseClick(rows[1])
                laidOut()
                compare(probe.missCalls, 1)
                compare(probe.lastMissId, "hook", r + ": the row's id, not its label")
                compare(shownAll(body, "sdMissPicker").length, 0, r + ": answering closes it")

                const s = spied(formingSource(false))
                s.declaredMiss = "hook"
                s.declaredMissName = "Hook"
                setSource(s)
                compare(plain(oneShown(body, "sdMissChip").text), "Miss: Hook")
                mouseClick(oneShown(body, "sdMissChip"))
                laidOut()
                const marked = shownAll(body, "sdMissCandidate").filter(function (c) { return c.on })
                compare(marked.length, 1, r + ": the current one is marked")
                compare(marked[0].missId, "hook")
                mouseClick(oneShown(body, "sdMissClear"))
                laidOut()
                compare(probe.lastMissId, "", r + ": declareMiss('') is the clear")
            }
        }

        // ═══════════════════════════ the after-shot pulse (D1) ═══════════════════════════

        function rowFor(r, id) {
            const v = viewOf("session")
            const rows = r === "Analyse" ? shownAll(v, "sdSessionRow") : replaySessionItems(v)
            return rows.filter(function (x) { return x.cardId === id })[0]
        }
        function washOf(item) { return findAll(item, "sdPulseWash")[0] }

        // NEEDS-NEW-DESIGN → rewritten (D1): after a surfaced shot, the rows that fired get ONE soft wash,
        // the focused condition leading; a condition that did not fire does not move. Both registers.
        function test_32_thePulseWashesWhatFiredAndLeadsWithTheFocus() {
            for (const r of ["Analyse", "Replay"]) {
                useRegister(r)
                body.pulseCue = null
                const s = spied(formingSource(false))
                s.afterShotDelta.fired = ["face_roll", "casting"]
                s.afterShotDelta.focusId = "casting"
                s.cards[0].focused = true
                s.focusConditionId = "casting"
                setSource(s)
                showTab("session")
                verify(!washOf(rowFor(r, "casting")).pulsing, r + ": nothing moves before a shot lands")

                body.pulseAfterShot()
                wait(0)
                verify(!!body.pulseCue, "a cue was published")
                compare(body.pulseCue.ids[0], "casting", "the focus leads")
                compare(body.pulseCue.ids.length, 2)
                const cw = washOf(rowFor(r, "casting")), fw = washOf(rowFor(r, "face_roll"))
                compare(cw.order, 0, r + ": the focused pattern leads the stagger")
                compare(fw.order, 1)
                verify(cw.pulsing && fw.pulsing, r + ": both rows that fired wash once")

                const s2 = spied(formingSource(false))
                s2.afterShotDelta.fired = ["casting"]
                setSource(s2)
                body.pulseAfterShot()
                wait(0)
                verify(washOf(rowFor(r, "casting")).pulsing, r + ": the one that fired washes")
                verify(!washOf(rowFor(r, "face_roll")).pulsing, r + ": the one that did not stays put")
                compare(washOf(rowFor(r, "face_roll")).order, -1)
            }
        }

        // NEEDS-NEW-DESIGN → rewritten: reduceMotion is NO pulse — not a shorter one, and nothing deferred.
        function test_33_thePulseHonoursReduceMotion() {
            for (const r of ["Analyse", "Replay"]) {
                useRegister(r)
                const s = spied(formingSource(false))
                s.afterShotDelta.fired = ["casting", "face_roll"]
                setSource(s)
                showTab("session")
                Theme.reduceMotion = true
                wait(0)
                body.pulseAfterShot()
                wait(0)
                for (const id of ["casting", "face_roll"]) {
                    const w = washOf(rowFor(r, id))
                    verify(!w.pulsing, r + "/" + id + ": no animation under reduceMotion")
                    compare(w.level, 0, r + "/" + id + ": and nothing drawn")
                }
                wait(150)
                for (const id of ["casting", "face_roll"])
                    verify(!washOf(rowFor(r, id)).pulsing, r + "/" + id + ": nothing started late")
                Theme.reduceMotion = false
            }
        }

        // ═══════════════════════════ nothing escapes the panel ═══════════════════════════

        // KEPT, rewritten: no region overhangs the panel, and nothing inside a view hangs out of it
        // sideways (below the fold is allowed: that is what the view's scroll is for) — at a full, a
        // narrow and a wide stage, in both registers, on every view, for hand-written and real states.
        function inside(item, top, label) {
            const tl = item.mapToItem(top, 0, 0), br = item.mapToItem(top, item.width, item.height)
            verify(tl.x >= -1 && tl.y >= -1, label + " starts inside")
            verify(br.x <= top.width + 1, label + " ends inside sideways (" + br.x + " > " + top.width + ")")
            return br
        }
        function noOverhang(label) {
            const regions = ["sdRegister", "sdTabBar", "sdCastHead", "sdShotCardsFlick", "sdCardsFlick", "sdWatchingFlick", "sdDetailFlick"]
            for (const n of regions) {
                const items = shownAll(body, n)
                for (let j = 0; j < items.length; ++j) {
                    if (items[j].width <= 0) continue
                    const br = inside(items[j], body, label + ": " + n)
                    verify(br.y <= body.height + 1, label + ": " + n + " fits the panel's height")
                }
            }
            const content = ["sdSessionRow", "sdFiredRow", "sdDotRow", "sdBarcodeRow", "sdSparkRow", "sdSmallMultiple",
                             "sdExpectationCard", "sdShotRuler", "sdShotStrip", "sdCauseGraph", "sdPathChains",
                             "sdFigureRow", "sdFeatureHead", "sdKeyFigures", "sdQuestions"]
            for (const n of content) {
                const items = shownAll(body, n)
                for (let j = 0; j < items.length; ++j) {
                    const f = flickOf(items[j])
                    if (!f || items[j].width <= 0) continue
                    const tl = items[j].mapToItem(f.contentItem, 0, 0)
                    const br = items[j].mapToItem(f, items[j].width, 0)
                    verify(tl.x >= -1 && tl.y >= -1, label + ": " + n + "[" + j + "] starts inside its view")
                    verify(br.x <= f.width + 1, label + ": " + n + "[" + j + "] ends inside its view sideways (" + br.x + " > " + f.width + ")")
                }
            }
        }
        function test_20_nothingOverhangsTheChrome() {
            const sizes = [[1168, 560], [396, 560], [1522, 700]]
            const states = [
                function () { return { source: coldSource(probe.expectationRows), readout: null } },
                function () { return { source: formingSource(false), readout: null } },
                function () { return { source: closingSource(), readout: null } },
                function () { return { source: reviewSource(), readout: reviewReadout() } },
                Real.reviewed, Real.long23, Real.liveNoPick
            ]
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                for (let s = 0; s < states.length; ++s) {
                    applyState(states[s]())
                    for (const sz of sizes) {
                        probe.width = sz[0]; probe.height = sz[1]
                        for (const k of ["shot", "session", "watching"]) {
                            showTab(k)
                            noOverhang(r + " state " + s + " " + k + " at " + sz.join("x"))
                        }
                    }
                }
            }
            useRegister("Analyse")
            for (const mk of [function () { return { source: detailSource(), readout: null } },
                              function () { return Real.reviewed(true) }]) {
                applyState(mk())
                for (const sz of sizes) {
                    probe.width = sz[0]; probe.height = sz[1]
                    laidOut()
                    noOverhang("detail at " + sz.join("x"))
                }
            }
        }

        // ═══════════════════════════ new: what the redesign promises ═══════════════════════════

        // The register is the surface's, then the mode's: the cast is always the glance (D4); Analyse mode
        // is the read; Capture and Replay read at a glance; forceRegister (tests only) pins it. The body
        // hands its state through, and the reader's tab survives a register swap.
        function test_50_theRegisterFollowsTheSurfaceThenTheMode() {
            setSource(spied(formingSource(false)))
            SessionMode.mode = SessionMode.capture
            laidOut()
            compare(reg().register, "Replay", "Capture reads at a glance")
            compare(body.register, "Replay")
            SessionMode.mode = SessionMode.replay
            laidOut()
            compare(reg().register, "Replay", "so does Replay")
            SessionMode.mode = SessionMode.analyse
            laidOut()
            compare(reg().register, "Analyse", "Analyse is the read")
            compare(body.registerItem, reg(), "the body exposes the register it loaded")

            body.tab = "watching"
            laidOut()
            compare(reg().view, "watching")
            SessionMode.mode = SessionMode.replay
            laidOut()
            compare(reg().register, "Replay")
            compare(reg().view, "watching", "the reader's tab survives the swap")
            verify(shown(viewOf("watching")))

            SessionMode.mode = SessionMode.analyse
            body.surface = "cast"
            laidOut()
            compare(reg().register, "Replay", "the cast is the glance whatever the stage is in")
            compare(reg().surface, "cast")
            body.surface = "panel"
            laidOut()
            compare(reg().register, "Analyse")

            body.forceRegister = "Replay"
            laidOut()
            compare(reg().register, "Replay", "a test pins the register over the mode")
            body.surface = "cast"
            body.forceRegister = "Analyse"
            laidOut()
            compare(reg().register, "Analyse", "and over the surface")
            body.forceRegister = ""
            body.surface = "panel"
            laidOut()
            compare(reg().register, "Analyse")

            body.interactive = false
            laidOut()
            compare(reg().interactive, false, "the register is told nobody can press it")
            body.interactive = true
        }

        // A surface nobody can press is DRAWN, never pressed: no tab, row, cell or filter takes a click;
        // the auto-closing cast swallows every press over its whole surface and never flips the stage; a
        // persistent cast keeps its tabs and picks shots, but never drills into a condition (D4).
        function test_51_aSurfaceNobodyCanPressSwallowsEveryClick() {
            useRegister("Analyse")
            body.interactive = false
            setSource(spied(closingSource()))
            compare(body._tab, "session", "nothing to read on This shot: the session is what it shows")
            const v = viewOf("session")
            compare(shownAll(v, "sdFilterTool").length, 0, "no filter to press")
            compare(shownAll(v, "sdSessionRowFocus").length, 0, "no focus to offer")
            const row = shownAll(v, "sdSessionRow")[0]
            clickRow(row)
            compare(probe.detailCalls, 0, "a row opens nothing")
            mouseClick(findAll(oneShown(row, "sdShotStrip"), "sdShotCell")[0])
            laidOut()
            compare(probe.shotCalls, 0, "a cell picks nothing")
            mouseClick(tabByKey("watching"))
            laidOut()
            compare(body.tab, "session", "a tab takes no press")

            // The auto-closing cast.
            SessionMode.mode = SessionMode.analyse
            body.surface = "cast"
            setReview(spied(formingSource(false)), newestReadout())
            compare(reg().register, "Replay")
            compare(body._tab, "shot")
            const dot = shownAll(viewOf("shot"), "sdDotRow")[0]
            intoView(dot)
            mouseClick(dot)
            laidOut()
            compare(probe.detailCalls, 0, "a condition opens nothing from the cast")
            compare(SessionMode.mode, SessionMode.analyse, "and the main stage is never flipped")
            const cell = findAll(oneShown(viewOf("shot"), "sdShotRuler"), "sdRulerCell")[1]
            intoView(cell)
            mouseClick(cell)
            laidOut()
            compare(probe.shotCalls, 0, "a shot column picks nothing on a surface about to vanish")

            // The persistent cast (a coach's second screen).
            body.interactive = true
            laidOut()
            compare(shownAll(body, "sdTab").length, 3, "a persistent cast keeps its tabs")
            showTab("shot")
            const dot2 = shownAll(viewOf("shot"), "sdDotRow")[0]
            intoView(dot2)
            mouseClick(dot2)
            laidOut()
            compare(probe.detailCalls, 0, "but never drills into a condition")
            const cell2 = findAll(oneShown(viewOf("shot"), "sdShotRuler"), "sdRulerCell")[1]
            intoView(cell2)
            mouseClick(cell2)
            laidOut()
            compare(probe.shotCalls, 1, "and still picks shots")
            compare(probe.lastShotId, cell2.shotId)
        }

        // D7: nothing assumes 15 shots. 23 shots (swing id 6 deleted): every strip has a cell per shot and
        // fits its view (narrowing, then wrapping — never clipping); labels are ORDINALS ("shot 8 of 23",
        // never the swing id 9); brushing and picking use the ids.
        function inBarcode(item) {
            let i = item ? item.parent : null
            while (i && i !== body) { if (i.objectName === "sdBarcode") return true; i = i.parent }
            return false
        }
        function test_52_aLongSessionFitsItsStripsAndCountsByOrdinal() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                applyState(Real.long23())
                showTab("shot")
                const h = headText(oneShown(viewOf("shot"), "sdShotHead")).toLowerCase()
                verify(h.indexOf("shot 8 of 23") >= 0, r + ": the ordinal: " + h)
                verify(h.indexOf("shot 9 of 23") < 0, r + ": never the swing id")
                for (const k of ["shot", "session", "watching"]) {
                    showTab(k)
                    const v = viewOf(k), f = flickOf(v)
                    const strips = shownAll(v, "sdShotStrip").filter(function (s) { return !inBarcode(s) })
                    for (let i = 0; i < strips.length; ++i) {
                        const cells = findAll(strips[i], "sdShotCell")
                        compare(cells.length, 23, r + "/" + k + ": strip " + i + " has a cell per shot")
                        for (let c = 0; c < cells.length; ++c)
                            verify(cells[c].mapToItem(f, cells[c].width, 0).x <= f.width + 1,
                                   r + "/" + k + ": strip " + i + " cell " + c + " is inside the view")
                    }
                }
            }
            useRegister("Replay")
            showTab("shot")
            const f = flickOf(viewOf("shot"))
            const cells = findAll(oneShown(viewOf("shot"), "sdShotRuler"), "sdRulerCell")
            compare(cells.length, 23, "the ruler has a column per shot")
            compare(cells[5].shotId, 7, "after the deleted swing the ids skip one")
            verify(cells[7].picked, "the 8th column is the picked shot (id 9)")
            compare(cells[7].shotId, 9)
            verify(cells[22].mapToItem(f, cells[22].width, 0).x <= f.width + 1, "and the ruler fits")

            useRegister("Analyse")
            showTab("session")
            const strip = oneShown(shownAll(viewOf("session"), "sdSessionRow")[0], "sdShotStrip")
            const sc = findAll(strip, "sdShotCell")
            compare(sc[5].shotId, 7)
            compare(sc.filter(function (c) { return c.picked }).map(function (c) { return c.shotId }).join(","), "9",
                    "one picked cell, by id")
        }

        // A zero count is a WORD: "none", "every one", never "no of" / "on no" (R's decision 8).
        function test_53_aZeroCountIsAWordNeverNo() {
            const bad = [/\bno of\b/i, /\bon no\b/i, /\bno are\b/i, /\boutside on no\b/i]
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                applyState(Real.zeroFired())
                for (const k of ["shot", "session", "watching"]) {
                    showTab(k)
                    const ts = shownTexts(reg())
                    for (let i = 0; i < ts.length; ++i) {
                        const p = plain(ts[i].text)
                        for (const re of bad) verify(!re.test(p), r + "/" + k + ": '" + p + "'")
                    }
                }
            }
            useRegister("Replay")
            showTab("shot")
            compare(oneShown(viewOf("shot"), "sdShotHead").title, "Inside every corridor")
            showTab("session")
            verify(headText(oneShown(viewOf("session"), "sdSessionHead")).indexOf("outside on none") >= 0,
                   "the glance: " + headText(oneShown(viewOf("session"), "sdSessionHead")))
            showTab("watching")
            verify(headText(oneShown(viewOf("watching"), "sdWatchingHead")).indexOf("outside on none") >= 0)
            useRegister("Analyse")
            showTab("shot")
            compare(plain(oneShown(viewOf("shot"), "sdFiredEmpty").text), "None of this session's patterns fired on this swing.",
                    "one line, not an empty table")
            showTab("session")
            verify(headText(oneShown(viewOf("session"), "sdSessionHead")).indexOf("on none of them") >= 0,
                   "the read: " + headText(oneShown(viewOf("session"), "sdSessionHead")))
        }

        // F47: Esc closes the open detail in Analyse; not on a surface nobody can press.
        function test_54_escClosesTheDetail() {
            useRegister("Analyse")
            setSource(detailSource())
            const d = viewOf("detail")
            tryVerify(function () { return d.activeFocus }, 2000, "the open detail takes the keyboard")
            keyClick(Qt.Key_Escape)
            laidOut()
            compare(probe.closeDetailCalls, 1, "Esc asked the model to close")
            body.interactive = false
            laidOut()
            d.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            laidOut()
            compare(probe.closeDetailCalls, 1, "but not on a surface nobody can press")
        }

        // HOUSE RULE 2: type never scales. The same text in a 480 and a 1522 panel is the same size.
        function fontMap(item) {
            const ts = shownTexts(item), seen = {}, out = {}
            for (let i = 0; i < ts.length; ++i) {
                const p = plain(ts[i].text)
                if (!p) continue
                seen[p] = (seen[p] || 0) + 1
                out[p] = ts[i].font.pixelSize
            }
            for (const p in seen) if (seen[p] !== 1) delete out[p]
            return out
        }
        function test_55_typeNeverScalesWithThePanel() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                applyState(Real.reviewed())
                for (const k of (r === "Analyse" ? ["shot", "session", "watching"] : ["shot", "watching"])) {
                    showTab(k)
                    probe.width = 480; probe.height = 700
                    laidOut()
                    park()
                    const a = fontMap(reg())
                    const rolesA = one(body, "sdTabBar").roles
                    const bodyA = rolesA.fBody.pixelSize, titleA = rolesA.fTitle.pixelSize
                    probe.width = 1522
                    laidOut()
                    park()
                    const b = fontMap(reg())
                    let common = 0
                    for (const t in a) {
                        if (b[t] === undefined) continue
                        ++common
                        compare(b[t], a[t], r + "/" + k + ": '" + t + "' is the same size in a 480 and a 1522 panel")
                    }
                    verify(common >= 3, r + "/" + k + ": enough text in common to judge (" + common + ")")
                    compare(one(body, "sdTabBar").roles.fBody.pixelSize, bodyA, r + ": the body role")
                    compare(one(body, "sdTabBar").roles.fTitle.pixelSize, titleA, r + ": the title role")
                }
            }
        }

        // HOUSE RULE 12: height is a resource too. Replay's This shot admits more rows (the clean ones, by
        // closeness to their edge) when the panel is taller — never bigger type, never a second block.
        function test_56_aTallerReplayThisShotShowsMoreRows() {
            useRegister("Replay")
            applyState(Real.reviewed())
            showTab("shot")
            probe.height = 560
            laidOut()
            const shortH = shownAll(viewOf("shot"), "sdDotRow").length
            probe.height = 1100
            laidOut()
            const tall = shownAll(viewOf("shot"), "sdDotRow").length
            verify(shortH >= 10, "every firing is drawn at any height (" + shortH + ")")
            verify(tall > shortH, "a taller panel admits more rows (" + shortH + " → " + tall + ")")
            verify(tall <= 76, "and only the measured ones")
        }

        // Nothing is ellipsised: every Text the panel draws either does not elide or is not truncated —
        // both registers, every view and the detail, from narrow to wide.
        function noneElided(label) {
            const ts = shownTexts(reg())
            for (let i = 0; i < ts.length; ++i)
                verify(ts[i].elide === Text.ElideNone || !ts[i].truncated, label + ": '" + plain(ts[i].text) + "' is ellipsised")
        }
        function test_57_noTextIsEllipsised() {
            const widths = [396, 480, 820, 1168, 1522]
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                applyState(Real.reviewed())
                for (const w of widths) {
                    probe.width = w
                    for (const k of ["shot", "session", "watching"]) {
                        showTab(k)
                        noneElided(r + "/" + k + " at " + w)
                    }
                }
            }
            useRegister("Analyse")
            applyState(Real.reviewed(true))
            for (const w of widths) {
                probe.width = w
                laidOut()
                noneElided("detail at " + w)
            }
        }

        // Replaces test_46 (RULES 3: fold, don't drop). A column that does not fit the width moves onto
        // the row's second line, with its word; every row keeps every fact at every width, and nothing a
        // row draws hangs out of it.
        function test_58_aNarrowerLedgerFoldsAndDropsNothing() {
            useRegister("Analyse")
            applyState(Real.reviewed())
            showTab("session")
            const keys = ["Rank", "Name", "Fired", "Shot", "Median", "Trend", "Typical", "Worst", "Best"]
            for (const w of [1522, 820, 480, 396]) {
                probe.width = w
                laidOut()
                const rows = shownAll(viewOf("session"), "sdSessionRow")
                compare(rows.length, 8, w + ": every pattern")
                for (let i = 0; i < rows.length; ++i) {
                    const row = rows[i], tag = w + ": " + row.cardId
                    for (const k of keys)
                        verify(shownAll(row, "sdSessionRow" + k).length >= 1, tag + " keeps its " + k)
                    const median = oneShown(row, "sdSessionRowMedian").text
                    verify(shownAll(row, "sdSessionRowUnit").length >= 1 || median.indexOf(" ") > 0,
                           tag + " keeps its unit (its own cell, or beside the median: '" + median + "')")
                    verify(shownAll(row, "sdShotStrip").length >= 1, tag + " keeps its strip")
                    const ts = shownTexts(row)
                    for (let t = 0; t < ts.length; ++t)
                        verify(ts[t].mapToItem(row, ts[t].width, 0).x <= row.width + 1,
                               tag + ": '" + plain(ts[t].text) + "' stays inside the row")
                }
            }
        }

        // D3: live, with shots but nothing picked, This shot reads the NEWEST swing (from the after-shot
        // delta), never "No shot yet".
        function test_59_liveWithNothingPickedReadsTheNewestSwing() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setSource(spied(formingSource(false)))
                showTab("shot")
                const v = viewOf("shot")
                verify(!shown(one(v, "sdShotEmpty")), r + ": shots exist, so never 'No shot yet'")
                const h = headText(oneShown(v, "sdShotHead")).toLowerCase()
                verify(h.indexOf("newest swing") >= 0, r + ": it says which swing: " + h)
                verify(h.indexOf("shot 5 of 5") >= 0, r + ": by its ordinal")
                compare(shownAll(v, "sdDotRow").filter(function (d) { return d.stateKind === "fired" }).length, 2,
                        r + ": what the newest swing fired")
                if (r === "Replay") {
                    compare(shownAll(v, "sdShotRuler").length, 0, "the newest swing publishes no per-shot run to rule")
                    compare(plain(oneShown(v, "sdQuietCount").text), "1 clean · 2 not measurable", "the rest, as counts")
                } else {
                    compare(shownAll(v, "sdFiredRow").length, 2)
                    compare(shownAll(v, "sdFiredRowValue").length, 0, "no value column the delta cannot fill")
                    verify(shownAll(v, "sdFiredRowOutBy").length >= 1, "but how far out it was")
                }
            }
            useRegister("Replay")
            applyState(Real.liveNoPick())
            showTab("shot")
            verify(headText(oneShown(viewOf("shot"), "sdShotHead")).toLowerCase().indexOf("shot 15 of 15") >= 0,
                   "the real session's newest swing")
        }

        // A detail opened in Replay is read in place: the body loads the Analyse register for it while the
        // stage stays in Replay, and closing it returns to the glance. Only the cast never drills (D9): it
        // keeps the reader on their tab, and a tab press closes the detail first.
        function test_60_replayReadsADetailInPlaceAndOnlyTheCastParksIt() {
            useRegister("Replay")
            const s = detailSource()
            setSource(s)
            compare(body.view, "detail")
            compare(reg().register, "Analyse", "an open detail is read in the Analyse register")
            compare(SessionMode.mode, SessionMode.replay, "without the stage leaving Replay")
            verify(shown(viewOf("detail")), "the detail page is drawn")
            verify(shown(one(body, "sdDetailBack")), "with a way back")
            const held = findAll(body, "sdRegisterHeld")
            compare(held.length, 1, "the glance is held under the detail, not torn down")
            verify(!shown(held[0]), "and hidden")

            const closed = detailSource()
            closed.detailConditionId = ""
            closed.detail = null
            setSource(closed)
            compare(reg().register, "Replay", "closing it returns to the glance")
            verify(reg() === held[0], "the same glance that was held")

            body.surface = "cast"
            setSource(detailSource())
            compare(reg().register, "Replay", "the cast keeps the glance under an open detail")
            compare(findAll(body, "sdDetailBody").length, 0, "no detail page on the cast")
            compare(findAll(body, "sdDetailFlick").length, 0)
            verify(shown(viewOf("session")), "the reader's own tab is under it")
            mouseClick(tabByKey("shot"))
            laidOut()
            compare(probe.closeDetailCalls, 1, "a tab press closes the detail first")
            compare(body.tab, "shot", "then goes to the tab")
            body.surface = "panel"
        }

        // F49: one measure at a time — a tab per measure, the shown one ruled, the pick asked of the model
        // (which keeps it across a rebuild); a condition with one measure draws no tabs.
        // NEW (posture_metrics_design.md §8): a composite condition's page opens on What changed — one line per
        // part, the part not read a "-" — and draws no figure row: a run of the composite would be its number.
        // The coverage is said once, in the trust fold under the table, never on it.
        function test_62_aCompositeOpensOnWhatChangedAndPrintsNoNumberOfItsOwn() {
            useRegister("Analyse")
            setSource(spied(compositeDetailSource("coherent")))
            const d = viewOf("detail")
            const wc = oneShown(d, "sdWhatChanged")
            verify(wc.open, "open on arrival")
            const rows = ["trunk", "pelvisDepth", "pelvisHeight", "knee", "head"].map(function (p) { return oneShown(wc, "sdWhatChangedRow:" + p) })
            compare(rows.length, 5, "one line per part, read or not")
            const t = allText(wc)
            for (const w of ["Address", "Impact", "Change", "Band", "Share", "Forward bend lost", "34°", "25°", "9°",
                             "most of it", "some", "a little", "against it"])
                verify(t.indexOf(w) >= 0, "the table says '" + w + "': " + t)
            const head = allText(rows[4])
            verify(head.indexOf("Head higher") >= 0 && head.indexOf("-") >= 0, "a part not read shows '-': " + head)
            verify(t.indexOf("not measured on this swing") < 0, "its reason is not on the table")
            compare(shownAll(d, "sdFigureRow").length, 0, "no figure row: the composite's run would be its number")
            const fold = oneShown(d, "sdWhatChangedTrust")
            compare(fold.lines[0].text, "Read from 4 of 5 parts: head higher, not measured on this swing.",
                    "the coverage is the trust fold's first line")
            verify(allText(d).indexOf(" z") < 0, "no figure in the composite's own unit anywhere on the page")
            mouseClick(oneShown(wc, "sdWhatChangedToggle"))
            laidOut()
            verify(!wc.open && shownAll(wc, "sdWhatChangedRow:trunk").length === 0, "it folds")
        }

        // NEW: poor setup posture is the same table without a change: the address reading against its ideal band.
        function test_63_theSetupTableReadsAddressAgainstTheIdealBand() {
            useRegister("Analyse")
            setSource(spied(compositeDetailSource("distance")))
            const wc = oneShown(viewOf("detail"), "sdWhatChanged")
            const t = allText(wc)
            verify(t.indexOf("At address") >= 0 && t.indexOf("Ideal") >= 0 && t.indexOf("32 to 48°") >= 0, t)
            verify(t.indexOf("Change") < 0 && t.indexOf("Impact") < 0, "no change column, no second instant: " + t)
        }

        // NEW: Replay's This shot lists What changed under the plot, closed, one line per fired composite.
        function test_64_replayListsWhatChangedForEachFiredComposite() {
            useRegister("Replay")
            const r = spreadReadout(0)
            r.conditions[0].whatChanged = whatChangedFixture("coherent")
            r.conditions[0].composite = true
            r.conditions[0].valueText = "9°"
            setReview(spied(reviewSource()), r)
            showTab("shot")
            const v = viewOf("shot")
            const list = oneShown(v, "sdWhatChangedList")
            const wcs = shownAll(list, "sdWhatChanged")
            compare(wcs.length, 1, "one per fired composite")
            verify(!wcs[0].open && shownAll(wcs[0], "sdWhatChangedRow:trunk").length === 0, "closed in the glance")
            verify(allText(wcs[0]).indexOf("Casting") >= 0, "named after its condition")
            mouseClick(oneShown(wcs[0], "sdWhatChangedToggle"))
            laidOut()
            verify(wcs[0].open && shownAll(wcs[0], "sdWhatChangedRow:trunk").length === 1, "and opens in place")
        }

        function test_61_theDetailsMeasureTabsPickOneMeasureAtATime() {
            useRegister("Analyse")
            applyState(Real.reviewed(true))
            const det = Real.detail()
            const d = viewOf("detail")
            const tabs = shownAll(oneShown(d, "sdDetailMeasureTabs"), "sdDetailMeasureTab")
            compare(tabs.length, det.measures.length, "a tab per measure")
            const on = tabs.filter(function (t) { return t.on })
            compare(on.length, 1, "one measure shown")
            compare(on[0].measureId, det.measureId, "the model's pick")
            const other = tabs.filter(function (t) { return !t.on })[0]
            intoView(other)
            mouseClick(other)
            laidOut()
            compare(probe.measureCalls, 1, "the press asks the model")
            compare(probe.lastMeasureId, other.measureId)
            compare(shownAll(viewOf("detail"), "sdDetailMeasureTab").filter(function (t) { return t.on })[0].measureId,
                    det.measureId, "and the panel waits for the model's answer")

            setSource(detailSource())
            compare(shownAll(viewOf("detail"), "sdDetailMeasureTabs").length, 0, "one measure, no tabs")
        }
    }
}
