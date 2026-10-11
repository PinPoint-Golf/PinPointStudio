> **Historical working document (11 Oct 2026).** `$D` was a session scratchpad, now gone. Its
> mock library (`$D/lib`, `$D/reg`) and data are archived at `/mnt/swingdata/scratch/typeset-harness/archive/2026-10-11/` (`mock_lib_round4/`,
> `mock_reg_round4/`, `data_4jul_shot8/`); the shipped code is in `src/Gui/typeset/` and
> `src/Gui/diagnostics/`. Kept as a worked example of the method in `docs/design/stage_typeset_playbook.md`.

# Round 4 · L (library): notes
All changes are additive with safe defaults; R's Replay/Analyse were smoke-rendered against the new lib (no errors).
New APIs are listed in README.md ("Round 4"). Finals: `out/r4_lib_*`.

## What changed
- **D5 stub.** Not measured is a short flat ink3 stub on the baseline in ShotStrip, Barcode, Sparkline, RunChart, SmallMultiple and the PageKey ("a stub not measured"). Missing values print "-".
- **D6 grades.** `Roles.linkGrades` ports `styleOf()`/PpChainLink. CauseGraph strokes: solid 2.0 (moved together), solid 1.5 (conditionally dependent), dashed 1.5 (coherent), dotted 1.0 (present together), dotted 1.0 at 0.5 opacity (unanchored). Arrowheads ONLY on the first two. PathChains, the path ledger and the key use one glyph per grade: ⇒ → ╌ ∼ ⋯.
- **D7 length.** Nothing assumes 15. Strips reserve `stripWidthFor(n)` (n × pitch up to 22 shots), narrow to `stripMinPitch`, then wrap. The Barcode narrows to a 12-unit cell, then wraps into bands (each with its head and per-shot row); the ShotRuler narrows to its figures, then wraps. Axis numbers thin (`axisLabelShown`). "Shot N of M" is the ordinal (`shotNo`).
- **LIB_REQUESTS, all done:** SparkHead; ShotRuler; Barcode per-row atoms, repeatable head, `rowsAll`, value fold; DotPlot `rowsFitting`, `showTier`, head as one atom, rows as atoms; Pager adopts PrintPager; PathChains print lists, Open in place, glyphs; PageKey merge; KeyFigures `exclude`; Standfirst `section`; Questions `opts` + `parts`; FeatureHead `showPull`/`showFacts`; the SmallMultiple band.
- **Ported for R:** LedgerTable focus link (F33), evidence line (F30), live recency (F29); FeatureHead honesty mark (F56) and norm citation in place (F51/D10); FigureRow hover readout (F53); CauseGraph `screenRequested` (F64); Tabs miss chip (F66); InlinePicker (F67); Expectations (F41); Brush.pulse + PulseWash (D1); `interactive` on every block with a mouse area (D4).
- **Print.** Print.qml now runs on the library Pager and PathChains, and PrintPager/PrintPaths are deleted. The barcode prints row by row with its head repeated. Path lists repeat their crosshead. KeyFigures uses `exclude`. The norm citation prints as a footnote under the figures. Portrait is 7 pages; landscape is 10 (Vector).

## Decisions beyond the brief
1. "Moved together" is full ink at the heaviest weight, not colorAccent (RULES 7: accent is a live control and colour means "out"). The other four grades are exactly as before.
2. Coherent's glyph is ╌ (it mirrors its dashed stroke and carries no arrow). The brief's example "→ coherent" would have given it a direction it has not earned.
3. In CauseGraph the likely driver's path keeps full strength and the others fade to 0.6. Width is now the grade's job. In the causes outline, arrows point up, at the effect.
4. Axis numbers are ordinals and brushing uses shot ids. Bookend and question shot numbers stay ids, as the model gives them.
5. Strip column cap: 22 shots' pitch. Barcode minimum cell: 12 units. ShotRuler minimum pitch: the width of "88". Below these, the figure wraps rather than shrinks further.
6. SmallMultiples spend the window top per ROW (the "empty band" was a fixed z = 6 window over shots that sit near the pass edge). Tiles in a row stay identically scaled. `commonTop: true` restores one window for the whole grid.
7. Barcode names are measured. Inline only while name + folded value fit in 42 % of the figure; otherwise above the bars (they clipped before).
8. The Pager's page window is a layer at 2× on white paper. Folio's curve-rendered marks ignored the scissor clip and leaked into the running head.
9. Norm citation: on screen, the Norm fact ends in "The source", which opens it in place. In print it is a footnote, not inside the fact column.
10. DotPlot stays rich between 50 and 52 em, with the tier folded onto the name ("pattern · Over the top"). This changes layouts at those widths, as requested.
11. KeyFigures in the live tense shows "Last fired: …" where there is no firingsAfter. FeatureHead's mark line is `kindText · mark`.
12. Tabs: the review note hides when it would collide with the tabs or the chip (it is tier 3 and already folds below medium).
13. The `hoverWash` is stronger (light 0.045, dark 0.065) and still lighter than the pick.
14. A stacked PathChains section carries its own rule inside its head, so a page break never strands it.
15. The LedgerTable's continuous pick column is drawn only while the strip is one line. A wrapped strip shades its own cell.
16. ShotStrip cells expose `cellState`, not `state`: Item.state would try to enter a QML State named "fired".

