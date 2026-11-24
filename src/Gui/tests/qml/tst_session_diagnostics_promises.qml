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

// The session diagnostics panel's body: what the redesign promises (50-64).
// One of five files split from tst_session_diagnostics.qml along its section headers (#254). The harness (the
// body, the fakes, the spies and the fixtures) is diag/SessionDiagnosticsProbe.qml, whose header is the suite's
// map; the helpers and the init() every test starts from are diag/SessionDiagnosticsCase.qml.
SessionDiagnosticsProbe {
    id: probe

    SessionDiagnosticsCase {
        name: "SessionDiagnosticsPromises"

        // ═══════════════════════════ new: what the redesign promises ═══════════════════════════

        // The register is the surface's, then the mode's: the cast is always the glance (D4); Analyse mode
        // is the read; Capture and Replay read at a glance; forceRegister (tests only) pins it. The body
        // hands its state through, and the reader's tab survives a register swap.
        function test_50_theRegisterFollowsTheSurfaceThenTheMode() {
            setSource(spied(formingSource(false)))
            app.state.mode = 0 /* Capture */
            laidOut()
            compare(reg().register, "Replay", "Capture reads at a glance")
            compare(body.register, "Replay")
            app.state.mode = 1 /* Replay */
            laidOut()
            compare(reg().register, "Replay", "so does Replay")
            app.state.mode = 2 /* Analyse */
            laidOut()
            compare(reg().register, "Analyse", "Analyse is the read")
            compare(body.registerItem, reg(), "the body exposes the register it loaded")

            body.tab = "watching"
            laidOut()
            compare(reg().view, "watching")
            app.state.mode = 1 /* Replay */
            laidOut()
            compare(reg().register, "Replay")
            compare(reg().view, "watching", "the reader's tab survives the swap")
            verify(shown(viewOf("watching")))

            app.state.mode = 2 /* Analyse */
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
            app.state.mode = 2 /* Analyse */
            body.surface = "cast"
            setReview(spied(formingSource(false)), newestReadout())
            compare(reg().register, "Replay")
            compare(body._tab, "shot")
            const dot = shownAll(viewOf("shot"), "sdDotRow")[0]
            intoView(dot)
            mouseClick(dot)
            laidOut()
            compare(probe.detailCalls, 0, "a condition opens nothing from the cast")
            compare(app.state.mode, 2 /* Analyse */, "and the main stage is never flipped")
            const cell = findAll(oneShown(viewOf("shot"), "sdShotRuler"), "sdRulerCell")[1]
            intoView(cell)
            mouseClick(cell)
            laidOut()
            compare(probe.shotCalls, 0, "a shot column picks nothing on a surface about to vanish")

            // The persistent cast (a coach's second screen).
            body.interactive = true
            laidOut()
            compare(shownAll(body, "sdTab").length, 4, "a persistent cast keeps its tabs")
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
            compare(app.state.mode, 1 /* Replay */, "without the stage leaving Replay")
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
