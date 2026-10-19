# Review (replay/analyse)

*Directory:* `src/Gui/review/`  
*17 components · 7,096 lines of QML*

The older replay/analyse stack: metric charts, data viewer, coverage and sequence strips, replay transport, and the Metric Catalogue (library/detail/route).

**C++ in this area:** `ChartMetrics`, `MetricCatalog`, `SessionListModel`, `SessionReviewController`, `SwingCoverageModel`, `SwingDataSource`, `SwingSeriesModel`, `TimelineLabels`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [MetricDetail](#metricdetail) | 568 | `Item` | 1 | 5 | 2 local comp. |
| [MetricLibrary](#metriclibrary) | 335 | `Item` | 0 | 5 | ⚠ unreferenced |
| [MetricRoute](#metricroute) | 52 | `QtObject` | 1 | 0 | singleton |
| [MetricRow](#metricrow) | 216 | `Item` | 1 | 1 |  |
| [NormativeBar](#normativebar) | 201 | `Item` | 1 | 0 |  |
| [PpChartPlot](#ppchartplot) | 1008 | `Item` | 1 | 0 |  |
| [PpChartSummary](#ppchartsummary) | 704 | `ColumnLayout` | 1 | 3 |  |
| [PpCoverageStrip](#ppcoveragestrip) | 226 | `Column` | 1 | 1 |  |
| [PpDataViewer](#ppdataviewer) | 639 | `Item` | 2 | 13 | 1 local comp. |
| [PpMetricChart](#ppmetricchart) | 1550 | `Item` | 2 | 11 | 2 local comp. |
| [PpPropertiesPanel](#pppropertiespanel) | 151 | `Item` | 1 | 3 |  |
| [PpReplayCharts](#ppreplaycharts) | 74 | `Item` | 0 | 2 | ⚠ unreferenced |
| [PpReplayTransport](#ppreplaytransport) | 88 | `Item` | 2 | 3 | 1 local comp. |
| [PpSegmentBrush](#ppsegmentbrush) | 365 | `Item` | 1 | 0 |  |
| [PpSequenceStrip](#ppsequencestrip) | 298 | `ColumnLayout` | 1 | 3 | 2 local comp. |
| [PpSpeedSelector](#ppspeedselector) | 84 | `Rectangle` | 1 | 0 |  |
| [PpTransitTimeline](#pptransittimeline) | 537 | `Item` | 2 | 3 |  |

### MetricDetail
`src/Gui/review/MetricDetail.qml` · 568 lines · root `Item`

> Detail view for a single metric. Reads the full descriptor from MetricCatalog and
> renders it in plain language: what it means, how to read it, its normative corridors
> (one NormativeBar per phase), what it needs to compute, and where it is used. Purely
> read-only; it emits back() for the parent to return to the directory.

- **Properties:** `var mc`, `var labels`, `string metricKey`, `readonly var d`
- **Signals:** `back()`, `openNorm(string measureId)`
- **Local components:** `Eyebrow`, `Body`
- **Uses:** `NormativeBar`, `PpDisplayText`, `PpDivider`, `PpPressable`, `PpTypePill`
- **Used by (1):** `MetricLibrary`

### MetricLibrary
`src/Gui/review/MetricLibrary.qml` · 335 lines · root `Item` · ⚠ **no app file instantiates this**

> The Metric Catalogue directory, shaped as a Settings panel. A filter chip row over
> the metric types, then the metrics grouped in manifest order, each row a MetricRow.
> Tapping a row swaps in MetricDetail (internal master→detail). Read-only: all data
> comes from the MetricCatalog façade; there is no shot loaded here, so availability is
> never computed (empty shotCtx everywhere) — a metric's needs are shown via its source
> glyphs and its detail "How it's measured" section, not a live verdict.

- **Signals:** `openNorm(string measureId)`
- **Functions:** `scrollToItem(itemId)`, `showMetric(key)`
- **Uses:** `MetricDetail`, `MetricRow`, `PpDisplayText`, `PpDivider`, `PpTextField`

### MetricRoute
`src/Gui/review/MetricRoute.qml` · 52 lines · root `QtObject` · **singleton**

> MetricRoute — the one-line router that links a metric ANYWHERE in the app to its
> catalogue detail page. A metric tile is typically instantiated several layers
> inside a stage delegate (or a top-level cast Window), so it has no path to the
> settings screen that hosts MetricLibrary/MetricDetail. Rather than thread a
> signal up through every host, tiles call MetricRoute.open(key) and Main.qml — the
> one place that owns navigation — performs the jump.
> CURRENTLY NO PRODUCER: the per-shot dashboard's tiles were its only callers and
> went with that surface. The receiving side (Main.qml → ScreenSettings) stays
> wired for the next surface that wants a metric deep-link.
> Deliberately state-light: `requestedKey` is a record of the last request for a
> late-attaching listener, not a queue. The signal is the contract.

- **Properties:** `string requestedKey`
- **Signals:** `openRequested(string key)`
- **Functions:** `open(key)`
- **Used by (1):** `Main`
- **Exercised by tests/tools:** `metric_colours`

### MetricRow
`src/Gui/review/MetricRow.qml` · 216 lines · root `Item`

> One directory row in the Metric Catalogue. Chromeless until hovered (the project's
> subtle-chrome preference); shows the metric label, a type pill + "key · unit"
> secondary line, and a source glyph per producer (IMU / camera / club / ball). It is
> purely presentational: it takes a row map (as emitted by MetricCatalog.query) and
> emits clicked() — the parent owns navigation.

- **Properties:** `var metric`
- **Signals:** `clicked()`
- **Uses:** `PpPressable`
- **Used by (1):** `MetricLibrary`

### NormativeBar
`src/Gui/review/NormativeBar.qml` · 201 lines · root `Item` · 7 hard-coded colour(s)

> A horizontal corridor bar for ONE corridor map, as MetricCatalog marshals it out of the norm set
> { greenLo, greenHi, amberLo, amberHi, lowOpen, highOpen, deltaFromAddress, … }: a green
> "Ideal" band nested inside a wider amber band on a muted track, with the amber min/max
> tick labels below. Self-contained and prop-driven so charting can reuse it later — set
> `value` to render an optional "you" marker (hidden while it is not finite).
> ONE-SIDED CORRIDORS. Where the measure's shape leaves a tail ungraded, the norm collapses
> that side's edges onto `mu` — the aspiration, never a sentinel. Drawn naively that reads
> as a WALL: a floor's green band would stop dead at mu with a hard edge, and every reading
> above it (all of which grade Ideal) would clamp to the last pixel of the track, so 1.55
> and 5.0 would look the same and both would look like they had left the corridor. So the
> green band runs off the open end as a fade, and the domain leaves room for it to run into
> — see barDomain() in dashboard_reductions.h, which owns that arithmetic because a rule
> living inside a QML binding is a rule nothing can test.
> The amber band needs no such treatment: it genuinely ENDS at mu on a floor, because above
> mu the grade is Ideal and the colour there is green.

- **Properties:** `var corridor`, `real value`
- **Used by (1):** `MetricDetail`

### PpChartPlot
`src/Gui/review/PpChartPlot.qml` · 1008 lines · root `Item`

> PpChartPlot — one plot region: a Y axis (grid + ticks + unit), an X time axis (ms from
> impact), swing-phase bands + landmark emphasis (top of swing, impact), N metric traces,
> verdict-marked P-position dots, a replay playhead, and a shared hover crosshair. Given a series subset (each
> decorated with its `color`), a value range [valueLo,valueHi], a time domain
> [domStartUs,domEndUs], and geometry — so it serves BOTH overlay (one plot, N series) and
> split (N plots, one series each). Pure scale/path binding only; axis maths lives in
> ChartMetrics, phase tags in TimelineLabels. This is the unit future charts reuse.

- **Properties:** `var series`, `var phases`, `real valueLo`, `real valueHi`, `real domStartUs`, `real domEndUs`, `real impactUs`, `string unitLabel`, `var sequence`, `real playheadUs`, `bool showPlayhead`, `bool showDots`, `bool showCrosshair`, `real cursorUs` … (+14 more)
- **Signals:** `hoverMoved(real tUs)`, `hoverExited()`, `seekRequested(real tUs)`, `scrubBegan()`, `scrubEnded()`
- **Functions:** `xForT(t)`, `yForV(v)`
- **Used by (1):** `PpMetricChart`
- **Exercised by tests/tools:** `plumb_bob_chart`, `tst_chart_presets`

### PpChartSummary
`src/Gui/review/PpChartSummary.qml` · 704 lines · root `ColumnLayout`

> PpChartSummary — the per-window summary cards row: one card per series, each fed by
> ChartMetrics.summaryMasked(series.t_us, series.value, series.valid, winStart, winEnd), where
> the window is the active one CLAMPED to the metric's phase domain. A card shows @impact /
> peak / Δ-segment / peak-rate, with the @impact value tinted by the band of the swing state at
> impact. @impact is the value at the impact landmark (read from the whole series, a fixed
> reference more useful to compare against PEAK than the window edge); peak/Δ/rate stay
> window-scoped, so selecting TOP→IMP shows the downswing's numbers. Recomputes live as the
> segment chips / brush move the window. Pure binding; all stats come from ChartMetrics.
> Nothing here shows a number it cannot stand behind (design metric_presentation_honesty.md §5.1):
> a bridged sample is excluded from every reduction, a window the domain clamp emptied prints "—"
> in the three window-scoped tiles, @impact prints "—" where it was not measured at impact, and a
> window with any unmeasured part in it wears a PARTIAL chip.
> Since Phase 2 (§5.2) PEAK is a 40 ms windowed-mean extremum and PK RATE a ≥50 ms least-squares
> slope, both from src/Metrics/series_reduce.h — the same reducers the diagnostics engine grades
> with, so a card and a corridor cannot disagree about the same window. PK RATE therefore also has
> an ABSENT state (`rateOk` false: no window long enough, or too few valid samples in it), and it
> …

- **Properties:** `var series`, `real startUs`, `real endUs`, `real impactUs`, `var phases`, `string segmentName`, `bool showHeader`
- **Uses:** `PpBadge`, `PpInset`, `PpMicro`
- **Used by (1):** `PpMetricChart`
- **Exercised by tests/tools:** `shaft_lie_card`

### PpCoverageStrip
`src/Gui/review/PpCoverageStrip.qml` · 226 lines · root `Column`

> Per-(source,part) coverage lanes over the swing time axis. Each lane's `bins`
> (0 present / 1 low-conf / 2 gap) renders as a fixed row of cells — a full cell in the
> source's colour, a short amber one, or none — so the three read by shape as well as
> colour; a selection band marks the detail window; a phase ladder runs underneath.
> Read-only.

- **Properties:** `var model`, `var phases`, `real spanUs`, `real windowStartUs`, `real windowEndUs`, `var colorFn`, `bool showTitle`, `readonly int labelW`, `real positionUs`, `bool interactive`, `readonly color lowConfColor`, `readonly real lowConfHeight`
- **Signals:** `seek(real us)`, `scrubBegin()`, `scrubEnd()`
- **Functions:** `kindOf(lbl)`, `nameOf(lbl)`, `kindGlyph(k)`
- **Uses:** `PpMicro`
- **Used by (1):** `PpDataViewer`

### PpDataViewer
`src/Gui/review/PpDataViewer.qml` · 639 lines · root `Item`

> Read-only swing.json inspector: region presets, a coverage strip, and a gap-aware
> detail table. Hosted as PpModeStage's tableDelegate, whose TABLE card frames it.

- **Properties:** `int sessionType`, `string swingDir`, `readonly bool hasSwing`, `readonly int ctrlGutter`, `bool scopeCollapsed`, `bool coverageCollapsed`, `bool tableCollapsed`
- **Functions:** `persistRegion(name)`, `srcColor(key)`, `stateMark(st)`
- **Local components:** `SectionHeader`
- **Uses:** `PpCardNote`, `PpChip`, `PpCoverageStrip`, `PpMicro`, `PpPopoverCard`, `PpPressable`, `PpPropertiesPanel`, `PpSegmentedControl`, `PpToast`, `PpTrustInlineFor`, `PpTypeset`, `SessionMode`, `TrustFocus`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`

### PpMetricChart
`src/Gui/review/PpMetricChart.qml` · 1550 lines · root `Item`

> PpMetricChart — reusable, prop-driven metric chart. Toggles between a single overlaid
> plot (all series, shared Y) and small multiples (one PpChartPlot per series, each Y
> auto-scaled). Owns the chart-LOCAL view window [viewStartUs,viewEndUs] (it never touches
> shotReplay / SwingDataSource, so different chart instances scope independently). The
> playhead and the view window are conceptually distinct — the window never moves the
> playhead; the playhead is simply clipped when outside the window.
> Phase 1: split/overlay, axes, phase bands, traces, P-dots, playhead, hover crosshair +
> tooltip, legend chips. Phase 2 adds the segment brush + chips → viewStartUs/viewEndUs.
> Phase 3 adds the summary cards. Phase 4 adds the METRICS preset combo — which catalogue
> group of series to plot, since a full capture now produces far more curves than one panel
> can show. Heavy maths lives in ChartMetrics; phase tags/value lookup in TimelineLabels;
> colours/sizes/fonts in Theme.
> TWO independent narrowings, deliberately kept apart: the preset chooses WHICH SERIES
> (enabledKeys), the segment chips/brush choose WHICH TIME SPAN (viewStartUs/viewEndUs).
> ⚠ WHAT THE LINE IS, since Phase 6 of docs/design/metric_presentation_honesty.md. Every surface
> here draws and quotes the 40 ms centred WINDOWED MEAN — the array ChartMetrics.windowedMean
> …

- **Properties:** `var seriesList`, `var phases`, `var kinematicSequence`, `var sequenceChecks`, `real startUs`, `real endUs`, `real impactUs`, `real playheadUs`, `bool showPlayhead`, `real viewStartUs`, `real viewEndUs`, `bool splitMode`, `var enabledKeys`, `bool showDots` … (+10 more)
- **Signals:** `seekRequested(real tUs)`, `scrubBegan()`, `scrubEnded()`
- **Local components:** `SectionHeader`, `DotKey`
- **Uses:** `PpCardNote`, `PpChartPlot`, `PpChartSummary`, `PpComboBox`, `PpMicro`, `PpPopoverCard`, `PpPressable`, `PpSegmentBrush`, `PpSegmentedControl`, `PpSequenceStrip`, `SessionMode`
- **Used by (2):** `PpCameraTiles`, `PpReplayCharts`
- **Exercised by tests/tools:** `instant_cards`, `ks_overlay_chart`, `ks_rising_chart`, `plumb_bob_chart`

### PpPropertiesPanel
`src/Gui/review/PpPropertiesPanel.qml` · 151 lines · root `Item`

> Read-only grouped key/value list over `metadata` — a list of groups
> [{ group: <title>, rows: [{ k, v }, ...] }, ...]. Plain provenance; no editing.

- **Properties:** `var metadata`, `string swingDir`
- **Uses:** `PpCardNote`, `PpMicro`, `PpPressable`
- **Used by (1):** `PpDataViewer`

### PpReplayCharts
`src/Gui/review/PpReplayCharts.qml` · 74 lines · root `Item` · ⚠ **no app file instantiates this** · no direct `Theme.*` refs

> Charts stage-panel: the focused replay's metric traces (PpMetricChart),
> scrubbing with the replay playhead, with split/overlay + chart-local segment
> selection + per-window summary. Bound to shotReplay. Hosted by PpModeStage as the
> "charts" panel, whose card is its frame and title; until a swing is reviewed the card holds
> one quiet line.

- **Properties:** `int sessionType`
- **Uses:** `PpCardNote`, `PpMetricChart`

### PpReplayTransport
`src/Gui/review/PpReplayTransport.qml` · 88 lines · root `Item`

> Always-on Replay transport chrome: play/pause, frame-step and speed. Slim control
> that rides in the shot carousel's top strip in Replay/Analyse, so video-only review
> still works even when the timeline panel (scrub + phase pills) is hidden. There is no
> Close button — exit via the toolbar mode switch / Capture / Esc.
> Content-sized (implicitWidth follows the button row) so the host can right-align it in
> the carousel strip; it does not stretch to fill.

- **Local components:** `TBtn`
- **Uses:** `PpMicro`, `PpPressable`, `PpSpeedSelector`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`

### PpSegmentBrush
`src/Gui/review/PpSegmentBrush.qml` · 365 lines · root `Item`

> PpSegmentBrush — whole-swing overview strip with a draggable selection window. Shows a
> faint per-series sparkline (each normalised to its own range) and the swing-phase ticks
> across the full extent, then a movable/resizable window the user drags to scope the
> chart. Emits windowChanged(startUs,endUs) as the body is dragged, an edge handle is
> pulled, or a fresh window is swept out — the parent writes that into its chart-local
> viewStartUs/viewEndUs (never shotReplay / SwingDataSource). Pure scale/path binding;
> per-series ranges come from ChartMetrics.summaryMasked, phase tags from TimelineLabels.
> It obeys the same validity contract as the chart it scopes (design
> metric_presentation_honesty.md §5.1): the y-range excludes bridged samples, and bridged /
> out-of-domain runs are drawn dashed and dimmer rather than as measurements. A sparkline is
> small, but it is the surface a reader uses to DECIDE WHERE TO LOOK, so a single bridged
> outlier flattening the whole trace into a spike sends them to the wrong place.
> Since Phase 6 the sparkline draws the same 40 ms WINDOWED MEAN the chart's traces do (`mean`,
> decorated by PpMetricChart._plottable from ChartMetrics.windowedMean) — one reduction across both
> surfaces, so the shape a reader picks a window from is the shape they then read. No raw dots here:
> at this height the reduced line is the legible one, and the raw samples are on the chart it scopes.

- **Properties:** `var series`, `var phases`, `real axisStartUs`, `real axisEndUs`, `real winStartUs`, `real winEndUs`, `real impactUs`, `real minWidthUs`
- **Signals:** `viewWindowChanged(real startUs, real endUs)`
- **Functions:** `xs(t)`, `toT(px)`
- **Used by (1):** `PpMetricChart`

### PpSequenceStrip
`src/Gui/review/PpSequenceStrip.qml` · 298 lines · root `ColumnLayout`

> PpSequenceStrip — the kinematic sequence TILE under the chart (2026-10-02), deliberately plain:
> four rows in the order the segments SHOULD peak, each with the place it ACTUALLY peaked, when,
> how fast, and that speed as a % of the tour-pro peak, each with its ±σ. It mirrors the plot:
> every peak marked on a curve has a row with numbers; an unplaced one is dimmed with a "?". The
> ACTUAL column carries the judgement as a mark beside the number — a tick in turn, a target out
> of turn, a dashed ring where the peak could not be pinned — so a wrong order is a shape and a
> colour, and nothing else needs reading. A first cut carried σ, timing windows, two order
> sentences and a cohort paragraph — Mark: "way too wordy and almost unreadable".
> Shown only under the "Kinematic sequence" preset. Every string is ChartMetrics.sequenceTable's
> (chart_metrics_test asserts them); this file binds and picks colours and marks.
> A SECTION OF THE CHART PANEL, NOT A CARD: it sits inside the panel's card, so it opens on a
> hairline and a Micro heading like the panel's other sections rather than drawing a box.

- **Properties:** `var kinematicSequence`, `real impactUs`, `bool collapsed`, `var checks`
- **Signals:** `toggled()`
- **Local components:** `HeadText`, `CellText`
- **Uses:** `PpBadge`, `PpMicro`, `PpPressable`
- **Used by (1):** `PpMetricChart`

### PpSpeedSelector
`src/Gui/review/PpSpeedSelector.qml` · 84 lines · root `Rectangle` · 1 hard-coded colour(s)

> Segmented playback-speed selector — all speeds visible, the active one
> highlighted with an accent-tinted capsule; click any segment to select it
> directly. Used by the Replay transport (PpReplayTransport) to drive the
> session-stage replay speed.

- **Properties:** `readonly var speeds`, `real current`
- **Signals:** `selected(real speed)`
- **Used by (1):** `PpReplayTransport`

### PpTransitTimeline
`src/Gui/review/PpTransitTimeline.qml` · 537 lines · root `Item`

> Transit — the swing's phases as a line of stations, and the master scrub/seek
> surface for Review mode. Stations sit at their TRUE proportional position on the
> line; upright full-word labels are nudged along the line only as far as needed to
> clear their neighbours (a 1-D solve done in C++ — TimelineLabels), with a hairline
> elbow back to each dot. A bead rides the line at the playhead carrying a readout
> chip (phase · time · live metric). Drag the line to scrub; click a station to seek.
> Both orientations render from one station model:
> • horizontal — line near the top, labels in a row beneath (host: top rail).
> • vertical   — line near the left, labels in a column to the right (host: left of stage).
> All iterative maths lives on `solver`; QML keeps to declarative bindings + handlers.
> A FLAT RAIL, NOT A CARD: it runs along the stage's edge and frames nothing. Its chips are the
> card family's chips (PpChip), and the measured P-positions use the provenance marks — a solid
> dot where the position was fitted, a hollow ring where it was sampled off the track — with
> the key in words at the rail's far end.

- **Properties:** `string orientation`, `bool snapToPhases`, `readonly real contentHeight`
- **Uses:** `PpCardNote`, `PpChip`, `PpMicro`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`
