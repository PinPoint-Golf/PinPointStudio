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

// The session diagnostics panel's body: review, the declared miss (retired), the shot outcomes and the after-shot pulse.
// One of five files split from tst_session_diagnostics.qml along its section headers (#254). The harness (the
// body, the fakes, the spies and the fixtures) is diag/SessionDiagnosticsProbe.qml, whose header is the suite's
// map; the helpers and the init() every test starts from are diag/SessionDiagnosticsCase.qml.
SessionDiagnosticsProbe {
    id: probe

    SessionDiagnosticsCase {
        name: "SessionDiagnosticsReview"

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

        // ═══════════════════════════ the declared miss, retired ═══════════════════════════

        // Rewritten: the chip and both pickers are gone from every register. A session that declared a
        // miss before the retirement still loads; `declaredMiss` stays readable on the model.
        function test_30_theDeclaredMissHasNoControlsLeft() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setSource(spied(coldSource(probe.expectationRows)))
                compare(findAll(body, "sdMissChip").length, 0, r + ": no invitation in Cold")
                setSource(spied(formingSource(false)))
                compare(findAll(body, "sdMissChip").length, 0, r + ": nor in Forming")
                compare(findAll(body, "sdMissPicker").length, 0, r + ": and no picker")
                const s = spied(formingSource(false))
                s.declaredMiss = "slice"
                s.declaredMissName = "Slice"
                setSource(s)
                compare(findAll(body, "sdMissChip").length, 0, r + ": an old declaration is not reported on the tabs")
                verify(shownAll(body, "sdTab").length > 0, r + ": and the session still reads")
            }
        }

        // ═══════════════════════════ the shot outcomes (shot_outcomes_design.md §6) ═══════════════════════════

        function outcomeSource() { return spied(Real.withOutcomes(formingSource(false))) }
        function chipOf(row, key) {
            const c = findAll(row, "sdFilterTool").filter(function (t) { return t.key === key })
            verify(c.length === 1, "one '" + key + "' link")
            return c[0]
        }
        function press(item) { intoView(item); mouseClick(item); laidOut() }

        // §6.1: the head of This shot on the glance: the labels in words, the story, the quiet lines.
        function test_65_theOutcomeHeadLeadsThisShotOnTheGlance() {
            useRegister("Replay")
            setReview(outcomeSource(), newestReadout())
            showTab("shot")
            const h = oneShown(viewOf("shot"), "sdOutcomeHead")
            compare(h.headText, "Fat · Slice · Short", "the labels, as words")
            compare(one(h, "sdOutcomeChip:chunk").text, "Fat", "a marker per label")
            compare(one(h, "sdOutcomeChip:slice").labelFamily, "direction")
            const words = allText(h)
            verify(words.indexOf("What happened · shot 5") >= 0, "the kicker: " + words)
            verify(words.indexOf("You stand up through the ball") >= 0, "the story: " + words)
            verify(allText(oneShown(h, "sdOutcomeSeenWith")).indexOf("Seen with: you stand up through the ball") >= 0, "the driver, as seen with")
            compare(oneShown(h, "sdOutcomeCarryLine").text, "24 yd short of your usual 162: 16 from the fat, 6 slower club speed, 2 curve")
            verify(shown(one(h, "sdOutcomeWhy")), "and the way into the read")
            compare(shownAll(body, "sdCarryWaterfall").length, 0, "the waterfall is the Analyse read's; the glance has its line")
        }

        // Nothing is drawn until the shot is analysed, and a source with no outcomes has no head at all.
        function test_66_theHeadWaitsForAnalysisAndSaysWhenThereIsNoReason() {
            useRegister("Replay")
            const pending = outcomeSource()
            pending.outcomesByShot[5] = { ready: false }
            setReview(pending, newestReadout())
            showTab("shot")
            compare(shownAll(viewOf("shot"), "sdOutcomeHead").length, 0, "not ready: nothing, and no spinner")

            setReview(spied(formingSource(false)), newestReadout())
            showTab("shot")
            compare(shownAll(viewOf("shot"), "sdOutcomeHead").length, 0, "a session with no outcomes has no head")

            const unexplained = outcomeSource()
            unexplained.outcomesByShot[5] = Real.noReasonOutcome()
            setReview(unexplained, newestReadout())
            showTab("shot")
            const h = oneShown(viewOf("shot"), "sdOutcomeHead")
            compare(oneShown(h, "sdOutcomeNoReason").text, "No reason found", "said plainly")
            compare(shownAll(h, "sdOutcomeSeenWith").length, 0, "and no driver is invented")
        }

        // §6.1: corrections are quiet links, per family; a press asks the model and nothing here mirrors it.
        function test_67_theCorrectionRowAsksTheModel() {
            useRegister("Replay")
            setReview(outcomeSource(), newestReadout())
            showTab("shot")
            const h = oneShown(viewOf("shot"), "sdOutcomeHead")
            compare(shownAll(h, "sdOutcomeCorrectRow").length, 0, "closed until asked for")
            press(oneShown(h, "sdOutcomeNotRight"))
            oneShown(h, "sdOutcomeCorrectRow")
            const strike = oneShown(h, "sdOutcomeCorrect:strike")
            verify(chipOf(strike, "chunk").on, "the detected label is marked")
            press(chipOf(strike, "thin"))
            compare(probe.correctCalls, 1)
            compare(probe.lastCorrectShot, 5)
            compare(probe.lastCorrectFamily, "strike")
            compare(probe.lastCorrectTo, "thin", "the candidate's id, not its name")
            compare(shownAll(h, "sdOutcomeCorrectRow").length, 0, "a correction closes the row")

            press(oneShown(h, "sdOutcomeNotRight"))
            const direction = oneShown(h, "sdOutcomeCorrect:direction")
            press(chipOf(direction, "meant:fade"))
            compare(probe.lastIntentShot, 5)
            compare(probe.lastIntentShape, "fade", "a miss the golfer meant")
            press(oneShown(h, "sdOutcomeFromHere"))
            compare(probe.lastIntentFromShot, 5)
            compare(probe.lastIntentFromShape, "fade", "and from here on")
        }

        // §6.1: a clean shot is one calm line, the row folded until "Not right?" opens it.
        function test_68_aCleanShotIsOneCalmLine() {
            useRegister("Replay")
            const s = outcomeSource()
            s.outcomesByShot[5] = Real.cleanOutcome()
            setReview(s, newestReadout())
            showTab("shot")
            const h = oneShown(viewOf("shot"), "sdOutcomeHead")
            compare(h.headText, "Good shot")
            compare(shownAll(h, "sdOutcomeSeenWith").length, 0)
            compare(shownAll(h, "sdOutcomeCarryLine").length, 0)
            compare(shownAll(h, "sdOutcomeCorrectRow").length, 0, "folded")
            press(oneShown(h, "sdOutcomeNotRight"))
            oneShown(h, "sdOutcomeCorrectRow")
            compare(oneShown(h, "sdOutcomeNotRight").text, "Close", "the same link folds it again")
            press(oneShown(h, "sdOutcomeNotRight"))
            compare(shownAll(h, "sdOutcomeCorrectRow").length, 0, "unchanged")
        }

        // §6.1: the auto-closing cast shows the words and nothing to press; Why is off on every cast.
        function test_69_theCastShowsTheWordsAndNothingToPress() {
            useRegister("Replay")
            body.surface = "cast"
            body.interactive = false
            setReview(outcomeSource(), newestReadout())
            showTab("shot")
            const h = oneShown(viewOf("shot"), "sdOutcomeHead")
            compare(h.headText, "Fat · Slice · Short")
            compare(shownAll(h, "sdOutcomeCorrectRow").length, 0, "no correction row")
            compare(shownAll(h, "sdOutcomeFromHere").length, 0)
            compare(shownAll(h, "sdOutcomeWhy").length, 0, "no way into the read")
        }

        // §6.1: "Why →" reads This shot in Analyse with the stage still in Replay, and comes back.
        function test_70_whyReadsThisShotInAnalyseOverTheGlance() {
            useRegister("Replay")
            setReview(outcomeSource(), newestReadout())
            showTab("shot")
            compare(body.placeScope, "Replay", "the glance keeps its own reader's place")
            press(oneShown(oneShown(viewOf("shot"), "sdOutcomeHead"), "sdOutcomeWhy"))
            compare(body.register, "Analyse", "read in Analyse")
            // #129: Analyse read OVER the glance keeps its lists' places apart from Analyse's own
            compare(body.placeScope, "Replay>Analyse", "over the glance, a place of its own")
            compare(app.state.mode, 1 /* Replay */, "the stage has not moved")
            oneShown(body, "sdOutcomeLead")
            press(oneShown(body, "sdShotWhyBack"))
            compare(body.register, "Replay", "and the chip is the way back")
            compare(body.placeScope, "Replay")
        }

        // §6.2: where the yards went, Analyse only, each part in causal order and the estimate said in words.
        function test_71_theCarryWaterfallIsTheReadsAccount() {
            useRegister("Analyse")
            setReview(outcomeSource(), newestReadout())
            showTab("shot")
            const v = viewOf("shot")
            verify(plain(oneShown(v, "sdOutcomeLead").head).indexOf("Fat slice, 24 yd short") === 0, "the phrase leads")
            oneShown(v, "sdOutcomeEvidence")
            const w = oneShown(v, "sdCarryWaterfall")
            compare(w.rows.map(function (r) { return r.name }).join(","), "Your usual,Club speed,Fat strike,Curve,Actual")
            compare(shownAll(w, "sdCarryWaterfallScaled").length, 0, "nothing was cut to fit")
            verify(allText(w).indexOf("against your usual 162 ± 7 yd") >= 0, "a modelled distance carries its provenance: " + allText(w))

            const scaled = outcomeSource()
            scaled.outcomesByShot[5].carry = Real.carryBlock({
                scaled: true, residual: 3, residualShown: true,
                parts: [{ id: "strike", name: "Fat strike", loss: 21, detail: "", lineWords: "from the fat" },
                        { id: "launch_spin", name: "Launch / spin", loss: 1, detail: "", lineWords: "launch and spin" }] })
            setReview(scaled, newestReadout())
            showTab("shot")
            const w2 = oneShown(viewOf("shot"), "sdCarryWaterfall")
            compare(w2.rows.map(function (r) { return r.name }).join(","), "Your usual,Fat strike,Launch / spin (estimate),Not accounted for,Actual")
            oneShown(w2, "sdCarryWaterfallScaled")

            const none = outcomeSource()
            none.outcomesByShot[5].carry = ({})
            setReview(none, newestReadout())
            showTab("shot")
            compare(shownAll(viewOf("shot"), "sdCarryWaterfall").length, 0, "no usual to be short of: no waterfall")
            oneShown(viewOf("shot"), "sdOutcomeEvidence")
        }

        // §6.3: the shot log is the ruler with words: one cell a shot, ✓ for clean, * for corrected; a click moves the carousel.
        function test_72_theShotLogIsOneCellAShot() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setSource(outcomeSource())
                showTab("session")
                const v = viewOf("session")
                const log = oneShown(v, "sdShotLog")
                verify(log.hasWords, r + ": words in place of counts")
                compare(log.nShots, 5, r + ": every shot")
                const cellText = function (id) { return allText(one(log, "sdShotLogCell:" + id)) }
                verify(cellText(1).indexOf("✓") >= 0, r + ": clean is ✓: " + cellText(1))
                verify(cellText(2).indexOf("FD ✓") >= 0, r + ": an intended shape keeps its name and ✓: " + cellText(2))
                verify(cellText(3).indexOf("SL*") >= 0, r + ": a corrected cell ends in *: " + cellText(3))
                verify(cellText(5).indexOf("FAT") >= 0 && cellText(5).indexOf("SL") >= 0, r + ": the labels: " + cellText(5))
                probe.shotCalls = 0
                press(one(log, "sdShotLogCell:4"))
                compare(probe.shotCalls, 1, r + ": a click moves the carousel")
                compare(probe.lastShotId, 4, r)
                compare(probe.lastShotDir, "/fake/session/swing_0004", r + ": the swing the log names")
            }
        }

        // §6.3: the mix and today's carry, the model's lines as written; absent when there is nothing to say.
        function test_73_theMixAndTodaysCarryAreTheModelsLines() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                setSource(outcomeSource())
                showTab("session")
                const v = viewOf("session")
                const mix = plain(oneShown(v, "sdShotMix").text)
                verify(mix.indexOf("Mix: 2 clean · 2 fat slice · 1 slice") === 0, r + ": " + mix)
                verify(mix.indexOf("1 corrected (cells marked *)") >= 0, r + ": corrections are counted: " + mix)
                compare(plain(oneShown(v, "sdCarryToday").text), "7 iron carry today 151 ± 9 yd · your usual 162 ± 7 yd", r)

                const none = outcomeSource()
                none.carryToday = []
                setSource(none)
                showTab("session")
                compare(shownAll(viewOf("session"), "sdCarryToday").length, 0, r + ": no carry reported, no line")

                setSource(spied(formingSource(false)))
                showTab("session")
                compare(shownAll(viewOf("session"), "sdShotLog").length, 0, r + ": a session with no outcomes has no log")
                compare(shownAll(viewOf("session"), "sdReasonsTable").length, 0, r + ": nor reasons")
            }
        }

        // §6.3: one row a reason, its shots on a strip, cost in yards in both registers; a row opens its
        // condition, and the unexplained row offers the flag instead.
        function test_74_theReasonsTableAccountsForTheBadShots() {
            for (const r of ["Replay", "Analyse"]) {
                useRegister(r)
                probe.detailCalls = 0
                probe.flagCalls = 0
                setSource(outcomeSource())
                showTab("session")
                const t = oneShown(viewOf("session"), "sdReasonsTable")
                const rows = shownAll(t, "sdPathRow")
                compare(rows.map(function (x) { return x.cardId }).join(","), "early_extension,", r + ": the reason, then the unexplained")
                const words = allText(rows[0])
                verify(words.indexOf("Early extension") >= 0, r + ": " + words)
                verify(words.indexOf("30 yd") >= 0, r + ": cost in yards: " + words)
                compare(findAll(rows[0], "sdShotCell").length, 5, r + ": a cell per shot")
                compare(findAll(rows[0], "sdShotCell").filter(function (c) { return c.cellState === "fired" }).length, 2, r + ": this reason's shots")
                verify(allText(rows[1]).indexOf("Flag for review") >= 0 || t.width < Theme.sp(400), r + ": the unexplained row offers the flag: " + allText(rows[1]))

                clickRow(rows[0], "sdPathRowName")
                compare(probe.detailCalls, 1, r + ": a row opens its condition")
                compare(probe.lastDetailId, "early_extension", r)

                setSource(outcomeSource())
                showTab("session")
                clickRow(shownAll(oneShown(viewOf("session"), "sdReasonsTable"), "sdPathRow")[1], "sdPathRowName")
                compare(probe.flagCalls, 1, r + ": one flag for the one unexplained shot")
                compare(probe.lastFlagShot, 4, r)
                compare(probe.detailCalls, 1, r + ": and it opens nothing")
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
    }
}
