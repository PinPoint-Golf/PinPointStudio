# C. Charts, plots and data-visualisation components: duplicate-code findings

Scope: `src/Gui/charts`, `review`, `typeset`, `diagnostics`, `trust` (QML plus the GUI-layer C++ they bind to).
Read-only pass against the working tree on 19 Oct 2026. Another agent was editing, so line numbers are as seen and may drift a few lines.
Paths are relative to `src/Gui/` unless a full path is given.

## Headline

1. **The "one box plot" rule holds for drawing.** Every box is drawn by `typeset/PpBoxPlot.qml`, and every quantile and whisker comes from `typeset/box_stats.h` (`quantile7`, `boxStats`). The C++ `spreadQuantile`, `session_curves.h::medianOf` and the `session_warp`, `session_traces` and `session_sequence` quartiles all forward to it. I found no second implementation in QML or C++. What is *not* single-sourced is the three thin adapters around PpBoxPlot, plus one miniature box glyph in the page key (C6).
2. **The largest duplicate mass is the old `review/` chart stack.** About 3,000 lines are dead or reachable only from test probes, and a further ~1,300 are dead as a chain (C1, C2, C3).
3. **The next two masses are the "fault-forward figure" scaffolding in diagnostics (C4, C5) and the Charts-vs-Diagnostics twin components (C9 to C12).** Both stacks were built from the same design vocabulary and the same typeset library, but each re-wrote the same blocks.
4. **C++ is largely well-unified for statistics, but the *number-formatting* and *tick* helpers are re-written 3 to 6 times** (C15, C16).

---

### C1. MetricLibrary / MetricDetail / MetricRow / NormativeBar: a dead chain, superseded by the Diagnostic Model panel
- **Verdict:** DEAD / SUPERSEDED
- **Where:**
  - `review/MetricLibrary.qml` (334 lines, `used_by` empty). `grep` over `src/` finds no instantiation. Only a comment in `review/MetricRoute.qml:24` and `docs/developer/metric_catalogue_developer_guide.md:38` mention it.
  - `review/MetricDetail.qml` (567), whose only user is MetricLibrary (`review/MetricLibrary.qml:321`).
  - `review/MetricRow.qml` (215), whose only user is MetricLibrary.
  - `review/NormativeBar.qml` (200), whose only user is MetricDetail (`review/MetricDetail.qml:387`).
  - `review/PpReplayCharts.qml` (73, `used_by` empty). It was the full-mode host of PpMetricChart (see C2).
  - `trust/PpGateNote.qml` (74, `used_by` empty). See C13.
- **What is duplicated:** The metric directory and detail screen were replaced by the Diagnostic Model panel. `settings/ScreenSettings.qml:90-95 showMetricDetail()` now routes to `diagnosticModelPanel.showMetric(key)`, not to MetricLibrary. `review/MetricRoute.qml` is a live singleton and the receiving side in `shell/Main.qml:747` is wired. The four-file chain is ~1,316 lines that nothing mounts. The three identical `_typeName(t)` switches that `duplicates.json` reports (`review/MetricDetail.qml:53`, `review/MetricRow.qml:46`, `review/MetricLibrary.qml:73`) are copies inside this dead chain, so deleting it removes that duplicate hit entirely. `PpReplayCharts` is superseded by `charts/PpChartsPanel` (hosted by `session/ScreenSessionMode.qml:141` and `session/ScreenWrist.qml:151`), per the stage typeset audit that lists "Charts (`review/PpReplayCharts.qml` → `PpMetricChart.qml`)" as the surface to replace.
- **Shared component/module it should fold into:** Delete the chain. Keep `MetricRoute.qml` (live). `NormativeBar`'s domain maths already lives in C++ (`review/chart_metrics.h:465`, `Coaching/dashboard_reductions.h`), so nothing is lost.
- **Effort / risk:** S. Check that the test CMake lists do not reference them (`session_setup_lint_test` and others grep QML paths) and that `PP_QML_FILES` is edited together.
- **Confidence:** high for MetricLibrary, MetricRow, MetricDetail, NormativeBar and PpReplayCharts. Medium that Mark has no plan to revive the Metric Library (the `MetricRoute` header says "CURRENTLY NO PRODUCER" but keeps the receiving side "for the next surface").

### C2. The review/ chart stack is, in production, only a compact plot inside the camera tiles
- **Verdict:** DEAD / SUPERSEDED (largely) and PARALLEL IMPLEMENTATION (of the Charts panel)
- **Where:**
  - `review/PpMetricChart.qml` (1,549 lines). Its only production mount is `cameras/PpCameraTiles.qml:367`, with `compact: true`. In compact mode, `PpMetricChart` hides the toolbar, brush, summary and sequence strip: lines 1003, 1056, 1065, 1123, 1186, 1204, 1231, 1406, 1421, 1513 and 1522 gate on `!root.compact`.
  - The full-mode host `review/PpReplayCharts.qml` is unreferenced.
  - `review/PpChartSummary.qml` (703). Its only users are PpMetricChart (non-compact) and the probe `shaft_lie_card`.
  - `review/PpSegmentBrush.qml` (364). Its only user is PpMetricChart (`review/PpMetricChart.qml:1185`, non-compact).
  - `review/PpSequenceStrip.qml` (297). Its only user is PpMetricChart (`review/PpMetricChart.qml:1405`, non-compact).
  - `review/PpChartPlot.qml` (1,007) stays live through the compact tile.
  - Other users are tests and probes only: `tst_chart_presets`, `plumb_bob_chart`, `instant_cards`, `ks_overlay_chart`, `ks_rising_chart`, `shaft_lie_card`, `metric_colours`.
- **What is duplicated:** The full-mode chart (presets, split and overlay, brush, summary tiles, sequence tile) has a complete Charts-panel twin that is the one users reach: `charts/PpChartsPanel` → `PpChartsShotView` / `PpChartsSessionView`, with `PpChTraceFigure` (575), `PpChSummary` (126), `PpChWindowBrush` (158), `PpChSeqTable` / `PpChSeqChain` and `PpChKeyFigures`. The Charts C++ presenters even call the old stack's C++ (`charts/charts_traces_presenter.cpp:1119` calls `ChartMetrics::sequenceTable`), so the maths is already shared and only the QML renderers are duplicated.
- **Shared component/module it should fold into:**
  - Reduce `PpMetricChart` to a "PpCompactTrace" (plot, legend and playhead) for the camera tiles, or give the tiles a `PpChPlot`-based trace.
  - Then drop PpChartSummary, PpSegmentBrush, PpSequenceStrip and the full-mode half of PpMetricChart, roughly 1,364 lines plus about 1,000 in PpMetricChart. They are only reachable by probes today.
  - If the full-mode screen is meant to return, say so, because the chart stack is then two live parallel implementations (C4, C8 to C10).
