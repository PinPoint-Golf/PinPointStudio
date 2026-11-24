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

// The session diagnostics panel's body: the live states: Cold, Forming, the strip, the quiet shot, Closing, the tabs, Watching and coverage, the fit.
// One of five files split from tst_session_diagnostics.qml along its section headers (#254). The harness (the
// body, the fakes, the spies and the fixtures) is diag/SessionDiagnosticsProbe.qml, whose header is the suite's
// map; the helpers and the init() every test starts from are diag/SessionDiagnosticsCase.qml.
SessionDiagnosticsProbe {
    id: probe

    SessionDiagnosticsCase {
        name: "SessionDiagnosticsStates"

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
                compare(shownAll(body, "sdTab").length, 4, r + ": four tabs")
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
            app.state.mode = 2 /* Analyse */
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
    }
}
