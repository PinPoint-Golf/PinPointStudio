> **Historical working document (11 Oct 2026).** `$D` was a session scratchpad, now gone. Its
> mock library (`$D/lib`, `$D/reg`) and data are archived at `/mnt/swingdata/scratch/typeset-harness/archive/2026-10-11/` (`mock_lib_round4/`,
> `mock_reg_round4/`, `data_4jul_shot8/`); the shipped code is in `src/Gui/typeset/` and
> `src/Gui/diagnostics/`. Kept as a worked example of the method in `docs/design/stage_typeset_playbook.md`.

# Tests: tst_session_diagnostics rewritten for the typeset panel, and its probes

Written 11 Oct 2026 during an unattended run. Nothing was built or run, and nothing was committed. qmllint reports no syntax errors; it cannot resolve the module, so it gives warnings only. The fixture JS was run under node.

## Files
- `src/Gui/tests/qml/tst_session_diagnostics.qml`: replaced in place, 59 test functions.
- `src/Gui/tests/qml/data/diag_session.js` (new, ~470 KB, `.pragma library`). It holds the real 4 Jul session ($D/data: readout, cards, source trimmed of cards, chains and detail, header, and detail2 = early_extension with 2 measures). Each item is stored as a JSON string, and every accessor returns a fresh copy. It also holds the live-state synthesis ported from `$D/reg/LiveStates.qml`: cold0, cold2(withProfile), forming, established, liveNoPick, quiet(picked), liveOlderPick, closing, reviewNoPick, reviewed(withDetail), zeroFired and long23. Each returns `{ source, readout }`. The test imports it with `import "data/diag_session.js" as Real`, the same way tst_auto_annotations imports a .js. qml_ui reads tests live from QUICK_TEST_SOURCE_DIR, so no CMake change is needed.
  - One liberty, recorded in the file: a condition's state on the cut shot comes from its own tick at that shot, so the strips, the dots and the delta agree.
- Probes: thisshot_follows_selection, diag_nav_survives_mode_switch, detail_hover, session_spread and diag_measure_tabs. Runners: build/run-me/verify-{thisshot,diag-nav,detail-hover,session-spread,measure-tabs}.sh, whose QML-error grep now names PpDiag*, PpLedgerTable, PpSpreadStrip, PpRunChart, PpHistoryStrip, PpCauseGraph, PpPathChains, PpDotPlot, PpBarcode, PpFigureRow, PpFeatureHead, PpShotStrip, PpTabs, PpTypeset and PpSessionDiagnostics. ledger_live_states is unchanged.

## Counts
34 kept, 6 obsolete, 13 rewritten from "needs new design", 12 new. That makes 59 functions (53 − 6 + 12).