- **Effort / risk:** M to L. Seven probes and tests pin it, and PpChartPlot's honesty contracts (`tst_chart_presets`, `metric_presentation_honesty`) would need to move to PpChPlot or be re-pinned.
- **Confidence:** high on the reachability facts. Medium on the recommendation, which is Mark's call.

### C3. The measured / mean / nearest-sample predicates are re-written in four QML files, with a C++ reference implementation beside them
- **Verdict:** TRUE DUPLICATE
- **Where:**
  - `_measured(s, i)`: `review/PpChartPlot.qml:243`, `review/PpSegmentBrush.qml:105` (byte-identical bodies).
  - `_measuredIdx(s, i)`: `review/PpMetricChart.qml:602`. It is the same rule with the domain test inlined.
  - `_measuredAt(s, t)`: `review/PpChartSummary.qml:162`. It inlines the nearest-sample search plus the same three tests, about 20 lines.
  - `_hasDomain`: `review/PpChartPlot.qml:224` and `review/PpSegmentBrush.qml:98`, identical. A third and fourth copy are inlined in PpMetricChart's `_measuredIdx` and PpChartSummary's `_measuredAt`.
  - `_hasMean`: `review/PpChartPlot.qml:275`, `review/PpChartSummary.qml:135` and `review/PpMetricChart.qml:648`, all identical.
  - `_meanOf`: those three plus `review/PpSegmentBrush.qml:121`, which inlines `_hasMean`.
  - `_nearestIndex`: `review/PpChartPlot.qml:252` and `review/PpMetricChart.qml:585`, identical linear scans. A third copy is inlined in PpChartSummary `_measuredAt`.
  - C++ reference: `ChartMetrics::measuredAt` (`review/chart_metrics.cpp`) and `SeriesView::isValid`.
