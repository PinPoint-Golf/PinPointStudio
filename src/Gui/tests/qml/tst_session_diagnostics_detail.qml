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
import "diag"
import "data/diag_session.js" as Real

// The session diagnostics panel's body: the condition detail (Analyse) and how far: the figures.
// One of five files split from tst_session_diagnostics.qml along its section headers (#254). The harness (the
// body, the fakes, the spies and the fixtures) is diag/SessionDiagnosticsProbe.qml, whose header is the suite's
// map; the helpers and the init() every test starts from are diag/SessionDiagnosticsCase.qml.
SessionDiagnosticsProbe {
    id: probe

    SessionDiagnosticsCase {
        name: "SessionDiagnosticsDetail"

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
            compare(outcome.mark, "outcome", "a miss is an outcome, not a fired condition")

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
            verify(words.indexOf("outcome, launch-monitor verified") >= 0, "and the outcome's")
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

        // REFACTOR 4-d: THE SESSION OWNS ITS DECLARED FOCUS. A declaration goes to the model alone, which forwards it to
        // the Session (EVT-10); the body holds no controller and writes no copy back (FOCUS-TRACE.md L2).
        function test_28b_aDeclarationGoesToTheModelAlone() {
            useRegister("Analyse")
            const s = spied(formingSource(false))
            setSource(s)
            showTab("session")
            park()
            compare(body.focusController, undefined, "the body has no focus controller to write back to")
            let row = shownAll(viewOf("session"), "sdSessionRow")[0]
            intoView(row)
            hoverRow(row)
            mouseClick(oneShown(row, "sdSessionRowFocus"))
            laidOut()
            compare(probe.focusCalls, 1, "the model is asked")
            compare(probe.lastFocusId, "casting", "for the tapped condition")
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
            compare(app.state.mode, 1 /* Replay */, "and the stage stays in Replay")
        }

        // KEPT, rewritten: the page itself — Back and the crumb, the feature head, both headlines in the
        // model's words, the rival named and not adjudicated; the tabs and the miss chip stand down.
        function test_39_theDetailDrawsCausesEffectsAndBothHeadlines() {
            useRegister("Analyse")
            const s = detailSource()
            s.stage = "forming"
            setSource(s)
            const d = viewOf("detail")
            compare(shownAll(body, "sdSessionView").length, 0, "the detail is the view")
            compare(shownAll(body, "sdTab").length, 0, "the tabs stand down over one condition's page")
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
            compare(oneShown(fr, "sdDetailHoverReadout").text, "shot 4 · 13% hand rise · inside the pass band · fired",
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
            compare(line.text, "shot 3 · 13% hand rise · inside the pass band · clean", "and the line reads it")

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
            compare(line.text, "shot 4 · 13% hand rise · inside the pass band · fired", "leaving restores the picked shot")
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
                              valueText: "3% hand rise", readout: "shot 6 · 3% hand rise · inside the pass band · clean" })
                for (let i = 0; i < 5; ++i) sp.run[i].fx = (i + 0.5) / 6
                sp.dots.push({ index: 5, shotId: 6, swingDir: "/lib/a/s/swing_0006", f: 0.15, clipped: 0, stack: 1,
                               state: "clean", current: false, recent: true, valueText: "3% hand rise" })
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
            compare(oneShown(fr(), "sdDetailHoverReadout").text, "shot 3 · 13% hand rise · inside the pass band · clean",
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
            compare(oneShown(fr(), "sdDetailHoverReadout").text, "shot 3 · 13% hand rise · inside the pass band · clean",
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
            compare(oneShown(fr(), "sdDetailHoverReadout").text, "shot 6 · 3% hand rise · inside the pass band · clean")
        }
    }
}