## Old test → new test
| Old | New | Notes |
|---|---|---|
| 01 coldSaysTheNIsTooSmall | test_01 (same name) | NND. Both registers: sdColdHeadline and sdColdSubline in sdSessionView, no head and no rows, sdStageChip and cardAside; also real cold0, whose This shot says "the next swing reads here". |
| 02 coldDrawsTheExpectationsDashed | test_02 | NND. 3 sdExpectationCard rows, "Usually yours", "expectations to test, not findings"; also real cold2. |
| 03 coldWithNoProfileOmitsTheBlockEntirely | test_03 | NND. sdExpectations is not shown, its height is 0, there are no cards, and no "Usually yours" text anywhere. |
| 04 formingDrawsTheCardsAndTheirState | test_04_formingDrawsTheRowsAndTheirState | sdSessionRow ×2 (cardId), Name, Fresh only on the fresh row, Fired "4/5", Line2 "dispersion", Shot ink warn vs not, focus quiet at rest; Replay sparkline rows or tiles ×2. |
| 05 notAssessableIsShortAndOutlined | test_05_notMeasuredIsAStubOnTheBaseline_neverAGap | NND (D5). 5 sdShotCell with cellState; one mark each; bar warn, tick ink3, stub ink3, stub < tick < bar, stub wider than tick, both on the baseline; Replay strip or sdTileStub. |
| 05b meter | test_05b_outBySaysHowFarAndRefusesToGuess | sdFiredRowOutBy shows "3.4×", or "-" when strengthKnown is false. |
| 05c verdict | test_05c_theWarnColourCarriesTheFiringAndOnlyAFiring | A fired value is warn and a clean one is not; no shown Text is colorError (both registers, 3 views). |
| 05d rank badge | test_05d_theRankLeadsTheRow | Rank "1"/"2", "1=" on both rows of a tie, "" when unranked. |
| 06 quiet strip | test_06_aQuietShotSaysSoAndKeepsItsRuler | NND (D2). Replay: sdQuietState, label, line, head and plot down, the ruler stays when picked, tabs live. Analyse: the quiet line plus the plot (never gated), tab count "quiet". Also real quiet(). |
| 07 closing bookends | test_07_closingPutsTheBookendsOnTheRowsAndTheTenseOnThisShot | sdSessionRowTypical/Worst/Best "4,3,5" with shotId; at 480 they fold with "typical shot"; sdShotEmpty "The session has closed…" in both registers. |
| 07b tabs | test_07b (same) | 3 sdTab with key; press → body.tab; views swap; closed This shot shows "carousel". Analyse sdTabCount 2/2/2; Replay shows no counts. |
| 07c pick edge + cast | test_07c (same) | Edge once. Cast (surface "cast", non-interactive): Replay in Analyse mode, no tabs, sdCastHead, frame down. |
| 07d live older pick | test_07d (same) | sdShotHead contains "shot 9 of 14" (ordinal) and never the id 23; sdShotLabel hidden; both registers. |
| 08 watching | test_08 (same) | With a pick: sdBarcodeRow ×2; without one, names and counts; Replay head "Two watched"; sdWatchingEmpty in both. |
| 09 coverage | test_09 (same) | sdCoverageLine in sdSessionView in BOTH registers, at 4 stages. |
| 11 narrow | test_11 (same) | At 396: 9 rows (Analyse) or 9 items (Replay), each fitting the width; scroll to the last; no Text ellipsised; title "SESSION DIAG.". |
| 11c filter | test_11c (same) | sdFilterTool all/ball/roots; body.cardFilter; rank not renumbered ("2" under ball); tab count follows; sdCardFilterEmpty text; Replay has no filter. |
| 12 both themes | test_12_bothThemesOfOneAesthetic | Folio 12/13: same row geometry, different fired colour, stub intact. |
| 14 drill-in rails | test_14_theDetailDrawsEveryPathAndOnlyTheEdgesTheModelAuthored | sdCauseNode / sdCauseEdge per graph equal the authored unique nodes/pairs (7 edges); below medium sdPathChain ×4. |
| 15 link strokes | test_15_everyLinkGradeGetsItsOwnStroke | NND (D6). sdCauseEdge grade, strokeStyle, hasArrow, st.width, strokeOpacity and st.color per grade; 5 distinct signatures; arrows only on the strong two; PathChains 5 distinct glyphs, all drawn, plus the arrow sentence. |
| 16 node kinds | test_16 (same) | NND (F56/F59). kind and mark per node; screened root's sdScreenCta "Run the screen"; sdDetailMark for a non-card page; PathChains footnotes carry the marks. |
| 16b watched node | test_16b (same) | kind "watched", mark "fired", not "ghost"; paths say "measured, not yet a pattern" and never "planned". |
| 19 narrow rails | test_19_narrowListsEveryPathAndOpensOneInPlace | sdPathChain ×2 causes; a press flips sdPathOpen.open, the chain grows, link word and note shown, closes again. |
| 20 overhang | test_20 (same) | Regions inside the body; content inside its view sideways; 3 sizes × 2 registers × 3 views × 7 states, plus 2 details × 3 sizes. |
| 21 review header | test_21_reviewStatesItsTense | sdReviewBadge, cardAside, sdShotLabel down, a shown sdReviewNote "final session state…", head "14 shots"; both registers. |
| 22 review no pick | test_22 (same) | sdShotEmpty "Pick a shot in the carousel…", never "next swing"; bookend column "7"; a pick opens and reads. |
| 23 review cells | test_23_reviewReadsEveryConditionAndNeverABlankSlot | 7 sdDotRow (5 fired, 2 clean) with tierTag; sdNotMeasuredNote names both reasons; sdFiredRowValue "−4.4"; tier column. Plus old 34's claim: Replay fired first, the unmeasured counted in sdQuietCount. |
| 24 selected tick | test_24_theSelectedShotIsThePickColumn | One picked cell (id 6), bar still warn, sdPickColumn at its x. |
| 25 here and after | test_25_rowsSayWhatHappenedHereAndAfter | Line2 "3 more firings after this shot" / "No firings…"; Fired "4/5"; live recency "Last fired 1 measurable shots ago". |
| 26 pip row | test_26 (unchanged) | PpShotPipRow. |
| 27 focus tag | test_27_aPatternRowOffersFocusAndSaysWhichStateItIsIn | NND (F33). sdSessionRowFocus hidden at rest, "Focus" on hover, "Focused · clear" at rest when focused; sdSessionRowFocusBar on that row only; Replay head "focus: Casting". |
| 28 focus tap | test_28_theFocusLinkAsksTheModelToDeclareOrClearFocus | Row link → declareFocus(id) or clearFocus, opens nothing, no local flip; sdDetailFocus the same. |
| 29 live nodes focus | test_29_aScreenedRootAsksForItsScreenNeverForAPage | NND (F63-64). Root click and its CTA → screenRequested("screen.pelvic_disassociation", id), no page; a live node opens its page; below medium sdScreenRequests/sdScreenCta. |
| 30 miss chip | test_30 (same) | NND (F66). "Declare miss" in Cold/Forming; down at Established; "Miss: Slice" when declared; down when non-interactive, on the detail, on the cast. |
| 31 miss picker | test_31_theMissPickerOpensInPlaceAndDeclaresOne | NND (F67). Inline (shown() reaches the body: not a Popup), pushes the view down; label; candidates by missId; pick → declareMiss("hook") and close; current one `on`; sdMissClear → "". |
| 32 pulse | test_32_thePulseWashesWhatFiredAndLeadsWithTheFocus | NND (D1). sdPulseWash order 0 for the focus, pulsing on fired rows only, both registers. |
| 33 reduceMotion | test_33 (same) | NND. No wash pulsing, level 0, nothing late. |
| 38 card opens | test_38_everyClickGoesSomewhereReal | Row → openDetail without focus; cell → shot 3; barcode row → "sway"; bookend Worst → shot 1 (F38 change); Replay dot row → openDetail and mode Analyse. |
| 39 detail draws | test_39 (same) | Tabs and miss chip down, Back, crumb, sdFeatureHead, exact headlines, rival "not adjudicated", flick inside; Back at 396. |
| 40 back restores | test_40_backRestoresThePlaceUntouched | Same tab, filter, the SAME sdCardsFlick object and contentY, watchingExpanded. |
| 42 retarget | test_42 (same) | Node → openDetail(other), no close; Back → 1. |
| 43 two figures | test_43_aRowShowsItsFiguresAndItsStripNotCharts | Median "14", Shot "15" warn, Unit once, strip, no charts; "-" in ink3. |
| 44 run | test_44_theDetailRunKeepsTheStripsPromises | 4 sdRunMark, 1 sdRunStub on the foot, Y ticks ⊆ {0,10,20}, higher drawn higher, ringed and sdRunSelected = 4; a mark click → folder of shot 1. |
| 45 strip + history | test_45_theSpreadAndTheHistoryCaptionThemselves | sdSpreadCaption, sdDetailHoverReadout, 3 sdHistoryColumn (current last), medians, sdHistoryCaption "7 Oct · 13", highMeans text; no history → 0 columns. |
| 47 hover | test_47_aHoverLightsTheSameShotEverywhereAndTheLineReadsIt | A real mouseMove on the dot → brush.hoveredShot 3, run mark hovered, line reads it; a brush hover of the unmeasured shot reads "-"; leaving restores; no shotRequested. |
| 48 follows shot | test_48_theOpenDetailFollowsTheCurrentShot | The republish (source plus readout) moves sdRunSelected and the ringed dot (none on the unmeasured); the hover survives a republish; a 6th shot adds a mark and a dot. |