- **What is duplicated:** The same short-mask, non-finite and phase-domain validity rule (`valid.length < t_us.length` discards the mask wholesale, NaN is not a measurement, domain end snapped on the nearest sample's time) exists as four QML functions plus the C++ original. The C++ header and the tests (`tests/chart_metrics_test.cpp:642, 1282, 1475, 1588`) name the copies and pin them as "THREE places consult validity". The code comments say "keep them equal" in at least five places. The QML copies exist deliberately to avoid marshalling a whole series per call.
- **Shared component/module it should fold into:** Either `review/chart_series.js` (a `.pragma library` with `measured`, `hasDomain`, `hasMean`, `meanOf`, `nearestIndex`), or, better, have `PpMetricChart._plottable` (which already decorates `mean`) also decorate a `measured` Uint8 mask once per data change, so consumers just read `s.measured[i]`. Falls away entirely if C2 is actioned.
- **Effort / risk:** S for the `.js` library. M for the mask decoration, which needs the honesty tests re-pinned.
- **Confidence:** high.

### C4. Two multi-series curve painters: PpChartPlot (Shape, 1,007 lines) and PpChPlot (Canvas, 168 lines)
- **Verdict:** PARALLEL IMPLEMENTATION (also a Canvas-vs-Shape split)
- **Where:** `review/PpChartPlot.qml` (Shape with `Shape.CurveRenderer` / `GeometryRenderer`, `ShapePath` per run, lines 573-690) against `charts/PpChPlot.qml:34-168` (one Canvas, layers `band | line | dots | vline | hline | bridges | span`). PpChPlot is reused by four Charts figures: `PpChKindCurve`, `PpChReadingTrace`, `PpChSessionFigure`, `PpChTraceFigure`.
- **What is duplicated:** Same job: windowed-mean line plus raw dots plus uncertainty ribbon plus dashed bridged runs plus dimmed "outside the phase window" runs plus P-position hairlines plus y-grid. Both implement the same F18 "honesty runs" (`measured` / `bridged` / `outside`) and the same "dashed run reaches one sample into each neighbour" rule, one on `ShapePath`s and one on Canvas strokes. They also carry independent map functions (`xForT` / `yForV` at `review/PpChartPlot.qml:160-164` against `xOf` / `yOf` / `tOf` at `charts/PpChPlot.qml:58-60`). PpChartPlot is also where peaks, σ whiskers and the sequence overlay live. PpChTraceFigure re-adds the same peak and whisker marks with its own Canvas (`charts/PpChTraceFigure.qml:260-340`).
- **Shared component/module it should fold into:** PpChPlot (the Charts one, already shared by 4 figures). If the camera-tile compact trace is kept, give it PpChPlot layers instead of PpChartPlot. Peaks and σ whiskers should become a layer kind (`k: "peak"`) in PpChPlot so PpChTraceFigure and PpChartPlot stop each drawing them.
- **Effort / risk:** L. PpChartPlot's honesty tests pin its behaviour. It becomes moot if C2 is actioned.
- **Confidence:** high that they are parallel. Medium that PpChartPlot's extras (σ ribbon variants, tooltips) all map onto PpChPlot layers.

### C5. The "fault-forward figure" scaffolding is re-implemented in each diagnostics figure instead of living in PpZonePlot
- **Verdict:** TRUE DUPLICATE (wash split, break slashes, stub, median polyline); PARALLEL IMPLEMENTATION (the z-scale)
- **Where:**
  - Corridor wash split at the break: `typeset/PpZonePlot.qml:33-42` (`washPieces`) is the same code as `diagnostics/PpSparkline.qml:67-75`, copied in full. PpSparkline also re-draws the fault line (`:76-84`), whereas PpZonePlot draws the fault line and pass edges for the other three figures.
  - Break-slash Canvas, with the same `k = u(n)` double-slash glyph: `typeset/PpZonePlot.qml:86-108`, `diagnostics/PpSparkline.qml:88-103`, `diagnostics/PpSmallMultiple.qml:187-197`.
  - The "not measured" foot stub rectangle (`width: max(2, min(u(n), width/n*k))`, `height: roles.stubH(u(m))`, `ink3`): `diagnostics/PpRunChart.qml:90-98` (`sdRunStub`), `diagnostics/PpSparkline.qml:106-114` (`sdSparkStub`), `diagnostics/PpSmallMultiple.qml:224-228` (`sdTileStub`), `charts/PpChRunBox.qml:103-108`, `typeset/PpShotStrip.qml:99`, `typeset/PpPageKey.qml:107`.
  - The break-aware median polyline Canvas ("the line breaks where the axis does"): `diagnostics/PpRunChart.qml:66-85` and `diagnostics/PpSmallMultiple.qml:203-218`. The two differ only in the region test.
  - The "distance to travel" dimension line (a bar plus ticks plus 'N to the pass edge' label with the flip-side logic): `diagnostics/PpRunChart.qml:136-232` (about 95 lines including the maths) and `diagnostics/PpSmallMultiple.qml:109-124` plus `:243-262`.
  - `PpSmallMultiple` keeps its own piecewise z-scale (`ty(z)` at `:177-181`) which parallels `typeset/PpFigureScale.y(f)` (compressed band, break, linear window), because tiles work in z units and not axis fractions.
- **What is duplicated:** Each figure repeats the same dozen small visual primitives. PpZonePlot is documented as "the common ground every value panel stands on" and PpRunChart, PpBoxStrip and PpHistoryStrip use it. PpSparkline and PpSmallMultiple sit outside it and re-draw the wash, break, fault line and stubs.
- **Shared component/module it should fold into:**
  - Make PpSparkline a PpZonePlot client (`PpZonePlot { scale: sc }`), removing about 35 lines.
  - Add a `PpFigureScale` mode for z-ladder input (map `z` to `f` once, in C++ or in `zOf`), so PpSmallMultiple can use PpFigureScale and PpZonePlot as well.
  - New small typeset components: `PpNotMeasuredStub { roles, x, baseY, pitch }`, `PpMedianLine { scale, points, medium }` and `PpDistanceToTravel { scale, shot, edges, flip }`.
- **Effort / risk:** M. The probes `session_spread`, `detail_hover` and `tst_session_diagnostics.qml` look up objectNames (`sdRunStub`, `sdSparkStub`, `sdTileStub`), so the names need preserving through a `namePrefix` property.
- **Confidence:** high for the wash, break and stub copies. Medium for the unified z-scale.

### C6. One box plot: rule holds, but three adapters repeat the marks-and-style assembly, and the page key hand-draws a box glyph
- **Verdict:** LEGITIMATE VARIANT (the three adapters keep their data shapes) plus CANDIDATE UNIFICATION (the shared marks and style assembly)
- **Where (audit):**
  - Grep over `charts review typeset diagnostics trust` for `whisker|quartile|q1|iqr|percentile|quantile` outside `typeset/PpBoxPlot.qml` finds nothing that draws or computes a box: only comments, tests, and presenter field names.
  - `diagnostics/PpBoxStrip.qml` and `diagnostics/PpHistoryStrip.qml` both instantiate PpBoxPlot (`:154` and `:129`).
  - `charts/PpChBoxCell.qml:101` is the third adapter, used by `PpChKindTable`, `PpChProfile`, `PpChReadingTrace`, `PpChRunBox`, `PpChScatter` and `PpChWristProfile`.
  - `review/NormativeBar.qml` is a corridor bar (not a box), and is dead anyway (C1). `charts/PpChSessionFigure.qml:95` draws a metric envelope band (the middle-half area on a curve) and not a box. `charts/PpChMovesWith.qml` draws a CI rule on −1…1.
  - `typeset/PpPageKey.qml:108-114` draws a miniature box (3 Rectangles) as the key glyph, with hard-coded alphas (`0.10` fill, `1.5` median weight) that are copies of the PpBoxPlot defaults.
- **What is duplicated:**
  - PpChBoxCell (`charts/PpChBoxCell.qml:78-98`) and PpBoxStrip (`diagnostics/PpBoxStrip.qml:138-151`) each build the same `marks` list (outliers plus the picked shot ringed) and the same hovered-shot `echo`, from different input shapes.
  - The style bundle is repeated three times: `lw: isPrint ? hw : 1`, `medW: isPrint ? 1 : 1.5` and `fill: Qt.alpha(ink3, .10)` at `charts/PpChBoxCell.qml:110-117`, `diagnostics/PpBoxStrip.qml:119-120, 164`, `diagnostics/PpHistoryStrip.qml:82, 140`.
- **Shared component/module it should fold into:**
  - Add a `style: "screen"|"print"` property to PpBoxPlot that sets `lw`, `medW` and the fill default. This is 6 lines saved per adapter and removes the drift risk.
  - Optionally add `PpBoxPlot.shotMarks(outlierIds, pickedId, hoveredId, valueOf)` as a helper that the two shot-level adapters call.
  - Draw the PpPageKey glyph by instantiating `PpBoxPlot` with `stats: {lo:.0,q1:.25,med:.45,q3:.75,hi:1}` over a 22×12 item, so the key can never drift from the plot.
- **Effort / risk:** S for the style property and the key glyph. M for the shared marks helper. Low risk if the `sdBox*` / `chBox*` objectNames are kept.
- **Confidence:** high. The rule-compliance conclusion is high. The helper is a taste call.

### C7. Run chart ("shot by shot") exists twice: charts/PpChRunBox and diagnostics/PpRunChart (plus PpSparkline)
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `charts/PpChRunBox.qml:19-131` against `diagnostics/PpRunChart.qml:19-241`, with `diagnostics/PpSparkline.qml` as the compressed form. Both use `PpMark` per shot in order struck, a stub for a not-measured shot, the shot-number axis, a fired mark and a ringed pick, and a box to the right. The shot-number label rule is the same: `step === 1 || roles.axisLabelShown(index, n, max(5, step), [picked, hover])` at `charts/PpChRunBox.qml:112` and `diagnostics/PpRunChart.qml:127`. Both lean on the shared `PpTypeset.shotLabelStep` / `axisLabelShown`, so only the delegate is duplicated.
- **What is duplicated:** About 40 lines of mark, stub, label and caption scaffolding. The scale differs on purpose: PpChRunBox maps raw value to y with its own `vy` (`:54`), and PpRunChart maps the model's fraction through PpFigureScale. The Charts one has a drift line, the diagnostics one has the fault-forward corridor and the distance dimension.
- **Shared component/module it should fold into:** Treat the two as variants of one `PpRunPlot { points, scale, marksFor }` in `typeset/`. The Charts side would feed it a linear scale (PpFigureScale with no bands) and the diagnostics side a fault-forward scale. The shot-number axis row should become `PpShotAxisLabels { roles, n, pitch, picked, hovered }`.
- **Effort / risk:** M. Both are heavily probed (`chRun`, `sdRunChart`, `sdRunMark`).
- **Confidence:** medium. They are conceptually the same figure, but the diagnostics one carries much more.

### C8. Kinematic-sequence tile: PpSequenceStrip (review) and PpChSeqTable (charts), including a pasted "Sequence checks" block
- **Verdict:** TRUE DUPLICATE (the checks block, about 55 lines) and PARALLEL IMPLEMENTATION (the table)
- **Where:** `charts/PpChSeqTable.qml:110-185` (Item `chk`) and `review/PpSequenceStrip.qml:196-278` (Item `chk`): same ids, same properties (`cs`, `list`, `pinned`, `open`, `labelW`), same `ink(state)` switch, same probe-text baseline-alignment trick, same hover-lists and tap-pins behaviour. Only Theme tokens versus `roles` differ. `duplicates.json` also records a 7-line clone of the Repeater. The tables themselves are `charts/PpChSeqTable.qml` (199) and `review/PpSequenceStrip.qml` (297), both fed by the same C++ (`ChartMetrics::sequenceTable`, `Coaching/sequence_checks.h:334` says "PpSequenceStrip and PpChSeqTable bind it").
- **Shared component/module it should fold into:** `typeset/PpSequenceChecks.qml { roles, checks }` (one component, used by PpChSeqTable). PpSequenceStrip dies with C2.
- **Effort / risk:** S if C2 is actioned (just delete the old one). S to M otherwise.
- **Confidence:** high.

### C9. Key figures row: charts/PpChKeyFigures and diagnostics/PpKeyFigures
- **Verdict:** PARALLEL IMPLEMENTATION (the column logic is a TRUE DUPLICATE)
- **Where:** `charts/PpChKeyFigures.qml:39` and `diagnostics/PpKeyFigures.qml:58`, an identical line: `cols: Math.max(1, fit >= n ? n : fit >= Math.ceil(n/2) ? Math.ceil(n/2) : Math.ceil(n/4))`, driven by `roles.columns(width, roles.flows.keyFigure, 0)`. Both draw a heavy rule, then a grid of label / figure / foot cells with hairline dividers. Row height is 58 against 50 `u`.
- **What is duplicated:** About 25 lines of grid maths and cell chrome. The Charts one adds the metric swatch, the unit run, the warn word and the gated "›" link.
- **Shared component/module it should fold into:** `typeset/PpKeyFigureGrid.qml` with a `delegate`/`cellHeight`, or make PpChKeyFigures and PpKeyFigures thin clients of it. At minimum put `columnsFor(n, fit)` in PpTypeset (`roles.wrapColumns(n, width, minEm)`).
- **Effort / risk:** S.
- **Confidence:** high.

### C10. Register scaffolding triplicated: `ViewFlick` ×3, cast-zoom formula ×2, view-head preamble ×5
- **Verdict:** TRUE DUPLICATE
- **Where:**
  - Inline `component ViewFlick: Flickable` in `charts/PpChartsRegister.qml:107-123`, `diagnostics/PpDiagAnalyse.qml:255-270` and `diagnostics/PpDiagReplay.qml:185-200`. Same 13 lines. Only `topMargin`, the `+ ro.u(n)` content padding, and whether `interactive:` is set differ.
  - The cast-zoom line `cast ? Math.max(1, Math.min(1.6, width/(42*ro.em), height/((castEm+4)*ro.em))) : 1` at `charts/PpChartsRegister.qml:105` and `diagnostics/PpDiagReplay.qml:209`. The surrounding `surfaceItem` (width / zoom, scale, `transformOrigin`) and the `castHead` and PpTabs block repeat at `charts/PpChartsRegister.qml:125-160` and `diagnostics/PpDiagReplay.qml:210-245`.
  - The five Charts views `charts/PpChartsShotView.qml`, `PpChartsSessionView.qml`, `PpChartsRelationsView.qml`, `PpChartsKindsView.qml` and `PpChartsReadingsView.qml` all open with the same 7 properties (`roles`, `brush`, `body`, `reg`, `interactive`, `cast`, `pageH`) and `objectName`. `PpChartsSessionView` and `PpChartsShotView` also share a 10-line `PpInlinePicker` preset block (`charts/PpChartsShotView.qml:315-326`, `charts/PpChartsSessionView.qml`).
  - The same shell shape sits in `diagnostics/PpDiagAnalyse.qml` (756 lines) against `diagnostics/PpDiagReplay.qml` (555), which also share a 12-line `firePulse()` and a 12-line header property block.
- **Shared component/module it should fold into:** `typeset/PpViewFlick.qml` (a Flickable-with-Loader that takes `shown`, `key`, `view`, `topMargin`) and `typeset/PpCastSurface.qml` (zoom, castHead, `castEm` callback). Have the register own the pulse cue (`firePulse`) in a shared `PpPulseCue` helper.
- **Effort / risk:** M. These are the register frames both panels depend on, and there are many probes (`diag_nav_survives_mode_switch`, `stage_separators`, `tst_charts_panel`).
- **Confidence:** high.

### C11. Panel shell: folder normaliser ×4, `_pointAtSession` ×3, reader's-place and scroll-restore ×2, focus-shot-by-dir ×2
- **Verdict:** TRUE DUPLICATE
- **Where:**
  - `_folder(p)`: `charts/PpChartsPanel.qml:91`, `trust/PpTrustPanel.qml:44`, `trust/PpTrustSummaryFold.qml:60` (all identical) and `diagnostics/PpSessionDiagnosticsPanel.qml:153` (the same without the `String(p||"")` guard).
  - `_pointAtSession()`: `charts/PpChartsPanel.qml:92` and `trust/PpTrustPanel.qml:45` (identical, flagged in `duplicates.json`), `diagnostics/PpSessionDiagnosticsPanel.qml:154-157` (expanded form), and `trust/PpTrustSummaryFold.qml:65` (`_point`).
  - The four-line `readonly property string sessionDir:` ternary over `sessionReviewController` / `shotProcessor` repeats in `charts/PpChartsPanel.qml:87`, `trust/PpTrustPanel.qml:40` and `diagnostics/PpSessionDiagnosticsPanel.qml:133`.
  - About 90 lines of reader's-place code (`_navFlicks`, `_findNamed`, `_flick` cache, `_watchFlicks`, `_saveNav`, `_restoreNav`, the `scrollRestore` Timer with its 50 ms / tries loop, and the `flickWatch` Instantiator): `charts/PpChartsPanel.qml:258-340` against `diagnostics/PpSessionDiagnosticsPanel.qml:214-330`. `duplicates.json` flags 4 clones of 8 to 12 lines each. Only the nav payload and the flick names differ.
  - The "find the carousel shot by swingDir, then enter Analyse or Replay" lookup (`charts/PpChartsPanel.qml:350-361`, `diagnostics/PpSessionDiagnosticsPanel.qml:398-410`), with a third copy in `session/PpLaunchMonitorPanel.qml`.
- **Shared component/module it should fold into:** `session/SessionPaths.js` or a tiny `PpSessionPath` singleton for `folder()`. A `PpReaderPlace.qml` (QtObject with `flicks`, `body`, `save`, `restore`, a `payload` callback) for the scroll and place logic. A `SessionMode.focusShotByDir(dir)` function for the lookup.
- **Effort / risk:** M. These panels are central to mode switching, and the existing comments record hard-won ordering bugs.
- **Confidence:** high.

### C12. The linear value-to-pixel maps and the y-axis gutter are re-written in each Charts figure
- **Verdict:** TRUE DUPLICATE (maps) / PARALLEL IMPLEMENTATION (gutters)
- **Where:**
  - `vy(v) = pTop + (1 - (v-lo)/max(1e-9, hi-lo)) * plotH`: `charts/PpChRunBox.qml:54` and `charts/PpChReadingTrace.qml:55` (byte-identical, as `duplicates.json` reports), `charts/PpChKindCurve.qml:55` (`yr.lo/hi`), `charts/PpChScatter.qml:59` (`py`, same maths), `charts/PpChWristProfile.qml:105`, `charts/PpChBoxCell.qml:70`, `charts/PpChPlot.qml:59` and `charts/PpChTraceFigure.qml:91`. Time and x variants: `tx` in KindCurve and ReadingTrace, `xOf` in PpChSeqSession, PpChTraceFigure and PpChWindowBrush, `px` in Scatter.
  - The y-tick gutter (a hairline Rectangle at `Math.round(vy(v))`, a right-aligned label at `gutL - width - u(5)`, `fFigLabel`, `ink3`): `charts/PpChRunBox.qml:66-72`, `charts/PpChReadingTrace.qml:62-67`, `charts/PpChKindCurve.qml:62-68`, `charts/PpChScatter.qml:81-87`, `charts/PpChWristProfile.qml:136-144`. Five near-identical Repeaters.
  - The P-position hairline-plus-tag Repeater: `charts/PpChReadingTrace.qml:70-82` and `charts/PpChKindCurve.qml:70-78`. The same idea is drawn inside PpChPlot's `pTicks`, so the two draw it in different layers.
  - The same `v` to `(v-lo)/(hi-lo)` map appears in `typeset/PpFigureScale`, `diagnostics/PpDotPlot.xs`, `review/PpSegmentBrush.xs/ys` and `review/NormativeBar._fx`.
- **What is duplicated:** About 15 copies of one affine map and 5 copies of the axis-gutter block (about 7 lines each).
- **Shared component/module it should fold into:** `charts/PpChYAxis.qml { roles, ticks, lo, hi, plotTop, plotH, gutL, unitText }` and `PpChXAxisMs`. A `PpChScale` QtObject (`lo`, `hi`, `top`, `h`, `y(v)`, `v(y)`) so the figures pass a scale rather than each owning `vy`. Charts figures could also hand the axis to PpChPlot as `yTicks` and let the presenter's tick text ride along.
- **Effort / risk:** S to M. Mechanical, and the `chRun` / `chTrace` object names are untouched.
- **Confidence:** high.

### C13. The gated-value "words ›" link idiom is open-coded ~15 times, and the shared component (PpGateNote) is unused
- **Verdict:** CANDIDATE UNIFICATION (and DEAD for PpGateNote)
- **Where:** `trust/PpGateNote.qml` (74 lines: link-coloured words with " ›", click opens `TrustFocus.open(subject, swingDir)` or expands `PpTrustInlineFor`) is referenced by nothing. The same behaviour is written by hand in:
  - `charts/PpChKeyFigures.qml:70, 81`
  - `charts/PpChSummary.qml:98`
  - `charts/PpChSeqTable.qml:84`
  - `charts/PpChSeqChain.qml:70`
  - `charts/PpChProfile.qml:153`
  - `charts/PpChTraceFigure.qml:469, 517`
  - `charts/PpChWristProfile.qml:248, 276`
  - `charts/PpChWristShotSection.qml:86, 152`
  - `diagnostics/PpLedgerTable.qml:146`
  - `diagnostics/PpFigureRow.qml:235`
  - `diagnostics/PpDiagReplay.qml:411` and `diagnostics/PpDiagAnalyse.qml:471` (the "not judged" footer groups, ~14 lines each, near-identical)
  - `trust/PpTrustSummaryFold.qml:103`
  - `review/PpDataViewer.qml:328`
  - The "open in panel else toggle inline" handler: `charts/PpChartsShotView.qml:69-74` (`openTrust(subject, origin)`) and `charts/PpChWristShotSection.qml:71-75` (`openTrust(subject)`), plus the inline `onClicked: if (!TrustFocus.open(...)) toggle` in PpDiagReplay, PpDiagAnalyse, PpTrustSummaryFold and PpFigureRow:242.
- **What is duplicated:** The link colour, the `›`, the MouseArea margins `-2`, the hand cursor, and the panel-else-inline decision. The two `not judged` footers (`nj` / `njFoot`) are a pasted block.
- **Shared component/module it should fold into:** Revive and extend PpGateNote: a `PpGateLink { roles, text, subject, swingDir, printed }` that emits `opened` and owns the TrustFocus-else-inline fallback through a shared `PpTrustToggle` (a `{openSubject}` QtObject), plus `PpNotJudgedFooter { groups, roles }` for the two diagnostics copies.
- **Effort / risk:** M. Touches many files, but each use is tiny. Print behaviour (`printed`: words only) must be preserved.
- **Confidence:** medium to high.

### C14. Ledger tables: spec-driven PpLedgerTable against hand-laid column tables in Charts
- **Verdict:** CANDIDATE UNIFICATION
- **Where:** `diagnostics/PpLedgerTable.qml` (555 lines) lays columns from `roles.tiers[kind]` specs (`:48, 90-103`: `vis` / `fold` / `colW` / flex). The Charts tables compute widths by hand and call `roles.shows(n, width)` for single tiers: `charts/PpChSeqTable.qml:36-46` (`segW`, `rankW`, `peakW`, `speedW`, `pctW` as `Math.round(k*em)`), `charts/PpChKindTable.qml:47`, `charts/PpChProfile.qml:64`, `charts/PpChMovesWith.qml:44-46`, `charts/PpChWristLedger.qml`. Header Text rows and row Texts both repeat width and alignment per column.
- **What is duplicated:** The column-with-tier-fold-and-second-line idea ("fo", "fold" in PpDiagRoles specs, "tier 3 folds to line 2" in each Charts header comment) is implemented once data-driven and five times by hand.
- **Shared component/module it should fold into:** Lift the column spec and layout (`vis`, `fold`, `colW`, flex) out of PpLedgerTable into `typeset/PpColumns.qml` / `PpTypeset.columnsFor(spec, width)`. Charts tables supply a spec through `PpChRoles.extraTiers` and keep their own cell delegates.
- **Effort / risk:** M to L. The Charts tables carry presenter-specific cells.
- **Confidence:** medium.

### C15. True-minus figure formatting written 6 times in C++, plus QML twins
- **Verdict:** PARALLEL IMPLEMENTATION (mostly TRUE DUPLICATE)
- **Where:**
  - `charts/charts_words.h:57 fig()` (U+2212, "-" when missing, −0 handled, rounding to `dec`).
  - `diagnostics/session_spread.h:375 spreadNumber()` (the same, plus `trimZeros`).
  - `diagnostics/session_diagnostics_model.cpp:76 fmtNumber(v, decimals)` (same, but **missing is an em dash "—"**, against "-" everywhere else, even though project convention is "- is NA").
  - `diagnostics/work_ons_controller.cpp:58 fmtNumber(v)` (one decimal fixed).
  - `review/chart_metrics.cpp:819 ChartMetrics::formatBare` (rounds identically to `fig`, calls `words::decimalsFor`, but prints an **ASCII hyphen** minus through `QString::number`, and adds a `+` for degrees).
  - `charts/charts_relations_presenter.cpp:88-96 r2()` (`kMinus`, correlation to ".62").
  - QML: `typeset/PpTypeset.qml:267 num(v)` (U+2212, a different rounding rule: whole if within 0.05 else 1 dp) and `diagnostics/PpDiagAnalyse.qml:112` (a local `fmt` with `.replace("-", "−")`).
- **What is duplicated:** One rule ("true minus, never −0, '-' for missing, decimals from the unit or the axis") is implemented with 4 slightly different rounding rules and 3 different missing markers. `formatBare`'s ASCII hyphen means the Charts panel and the compact camera-tile chart can print the same number with different minus glyphs.
- **Shared component/module it should fold into:** Make `charts/charts_words.h` (already a header-only namespace) the home, say `src/Gui/common/figure_format.h`. Export `fig(v, dec)`, `figTrim(v, dec)` and `na()`. `spreadNumber`, `fmtNumber` ×2, `formatBare` and `r2` become one-line forwards. Expose to QML through one `Figures` singleton so `PpTypeset.num` and the PpDiagAnalyse local go away.
- **Effort / risk:** M. Output strings are pinned by many tests (`charts_*_test`, `chart_metrics_test`, `session_diagnostics_test`), so any rounding difference shows as test churn. Pick the rule and re-baseline deliberately.
- **Confidence:** high that these are the same job. Medium on which rounding the owner wants.

### C16. Three C++ "nice ticks" implementations, three C++ `shortUnit`s
- **Verdict:** TRUE DUPLICATE (near-identical cutoffs differ)
- **Where:**
  - Ticks: `review/chart_metrics.cpp:626 ChartMetrics::niceTicks` (1-2-5 steps at cutoffs 1.5 / 3 / 7), `charts/charts_relations_presenter.cpp:2416 ChartsRelationsPresenter::ticks` (the same algorithm, cutoffs ≤1.5 / ≤3 / ≤7, adds `{v, text}`), and `diagnostics/session_spread.h:455 spreadTickValues` (the same, with cutoffs 1.5 / **3.5** / **7.5**).
  - `shortUnit`: `charts/charts_relations_presenter.cpp:104` (anonymous namespace), `review/chart_metrics.cpp:753 ChartMetrics::shortUnit` (same body, `%…` to `%`), and `diagnostics/PpDiagRoles.qml:115 shortUnit()` (QML, a different rule: strips " width" and " length").
- **What is duplicated:** A 1-2-5 tick generator and a `%`-prefix unit shortener, each written three times. The differing cutoffs (3.5/7.5 against 3/7) mean the same axis span can give different tick steps in the run chart and in a Charts plot.
- **Shared component/module it should fold into:** `figure_format.h` (see C15): `niceTicks(lo, hi, maxTicks, cutoffs = Default)` and `shortUnit`. The relations `ticks()` becomes `niceTicks` plus `fig(v, dec)`.
- **Effort / risk:** S. Pick the cutoffs deliberately, because the diagnostics strip tests pin ticks.
- **Confidence:** high.

### C17. QML text helpers duplicate C++ word helpers; diagnostics composes prose in QML while Charts forbids it
- **Verdict:** PARALLEL IMPLEMENTATION (helpers: TRUE DUPLICATE; the prose approach: an asymmetry to flag)
- **Where:**
  - `typeset/PpTypeset.qml:262-276 cap1 / word / countWord` (the 21-word list "no, one … twenty" at `:272`) against `charts/charts_words.h:98-116 word / countWord / cap1` (the same 21 words, same "none for a lone zero" rule).
  - `list(items)` ("a, b and c"): `diagnostics/PpQuestions.qml:69`, `diagnostics/PpDiagPrint.qml:83 listOf` (identical bodies, flagged), and `charts/charts_words.h:119 list()`.
  - `capitalised(s)` ×3 in `home/Hm*.qml` (flagged, outside this cluster) duplicates `PpTypeset.cap1`.
  - `diagnostics/PpDiagRoles.qml` is 229 lines, and about 120 of them (`shotStandfirst`, `sessionStandfirst`, `watchStandfirst`, `firedEntry`, `sessionEntry`, `recWords`, `trendWords`, `afterWords`) compose English sentences in QML from model fields. It also **parses C++-formatted strings back into parts**: `fig(v)` and `unitOf(v)` regex the model's `valueText`, `corridorShort` / `corridorParts` / `corridorClause` regex the sentence built in C++ at `diagnostics/session_spread.h:407 spreadCorridorWords`, and `recParts` regexes "13 of 15" from `recurrence`.
  - By contrast `charts/PpChRoles.qml` says "NO number and NO word is made here: every one comes from the presenters", and the Charts C++ builds `Sentence` objects.
- **What is duplicated:** The helper functions exist in both languages. More important, the diagnostics stack round-trips values through English text (C++ writes "pass up to 8.7 · fault at 13 % hand rise", QML regexes it), which is the QML↔C++ boundary smell. It also contradicts the `spec` rule that C++ decides numbers and words.
- **Shared component/module it should fold into:** Have `SessionDiagnosticsModel` publish the corridor parts (`corridor: {pass: "8.7", fault: "13", unit: "% hand rise"}`) and recurrence as `{n, of}` data next to the text, so `corridorShort/Parts/Clause`, `recParts`, `fig` and `unitOf` can be removed. Move standfirst and entry sentence composition into C++ `Sentence`s as the Charts presenters do. Keep the QML `cap1/word/countWord` only for the three label helpers that need them, or expose the C++ ones through the Figures singleton.
- **Effort / risk:** L for the prose move, S for the three tiny helpers. The sentences are heavily pinned in `tst_session_diagnostics.qml`.
- **Confidence:** high that the duplicates exist. Medium that the prose move is wanted.

### C18. Scrub-to-seek MouseArea written three times in Charts figures, each coupled straight to the `shotReplay` global
- **Verdict:** CANDIDATE UNIFICATION
- **Where:** `charts/PpChReadingTrace.qml:104-113`, `charts/PpChTraceFigure.qml:408-424`, `charts/PpChSessionFigure.qml:232-254`, plus older equivalents in `review/PpChartPlot.qml:963`, `review/PpTransitTimeline.qml:290` and `review/PpDataViewer.qml:377`.
- **What is duplicated:** The `seekTo(px)` function (clamp px, ask the presenter for µs, `shotReplay.seekToUs`), the press / release / cancel bracketing with `beginScrub` / `endScrub`, and the `typeof shotReplay !== "undefined"` guard. Two of the three differ only in which presenter method resolves the time.
- **Shared component/module it should fold into:** `charts/PpChScrubArea.qml { resolve: function(px) → us, enabled, cursor }` that owns the `beginScrub` / `seekToUs` / `endScrub` bracket (one place to fix the cancel-without-release leak).
- **Effort / risk:** S.
- **Confidence:** medium to high.

### C19. QML chart-series helpers: `byKey` ×2, `byId` ×2, and the phase and window helpers
- **Verdict:** TRUE DUPLICATE (small)
- **Where:** `byKey(list, key)` at `charts/PpChSessionFigure.qml:80` and `charts/PpChTraceFigure.qml:98`. `byId(list, id)` at `typeset/PpTypeset.qml:277` and `diagnostics/PpDiagReplayState.qml` (its `byId`). `_phaseUs(phase)` at `review/PpChartSummary.qml:189` and `review/PpMetricChart.qml:452`. The window-clamp pair `_winStart/_winEnd` (`review/PpChartSummary.qml:195-200`) and `_domWinStart/_domWinEnd` (`review/PpMetricChart.qml:508-519`), which the source comments say to "keep equal". `capitalised` ×3 (above).
- **Shared component/module it should fold into:** `PpTypeset.byKey/byId` (one generic `find(list, field, value)`), and the review ones die with C2.
- **Effort / risk:** S.
- **Confidence:** high.

### C20. Dash-pattern stroking loop hand-rolled three times
- **Verdict:** TRUE DUPLICATE (small)
- **Where:** `typeset/PpZonePlot.qml:74` (pass edge dash), `typeset/PpPageKey.qml:146` (pass or chance swatch) and `typeset/PpPageKey.qml:157` (link-grade stroke), each `while (x < width) { const st = pat[on ? 0 : 1]; if (on) { moveTo … lineTo(min(width, x + st)) } x += st; on = !on }`. `charts/PpChPlot.qml` and `charts/PpChScatter.qml` use `ctx.setLineDash(...)` for the same job.
- **Shared component/module it should fold into:** A `PpTypeset.dashPath(ctx, x0, y, x1, pattern)` helper, or `PpDashedRule.qml`. The hand loop exists to keep dashes crisp on half-pixel rows, so either keep it in one place or justify `setLineDash` everywhere.
- **Effort / risk:** S.
- **Confidence:** medium.

### C21. PpSmallMultiple re-derives the corridor ladder and pass and fault edges from `bands` in QML, in three different places
- **Verdict:** PARALLEL IMPLEMENTATION (QML maths that duplicates C++)
- **Where:**
  - `diagnostics/PpSmallMultiple.qml:56-80 zPoints` / `zOf`: a piecewise-linear "z ladder" (0 mid corridor, 2 pass edge, 3 fault line) rebuilt per tile from `bands[]`.
  - `diagnostics/PpSmallMultiple.qml:109-124` (`dist`) finds pass edges by `[g1,g2].sort().join("|") === "good|watch"`.
  - `typeset/PpFigureScale.qml:52-63` (`edges`) uses the same grade-pair string idiom to classify edges as `fault`, `pass` and `aim`.
  - `diagnostics/PpRunChart.qml:138-170 (dist)` filters those same edges again.
  - The C++ owners of the same facts: `Coaching/diagnostic_ledger.h` (`severityLevel`, `rowStrength`, the z excess) and `diagnostics/session_spread.h` (`SpreadCorridor` with `signalLo/Hi`, `faultLo/Hi`, `bands` from `spreadBands`). PpBoxPlot already consumes the C++ corridor object (`corridor.signalLo` …) while the strip figures consume `bands[]`. So the same pass and fault facts reach QML in two shapes.
- **What is duplicated:** Corridor-normalised position (z) is computed in C++ for the shot's headline value (`strengthExcess`, used by PpDotPlot at `x = strengthExcess`) and recomputed in QML for each run point (`zOf` over `bands`) in PpSmallMultiple. The code comment in `session_diagnostics_model.cpp:100` says the ladder "lives in diagnostic_ledger.h beside the z it reads, not in QML JS".
- **Shared component/module it should fold into:** Publish `z` per run point from the model (`run[i].z`, `median[i].z`), and publish the corridor edge list once (`spread.edges: [{f, value, kind}]`), so `zPoints`, `zOf` and the grade-pair string match all go. PpFigureScale `edges` then reads that list.
- **Effort / risk:** M. It changes the model's published shape, which `tst_session_diagnostics` and `session_spread` probes pin.
- **Confidence:** medium. I have not verified that `severityLevel`'s z equals `zOf` for every shape (one-sided corridors fold `side`).

### C22. Inline pickers and question blocks: charts/PpChChoices + PpChReadingPicker + PpChQuestions against typeset/PpInlinePicker + diagnostics/PpQuestions
- **Verdict:** LEGITIMATE VARIANT (mostly), with small overlap
- **Where:** `charts/PpChChoices.qml` (a text-line control row), `charts/PpChReadingPicker.qml` (an in-place family picker), `charts/PpChQuestions.qml` (67 lines, presenter-fed) against `typeset/PpInlinePicker.qml` (73) and `diagnostics/PpQuestions.qml` (154, which builds questions in QML).
- **What is duplicated:** The "in place, never a pop-up, current in ink, others as links" idiom. The three Charts pieces are different layouts of it. PpChQuestions and PpQuestions both show at most three questions asked IN PLACE through the brush, but PpQuestions builds them in QML (`forCondition`, see C17) while PpChQuestions takes them from a presenter.
- **Shared component/module it should fold into:** Keep. If C17's prose move is done, PpQuestions becomes a thin client like PpChQuestions and the two can share `typeset/PpAskedQuestions.qml`.
- **Effort / risk:** S to M, only as a follow-on to C17.
- **Confidence:** medium.

---

## Already shared / fine
- **Box statistics:** `typeset/box_stats.h` is the single source (`quantile7`, `boxStats`). `charts/session_curves.h:102-103`, `diagnostics/session_spread.h:184` (`spreadQuantile`), `charts/session_warp.cpp`, `session_traces.cpp`, `session_sequence.cpp` and `session_relations.cpp` all forward to it. `charts/box_map.h` is the single serialisation of a box for QML (shared by the relations and wrist presenters).
- **Box drawing:** PpBoxPlot is used by PpChBoxCell, PpBoxStrip and PpHistoryStrip. PpBoxReading is used only by PpBoxPlot.
- **Shot mark:** `typeset/PpMark.qml` is used by 8 files across charts and diagnostics.
- **Brush and hover state:** `typeset/PpBrush.qml` is shared by both registers and the print pipeline.
- **Scale:** `typeset/PpFigureScale.qml` is shared by PpRunChart, PpBoxStrip, PpHistoryStrip and PpSparkline. `PpZonePlot` is shared by PpRunChart, PpBoxStrip and PpHistoryStrip (but see C5 for what it misses).
- **Charts curve painter:** `charts/PpChPlot.qml` is shared by four Charts figures. `charts/PpChText.qml` is shared by eight.
- **Strips and washes:** `typeset/PpShotStrip.qml`, `PpPulseWash.qml`, `PpTabs.qml`, `PpInlinePicker.qml`, `PpNotes.qml` and `PpSectionHead.qml` are used by both stacks.
- **Roles layering:** both PpChRoles and PpDiagRoles extend PpTypeset, so fonts, inks, `u()`, `shows()`, `shotLabelStep` and `axisLabelShown` are single-sourced.
- **Presenters call the old stack's C++:** `ChartMetrics::sequenceTable`, `niceTicks`, `timeTicksMs` and `windowedMean` are reused by the Charts presenters. The statistics and reductions are not duplicated.
- **`charts_words.h::decimalsFor`:** `ChartMetrics::formatBare` already calls it. This is the pattern C15 and C16 want extended.
- **Trust inline:** `PpTrustInlineFor`, `PpTrustReading` and `TrustFocus` are shared by 8 or more call sites. It is the *link* half that is duplicated (C13).

## Not examined
- `review/PpDataViewer.qml`, `PpPropertiesPanel.qml`, `PpCoverageStrip.qml` and `PpTransitTimeline.qml` bodies. They are session-screen widgets and were only checked for chart-plot overlap, not read in full. `PpTransitTimeline` (536) and `PpChWindowBrush` / `PpSegmentBrush` share a "µs to pixel and drag handles" idea that I did not measure.
- `diagnostics/PpCauseGraph.qml`, `PpPathChains.qml`, `PpEntryFlow.qml`, `PpIndexList.qml`, `PpFeatureHead.qml`, `PpFigureRow.qml` (340), `PpDiagPrint.qml` / `typeset/PpPager.qml` print pipeline, `PpShotRuler.qml`, `PpBarcode.qml` (only compared against PpChSeqSession's order barcode, which is a different layout, not a duplicate).
- The bodies of `charts/PpChSessionFigure.qml` (360) and `PpChTraceFigure.qml` (575) beyond their shared helpers. They probably contain internal repetition across rows and across facets that a figure-level read would find.
- `charts/PpChWristProfile.qml`, `PpChWristSession.qml`, `PpChWristShotSection.qml` and `PpChWristLedger.qml` against `diagnostics/wrist_diagnostics_model.cpp`.
- C++ presenter bodies (`charts_traces_presenter.cpp` 3,104 lines, `charts_relations_presenter.cpp` 2,513, `charts_wrist_presenter.cpp` 1,629, `session_diagnostics_model.cpp` 4,551). I looked only at formatters, ticks and quantiles. Other helper duplication between the Charts presenters and the diagnostics model (for example `lowerLabel`, `aOrAn`, `signedMs` variants) is likely but unmeasured.
- Tests and probes, except as evidence of reachability.
- Whether `NormativeBar`'s sibling `PpRangeBar` (named in `docs/design/norm_shapes.md`) lives outside this cluster. It is not under these directories.