## objectNames added (INVENTORY §5)
- **Tabs:** sdTabBar · sdTab(key,on) · sdTabLabel · sdTabCount · sdReviewNote · sdMissChip (chipObjectName).
- **DetailBar:** sdDetailBack · sdDetailTitle · sdDetailFocus(focused).
- **Standfirst:** sdStandfirst · sdFilterTool(key,on), also on LedgerTable tools.
- **LedgerTable:** sdFiredTable / sdSessionTable / sdWatchingTable / sdPathTable · sdLedgerHead · sdPickColumn · rows sdFiredRow / sdSessionRow (conditionId, cardId, stateKind, rankText, thisShot, focused) · cells `<row>` + Rank, Name, Fired, Shot, Median, Unit, Trend, Typical, Worst, Best, Value, OutBy, Corridor, Tier (same name when folded) · sdSessionRowLine2 · sdSessionRowEvidence · sdSessionRowFresh · sdSessionRowFocus(focused) · sdSessionRowFocusBar.
- **DotPlot:** sdDotPlot · sdDotHead · sdDotRow(conditionId, stateKind, tierTag).
- **Barcode:** sdBarcode · sdBarcodeHead · sdBarcodeRow(conditionId) · sdBarcodeValue.
- **Strips, rows, tiles:** sdShotStrip · sdShotCell(shotId, cellState, picked, hovered) · sdShotStub · sdShotRuler · sdRulerCell(shotId, count) · sdSparkRow(cardId) · sdSparkRowName · sdSparkRowShot · sdSparkHead · sdSparkStub · sdSmallMultiple(cardId) · sdTileStub.
- **Detail:** sdFeatureHead · sdDetailMark · sdNormCitation · sdKeyFigures · sdFigureRow · sdDetailHoverReadout · sdSpreadCaption · sdHistoryCaption · sdRunYTick.
- **Figures:** sdSpreadStrip(hoveredShot) · sdStripDot(shotId, index, current, hovered) · sdRunChart(hoveredShot) · sdRunMark · sdRunSelected(shotId) · sdRunStub · sdHistoryStrip · sdHistoryColumn(current) · sdHistoryMedian.
- **Causes:** sdCauseGraph · sdCauseNode(conditionId, kind, mark) · sdCauseEdge(grade, strokeStyle, hasArrow, strokeOpacity/opacity) · sdScreenCta(conditionId) · sdDetailCauseHeadline · sdDetailOutcomeHeadline · sdDetailCappedNote · sdDetailRival · sdPathChains · sdPathChain(which, index, open) · sdPathOpen(open).
- **Other blocks:** sdPageKey · sdQuestions · sdQuestion(questionId) · sdAnswer(questionId) · sdMissPicker · sdMissPickerLabel · sdMissCandidate(missId, on) · sdMissClear · sdExpectations · sdExpectationCard · sdPulseWash.
- **Not added (they belong in the registers):** sdCoverageLine, sdShotEmpty, sdWatchingEmpty, sdQuietState/Line, sdNotMeasuredNote, sdFilterEmpty, sdColdHeadline, sdDetailMeasureTabs, the views and flicks.

## Not done / open
- Analyse detail: the PageKey sits tight on the FigureRow panel titles. This was already so in r3, and the spacing is R's.
- The Folio section label still names the section a page ENDS in (NOTES3_print "Unsure").