### Obsolete (deleted; the reasons are also in the file's header)
- **test_10 designSizeIsScaleOne…** — fit factor `k` gone (house rule 2). → test_55.
- **test_11b wholeRowsOfCards** — no card grid; ledger rows are atoms (RULES 6, D11).
- **test_34 ordersByInformationAndFoldsTheSilentTail** — silent-tail fold replaced by closeness order + count (S37, D11); "fired first" moved into test_23.
- **test_35 tailOpensInPlace** — no tail (S37, D11).
- **test_41 detailGoesVerticalWhenCompact** — vertical rails gone; PathChains below medium (test_19), Back at narrow (test_39).
- **test_46 figuresFitTheCardsAtEverySize** — no card figures. → test_58 (fold, don't drop).

These went with their components and have no test of their own:
- the 5-step meter ladder and its colours;
- the FIRED/CLEAN pill words;
- "scroll for the rest";
- the separate newest-swing after-shot strip;
- the corridor strip's "← better / worse →" words;
- nearest-dot picking;
- the "twice its first height" run.

### New tests (50–61)
- 50 the register follows the surface, then the mode (Capture/Replay → Replay, Analyse → Analyse, cast → Replay, forceRegister wins). The tab survives a swap, `body.registerItem` is exposed, and `interactive` is passed through.
- 51 `interactive: false` swallows clicks: a non-interactive panel (row, cell, tab, filter, focus), the auto cast's guard (dot, ruler; the stage is never flipped), and the persistent cast (tabs live, shots pick, no drill).
- 52 a 23-shot session with id 6 deleted: 23 cells per strip and every cell inside its view; "shot 8 of 23", never 9; the ruler has 23 columns, ids skip, the picked column is id 9.
- 53 zero counts as words: no "no of" / "on no" / "no are" in any Text; "outside on none" in Replay's Session and Watching; "Inside every corridor"; sdFiredEmpty; Analyse "on none of them".
- 54 Esc closes the detail (activeFocus, then keyClick); not when non-interactive.
- 55 house rule 2: each Text that occurs once at 480 and at 1522 has the same pixelSize there; roles.fBody and fTitle are unchanged.
- 56 house rule 12: Replay This shot shows more sdDotRow at height 1100 than at 560 (real readout).
- 57 no Text ellipsised (elide none, or truncated false): both registers, 3 views, 5 widths, and the detail.
- 58 fold, don't drop: every row keeps Rank, Name, Fired, Shot, Median, Trend, Typical, Worst, Best (grid or fold), its unit and its strip, at 1522, 820, 480 and 396; no row Text overhangs.
- 59 D3: live with nothing picked reads the newest swing. "Newest swing · shot 5 of 5", 2 fired rows; Replay has no ruler and shows "1 clean · 2 not measurable"; Analyse drops the value column and keeps "out by".
- 60 D9: Replay never drills. No sdDetailBody or sdDetailFlick, sdDetailParked "Casting is open in Analyse", and a tab press closes the detail and then switches tab.
- 61 F49 measure tabs: one tab per measure, exactly one `on` (= detail.measureId), and a press calls setDetailMeasure; one measure draws no tabs.

## Assumptions to check after the build (names and behaviour)
1. **The body:** `surface`, `forceRegister`, `register`, `registerItem` and `view` exist (seen in the ported body), and so do the frame's sdTitle, sdStageChip, sdReviewBadge and sdShotLabel. `_tab` forces "shot" when non-interactive and a shot is available.
2. **The register root:** objectName "sdRegister", properties `register`, `view`, `surface` and `interactive`. It is found by walking `children` from the body (inside the Loader).
3. **Views:** sdShotView, sdSessionView, sdWatchingView and sdDetailBody are inside the named Flickables (sdShotCardsFlick, sdCardsFlick, sdWatchingFlick, sdDetailFlick). They are built only on first show, so each test shows a tab before it looks up a view. Replay has no sdDetailFlick.
4. **Blocks:**
   - The ledger cells and their folded twins share one objectName (`sdSessionRow<Key>`) and carry a `text` property. The colour is read off their first Text child (`inkOf`).
   - The PpShotStrip cell's marks are found by index: sdShotStub's parent has children [fired bar, clean tick, stub].
5. **Edges:** sdCauseEdge exposes `st` (the linkStyle object, `st.width` and `st.color`) and `strokeOpacity`. Edges exist only in graph mode. At 1168 px the detail is graph mode (em = 11 px); at 480 it is PathChains.
6. **PathChains:** `linkGlyph(g)` is callable from the test. Footnote marks print with ", " in place of the first " · ".
7. **Marks:** PpMark is a 0×0 item. `mouseClick(mark)` and `mouseMove(mark, 0, 0)` land on its centred MouseArea. The brush is reached as `sdSpreadStrip.brush`.
8. **The pick:** picking comes from `readout.shotId`, not from spread.current. That is why the detail tests also hand a readout (spreadReadout).
9. **Watching table:** without a picked shot, Watching's names/counts table is a PpLedgerTable of the default kind, so its rows are named **sdFiredRow**. Test_08 asserts the text, not the rows. Consider `kind`/rowName for that table.
10. **Pulse:** PpPulseWash is the first `sdPulseWash` under each row or tile, with `order`, `pulsing` and `level`.
11. **Miss chip:** sdMissChip (a PpTabs chip) has text "Declare miss" or "Miss: Slice". sdMissPickerLabel reads "What is the bad shot?".
12. **Esc:** this needs the offscreen window to hand activeFocus to sdDetailBody. It is the likeliest flake: if it fails, check that the window is active.
13. **Typeset sizes:** em = 11 px at fontScale 1 (compact ≥ 462, medium ≥ 660, wide ≥ 990).

### Likely failures that would be real contract gaps (the tests encode INVENTORY; not "fixed" in the tests)
- **test_43 (the "-" colour).** It asserts that "-" is ink3. PpLedgerTable gives a non-fired shot `roles.ink`; INVENTORY #47 says ink3, so that unmeasured never reads as clean.
- **A watched node not measured on the shot.** INVENTORY #34 says it must not be a "ghost". PpCauseGraph.nodeKind still maps `state notAssessable` → "ghost", and its comment says otherwise. Test_16b asserts only the fired case; I left the not-measured case unasserted for you to decide.
- **test_40 / D8.** If a republish rebuilds rows and the Flickable re-clamps contentY, the scroll check fails. That would be a real place-keeping bug.

## Probes (by the fork; details in its report)
- **thisshot:** sdRegister is Analyse, sdShotView is shown, rows > 0, sdShotEmpty is hidden, sdShotHead contains "shot N of".
- **diag_nav:** also asserts sdDetailBody in Analyse and sdDetailParked in Replay. **Risk:** the panel's navigation save may record a 0 contentY from an unbuilt view.
- **detail_hover:** moved to Analyse. Hover is identified by shot ID through the PpMark MouseArea, and the caption is checked against run[].readout.
- **diag_measure_tabs:** the model half is unchanged. In Replay it checks there are no tabs; in Analyse, two tabs with the picked one `on`.
- **session_spread:** bodies are made with `forceRegister: "Analyse"`. Row checks are by cardId with the median, shot and unit cells; the detail checks use the new blocks; hover goes through `brush.hoverShot`.

**Dropped from the probes:**
- the "twice its height" run;
- nearest-dot picking;
- the header pill and tick run;
- sdHistoryYTick, now sdRunYTick on the figure row.

## Round 2 (after the first build): verdicts, fixes and runs
The first qml_ui run had 242 passing and 6 failing, all in SessionDiagnostics. I used 2 of the 3 allowed test builds. The final ctest result for qml_ui is **248 passed, 0 failed** (Totals from build/tests/Testing/Temporary/LastTest.log).

**The six failures:**
- **test_11 — TEST was wrong.** Replay's one-line rows fit 396×560, so there is nothing to scroll. The overflow is now asserted for Analyse only, and Replay must still reach the ninth row.
- **test_22 — TEST was wrong.** The previous register's pick had left `body.tab` on "shot". The test now resets the tab to "session" before each register.
- **test_27 and test_28 — CODE was wrong.** A hover over a row's NAME never reached the row, because the rich-text name cell takes the hover from the MouseArea beneath it. The row never went "hot" where a reader points, so the focus link never appeared. The first run's warning confirmed it. Fix: PpLedgerTable rows and PpBarcode rows now hover through a `HoverHandler`, and the MouseArea keeps only click and cursor. The test helper `hoverRow` hovers the name and asserts it strictly.
- **test_43 — CODE was wrong** (INVENTORY #47, D5). PpLedgerTable now draws the session "shot" cell in ink3 when the text is "-" or the shot was not measured. The fired-table value cell is ink3 for a not-measured condition.
- **test_58 — TEST was wrong.** `one(row, "sdShotStrip")` found a hidden strip inside a non-strip grid cell. The test now uses `shownAll(...) >= 1`.

**Code changes (to be recorded for Mark):**
- src/Gui/diagnostics/PpLedgerTable.qml: the unmeasured "-" is ink3, and row hover is a HoverHandler.
- src/Gui/diagnostics/PpBarcode.qml: row hover is a HoverHandler (the same defect when the name is folded as rich text).
- The app tree is NOT rebuilt with these changes. No probe depends on them.

**Probes**, run against the app build under guard.sh, each on a scratch copy:
- **verify-thisshot: PASS.**
- **verify-diag-nav: PASS.** The scroll restore was 132 → 132.
- **verify-measure-tabs: PASS.**
- **verify-diag-print: PASS.** Two PDFs of 7 pages each are in /mnt/swingdata/scratch/diag-print/out.
- **verify-session-spread: PASS after three probe fixes:**
  1. A binding loop: `readout` was bound to a function that walked the window tree. The probe now follows the readout through a timer, plus a synchronous follow before each check.
  2. The bodies the probe made were counted as the app's body. They are now tracked in `made`.
  3. "This session last" was a stale data assumption: the library now has an 8 Oct session. The probe now checks that the columns are oldest first and that the current column is this session.
- **verify-detail-hover: 5 of 6 holds pass.** Two probe fixes: the dot or mark target is now one the pointer can reach (on screen, and not under a later mark's pick area), and it is centred in the view before the move. The probe also logs what is under the pointer when a hold fails.
  - The remaining FAIL is **CODE, not fixed: PpFigureRow has no form for a ~172 px panel** (the "split" arrangement). The run chart starts beyond the view (right edge 369 of 172 px), so it is clipped away entirely and no run mark can be reached.
  - It needs a design call: below about 290 px, stack spread / run / history each at full width, and move the zone labels (96 px) under the figure. It also needs an app rebuild to judge. I did not change the layout blind.

**Runner changes:** verify-thisshot, verify-diag-nav and verify-measure-tabs now copy their session to /mnt/swingdata/scratch/diag-probes/<probe>/ and run on the copy. Loading a session regrades it in place, so they used to run on the library. verify-detail-hover and verify-session-spread already staged copies, and verify-diag-print copies to scratch.
