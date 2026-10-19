# Charts panel

*Directory:* `src/Gui/charts/`  
*39 components · 6,923 lines of QML*

The Charts stage panel (PpCh*): This shot / Session / Relations / Kinds views and their blocks, built on the typeset vocabulary.

**C++ in this area:** `ChartsRelationsPresenter`, `ChartsTracesPresenter`, `ChartsWristPresenter`, `SessionSeriesModel`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [PpChBoxCell](#ppchboxcell) | 124 | `Item` | 6 | 1 |  |
| [PpChChoices](#ppchchoices) | 100 | `Flow` | 4 | 0 |  |
| [PpChFoldHead](#ppchfoldhead) | 59 | `Item` | 3 | 0 |  |
| [PpChKeyFigures](#ppchkeyfigures) | 88 | `Item` | 1 | 0 |  |
| [PpChKindCurve](#ppchkindcurve) | 169 | `Item` | 1 | 1 |  |
| [PpChKindTable](#ppchkindtable) | 163 | `Column` | 1 | 1 |  |
| [PpChMatrix](#ppchmatrix) | 86 | `Column` | 1 | 0 |  |
| [PpChMovesWith](#ppchmoveswith) | 148 | `Column` | 2 | 1 |  |
| [PpChPlot](#ppchplot) | 169 | `Canvas` | 4 | 0 |  |
| [PpChProfile](#ppchprofile) | 218 | `Column` | 2 | 3 |  |
| [PpChQuestions](#ppchquestions) | 68 | `Column` | 3 | 0 |  |
| [PpChReadingPicker](#ppchreadingpicker) | 68 | `Column` | 1 | 0 |  |
| [PpChReadingTrace](#ppchreadingtrace) | 139 | `Item` | 1 | 2 |  |
| [PpChReplaySession](#ppchreplaysession) | 93 | `Column` | 1 | 3 |  |
| [PpChRoles](#ppchroles) | 81 | `PpTypeset` | 1 | 1 |  |
| [PpChRunBox](#ppchrunbox) | 132 | `Item` | 4 | 2 |  |
| [PpChScatter](#ppchscatter) | 169 | `Item` | 2 | 2 |  |
| [PpChSeqChain](#ppchseqchain) | 82 | `Column` | 1 | 1 |  |
| [PpChSeqSession](#ppchseqsession) | 194 | `Column` | 1 | 2 |  |
| [PpChSeqTable](#ppchseqtable) | 200 | `Column` | 1 | 0 |  |
| [PpChSessionFigure](#ppchsessionfigure) | 361 | `Item` | 1 | 1 |  |
| [PpChSummary](#ppchsummary) | 127 | `Column` | 1 | 0 |  |
| [PpChText](#ppchtext) | 41 | `Text` | 10 | 0 |  |
| [PpChTraceFigure](#ppchtracefigure) | 576 | `Item` | 1 | 1 |  |
| [PpChWindowBrush](#ppchwindowbrush) | 159 | `Item` | 1 | 0 |  |
| [PpChWristLedger](#ppchwristledger) | 118 | `Column` | 1 | 0 |  |
| [PpChWristProfile](#ppchwristprofile) | 300 | `Item` | 2 | 1 |  |
| [PpChWristSession](#ppchwristsession) | 170 | `Item` | 1 | 5 |  |
| [PpChWristShotSection](#ppchwristshotsection) | 295 | `Item` | 1 | 9 |  |
| [PpChartsAnalyse](#ppchartsanalyse) | 28 | `PpChartsRegister` | 1 | 1 |  |
| [PpChartsBody](#ppchartsbody) | 347 | `Item` | 1 | 5 |  |
| [PpChartsKindsView](#ppchartskindsview) | 114 | `Column` | 1 | 5 |  |
| [PpChartsPanel](#ppchartspanel) | 380 | `Item` | 2 | 2 |  |
| [PpChartsReadingsView](#ppchartsreadingsview) | 204 | `Column` | 1 | 2 |  |
| [PpChartsRegister](#ppchartsregister) | 216 | `Item` | 2 | 9 | 1 local comp. |
| [PpChartsRelationsView](#ppchartsrelationsview) | 192 | `Column` | 1 | 10 |  |
| [PpChartsReplay](#ppchartsreplay) | 28 | `PpChartsRegister` | 1 | 1 |  |
| [PpChartsSessionView](#ppchartssessionview) | 240 | `Column` | 1 | 10 |  |
| [PpChartsShotView](#ppchartsshotview) | 477 | `Column` | 1 | 18 |  |

### PpChBoxCell
`src/Gui/charts/PpChBoxCell.qml` · 124 lines · root `Item` · no direct `Theme.*` refs

> PPCHBOXCELL — one reading's session as a box plot, and this shot on it (spec 15.8's box, laid on its side so
> a column of readings reads as one profile, or stood up beside a run or at a P-position on a trace). The
> Charts panel's adapter onto PpBoxPlot, THE box plot (typeset/): it maps the presenter's numbers to pixels,
> chooses the Charts inks and turns the presenter's per-shot values into the shot marks — a shot past the
> whiskers (warn when a diagnostics condition reading it fired on that shot), the picked shot ringed at its
> value, the hovered shot (the Brush) a lighter ring. Every number is the presenter's `box`
> ({ n, min, max, q1, med, q3, lo, hi, outliers: [ordinal], texts }) and `values` (one per shot, index =
> ordinal − 1, null where unmeasured).
> Colour (§15.4 item 1, D5): a box stands for a METRIC, so it is drawn in the metric's hue (`color`), muted
> to ink3 at rest when `muted` (a profile of 68 coloured boxes is busy) and coloured on hover or when apart.
> `corridor` (#10, the Readings rows only): the presenter's { known, lowOpen, highOpen, signalLo/Hi, faultLo/Hi },
> drawn by PpBoxPlot under the box, kept quiet (Mark, 18 Oct: "so long as the corridor and fault line are
> subtle"). The scale widens to take in the corridor's graded edges, so a session that sits wholly outside it
> still shows where it is.
> HOVER, EVERYWHERE (Mark, 18 Oct): an interactive cell lays its values on the plot while the pointer is over it
> (PpBoxPlot: across, in two lanes; stood up, the reading beside it). `texts` overrides the box's own texts —
> …

- **Properties:** `QtObject roles`, `var brush`, `var box`, `var values`, `var fired`, `real lo`, `real hi`, `color color`, `bool muted`, `bool vertical`, `bool interactive`, `real markScale`, `var corridor`, `var texts` … (+19 more)
- **Signals:** `shotActivated(int ordinal)`
- **Functions:** `pos(v)`, `valueOf(ord)`
- **Uses:** `PpBoxPlot`
- **Used by (6):** `PpChKindTable`, `PpChProfile`, `PpChReadingTrace`, `PpChRunBox`, `PpChScatter`, `PpChWristProfile`

### PpChChoices
`src/Gui/charts/PpChChoices.qml` · 100 lines · root `Flow` · no direct `Theme.*` refs

> PPCHCHOICES — a view's controls as quiet lines of text ("Metrics  Kinematic sequence ▾   Window  P4→P8 ·
> P1→P10 · Full"). The current choice is ink, the others links; nothing is boxed and nothing pops up: a long
> list opens IN PLACE under the line (the register owns the PpInlinePicker). Wraps, never truncates. A set
> may be a toggle row (`multi`: several options on at once, e.g. a preset's metrics) or an opener (`opens`:
> one option that toggles a picker, drawn with ▾ / ▴). Screen only.
> sets: [{ key, label, current, multi, opens, open, options: [{ key, label, on, enabled, swatch }] }]
> picked(setKey, optionKey) — the register routes it to the body.
> objectNames: chChoice:<set key> on each group, chOption (+ `key`, `setKey`) on each option.

- **Properties:** `QtObject roles`, `bool interactive`, `var sets`, `readonly var keyKinds`, `bool atom`
- **Signals:** `picked(string set, string option)`
- **Used by (4):** `PpChWristShotSection`, `PpChartsRelationsView`, `PpChartsSessionView`, `PpChartsShotView`

### PpChFoldHead
`src/Gui/charts/PpChFoldHead.qml` · 59 lines · root `Item` · no direct `Theme.*` refs

> PPCHFOLDHEAD — a crosshead that folds its section (CONTRACT Addendum: "Collapsible sections keep their saved
> state"; today's CONTROLS / SUMMARY / SEQUENCE folds, persisted per screen × mode in AppSettings
> sectionCollapse by the panel). Quiet: the section's title in the section role, an ink3 caret, and while
> folded a one-line `state` so nothing it held is silently gone (simplify = quieten). Not foldable in print.
> objectName: chFold:<key>.

- **Properties:** `QtObject roles`, `string key`, `string title`, `string sub`, `string state_`, `bool collapsed`, `bool foldable`, `bool interactive`, `readonly var keyKinds`, `bool atom`, `bool keepNext`
- **Signals:** `toggled()`
- **Used by (3):** `PpChWristShotSection`, `PpChartsSessionView`, `PpChartsShotView`

### PpChKeyFigures
`src/Gui/charts/PpChKeyFigures.qml` · 88 lines · root `Item` · no direct `Theme.*` refs

> PPCHKEYFIGURES — the key figures under a heavy rule (B's KeyFigs; design §2.1 "KeyFigures with B's place
> words"): a label with the metric's swatch, the figure with its unit, and a quiet foot that places it in
> the session ("median 72 · 3rd highest"). A warn figure is set in warn AND carries its word ("late",
> "out of its corridor") — never colour alone. Wraps n → n/2 → n/4, each figure at least 10 em
> (flows.keyFigure). Items are ChartsRelationsPresenter.thisShot.keyFigures.items, shown as given.
> objectName: chKeyFigures; each figure chKeyFigure (+ readingId).

- **Properties:** `QtObject roles`, `var items`, `bool atom`, `readonly var keyKinds`, `readonly int n`, `readonly int fit`, `readonly int cols`, `readonly real cw`, `readonly real ch`
- **Signals:** `trustClicked(string subject)`
- **Used by (1):** `PpChartsShotView`

### PpChKindCurve
`src/Gui/charts/PpChKindCurve.qml` · 169 lines · root `Item` · no direct `Theme.*` refs

> PPCHKINDCURVE — each kind's curve (design §2.4, §4; Mark's group fit): ONE curve per kind, fitted to the
> pooled valid samples of all its shots with their P-positions lined up (D9), in the metric's hue — the first
> kind solid (ch.kindW), the second dashed (ch.kindDash, α kindDashAlpha) — with ± 1 sd hairlines (no wash;
> ch.spreadEdgeW / spreadEdgeAlpha). It breaks where fewer than 3 shots contribute, a kind under 3 is refused
> with the presenter's sentence, and each curve is labelled with its kind and n. Under it, quietly, each
> shot's own measured stretches (ch.coverRowPitch, coverBar), this shot in ink: what the kind filled. No shot's
> number is read from it. Data: ChartsRelationsPresenter kinds.curves[i].
> objectName: chKindFit:<key>.

- **Properties:** `QtObject roles`, `var brush`, `QtObject ticker`, `var curve`, `real plotH`, `bool showCover`, `bool atom`, `readonly var keyKinds`, `readonly var fits`, `readonly bool hasWindow`, `readonly real t0`, `readonly real t1`, `readonly var yr`, `readonly var yt` … (+7 more)
- **Functions:** `tx(t)`, `vy(v)`
- **Uses:** `PpChPlot`
- **Used by (1):** `PpChartsKindsView`

### PpChKindTable
`src/Gui/charts/PpChKindTable.qml` · 163 lines · root `Column` · no direct `Theme.*` refs

> PPCHKINDTABLE — Kinds' members and what else separates them (design §2.4, D7): the kinds' members in shot
> order (a number brushes that shot everywhere and a click puts it in the carousel; the picked one in its
> column wash), with the most typical shot named; a neutral "not measured" set when the presenter has one;
> then the separation table — each reading's two medians, d (ink when it clears the d chance gives at these
> sizes, ink3 inside it) and the two kinds' boxes on one scale, the first kind above, this shot ringed in its
> own kind. Same-quantity rows carry ‡. Everything is ChartsRelationsPresenter.kinds'.
> column        tier  below it folds to
> name, d       1     (line 1)
> medians, unit 3     line 2, "faster 1630 · slower 1204 °/s"
> boxes         1     (line 1, narrower)
> objectNames: chKindMembers, chKindTable; each separation row chKindRow (+ readingId).

- **Properties:** `QtObject roles`, `var brush`, `var kinds`, `bool interactive`, `bool atom`, `readonly var keyKinds`, `readonly var groups`, `readonly var names`, `readonly var sep`, `readonly real em`, `readonly bool cols`, `readonly real gap`, `readonly real medW`, `readonly real unitW` … (+3 more)
- **Signals:** `shotActivated(int ordinal)`
- **Uses:** `PpChBoxCell`
- **Used by (1):** `PpChartsKindsView`

### PpChMatrix
`src/Gui/charts/PpChMatrix.qml` · 86 lines · root `Column` · no direct `Theme.*` refs

> PPCHMATRIX — the Up reading's family against itself (design §2.3 "the family matrix"): a lower triangle of
> r set as FIGURES, an annual-report table and not a heat map (the only wash in a chart is the corridor's): a
> figure is ink when it clears chance and ink3 inside it, the pair on the scatter in the pick wash. Rows are
> named and numbered, columns carry the numbers. As many readings as their columns fit (ch.matrixCellEm);
> the rest stay in the "Moves with" ledger. A cell puts that pair on the scatter.
> objectName: chMatrix; each cell chMatrixCell (+ xId, yId).

- **Properties:** `QtObject roles`, `var matrix`, `bool interactive`, `bool atom`, `readonly var keyKinds`, `readonly real em`, `readonly real cellW`, `readonly real nameW`, `readonly var all`, `readonly int n`, `readonly real rowH`
- **Signals:** `pairActivated(string xId, string yId)`
- **Used by (1):** `PpChartsRelationsView`

### PpChMovesWith
`src/Gui/charts/PpChMovesWith.qml` · 148 lines · root `Column` · no direct `Theme.*` refs

> PPCHMOVESWITH — "Moves with …" (design §2.3): every other reading ranked by |r| with the Up reading across
> the session (ChartsRelationsPresenter relations.ledger): r (ink when it clears chance, ink3 inside it), its
> 95 % interval DRAWN on a −1…1 rule, n, the slope in units, and the pair as a sparkline. A same-quantity pair
> carries ‡ and no rank (labelled, not ranked as a finding). A row puts that reading across the scatter.
> column      tier  below it folds to
> rank, name  1     (line 1)
> r           1     (line 1)
> interval    2     line 2, its text "(.37 to .91)"
> n, slope    3     line 2, "n 15 · +1.3 mph per °"
> the pair    4     the scatter above draws the selected pair
> objectNames: chMovesWith; each row chMovesRow (+ readingId).

- **Properties:** `QtObject roles`, `var rows`, `string acrossId`, `bool interactive`, `bool atom`, `readonly var keyKinds`, `readonly real em`, `readonly bool showIv`, `readonly bool showNs`, `readonly bool showSp`, `readonly real gap`, `readonly real rankW`, `readonly real rW`, `readonly real ivW` … (+4 more)
- **Signals:** `rowActivated(string readingId)`
- **Uses:** `PpMark`
- **Used by (2):** `PpChWristShotSection`, `PpChartsRelationsView`

### PpChPlot
`src/Gui/charts/PpChPlot.qml` · 169 lines · root `Canvas` · no direct `Theme.*` refs

> PPCHPLOT — the one painter every Charts curve figure draws with: a Canvas over a plot's AREA (no gutters),
> mapping time t on [x0, x1] across its width and value v on [lo, hi] up its height. It paints only what it
> is handed — the numbers are the presenters' — in back-to-front order: furniture (P-position hairlines, the
> value grid, the zero line), bands, quiet lines, dots, then strong lines.
> layers: [ { k: "band",  t, lo, hi, color }                                   a wash (metric hue at envAlpha)
> | { k: "line",  t, v, color, w, alpha, dash, runs, shot, z }          a curve; NaN = a break
> | { k: "dots",  t, v, color, alpha, d }                               raw samples
> | { k: "vline", t, color, dash }                                      e.g. where the curves part
> | { k: "hline", v, color, dash }                                      e.g. the difference's zero
> | { k: "bridges", list: [{ t0, v0, t1, v1 }], color, w, alpha, shot } ]  the presenter's BRIDGES: a
> dashed straight line across a run that is no measurement
> runs (This shot's honesty, F18): [{ from, to, style: "measured" | "bridged" | "outside" }] inclusive index
> ranges: a bridged run has no values (its dashed bridge is a "bridges" layer), outside the metric's phase
> window is dimmed.
> shot (an ordinal, optional): the curve belongs to that shot, so the Brush can light it (hover) or let it
> recede (a question's highlight). z orders lines (higher on top).

- **Properties:** `QtObject roles`, `var brush`, `var layers`, `real x0`, `real x1`, `real lo`, `real hi`, `var pTicks`, `var yTicks`, `bool zeroLine`, `readonly var deps`
- **Functions:** `fin(v)`, `xOf(t)`, `yOf(v)`, `tOf(px)`
- **Used by (4):** `PpChKindCurve`, `PpChReadingTrace`, `PpChSessionFigure`, `PpChTraceFigure`

### PpChProfile
`src/Gui/charts/PpChProfile.qml` · 218 lines · root `Column` · no direct `Theme.*` refs

> PPCHPROFILE — readings as ruled rows (design §2.1 "Apart from the session", D5): each with this shot's
> value, the session median, its place and a box of the session with this shot ringed (PpChBoxCell), under
> family crossheads when `groups` is given. Every field is a ChartsRelationsPresenter READING ROW's. A value a
> diagnostics condition graded and found outside its corridor is set in warn AND carries its word
> (firedWord). D5: boxes are muted at rest and take the metric's hue on hover, or when the reading is apart.
> A row opens IN PLACE (body.openId): its curve with a box at every P-position (PpChReadingTrace) when it has
> one, else its run through the session (PpChRunBox) — `openRow` is the presenter's.
> column          tier  below it folds to
> name, value     1     (line 1)
> box             1     (line 1, narrower: ch.profileBoxEm by class)
> unit            1     (line 1)
> at (P tag)      3     line 2, first
> median          3     line 2, "median 72"
> place           3     line 2, "highest of 15"
> objectNames: chApart (the apart list) / chProfile (every reading); each row chApartRow (+ readingId),
> the open one also chApartRowOpen.

- **Properties:** `QtObject roles`, `var brush`, `QtObject presenter`, `var rows`, `var groups`, `string headLabel`, `string shotLabel`, `string countLabel`, `string openId`, `var openRow`, `real playMs`, `string playText`, `bool interactive`, `bool showPlace` … (+14 more)
- **Signals:** `rowActivated(string readingId)`, `shotActivated(int ordinal)`, `trustClicked(string subject)`
- **Uses:** `PpChBoxCell`, `PpChReadingTrace`, `PpChRunBox`
- **Used by (2):** `PpChartsReadingsView`, `PpChartsShotView`

### PpChQuestions
`src/Gui/charts/PpChQuestions.qml` · 68 lines · root `Column` · no direct `Theme.*` refs

> PPCHQUESTIONS — at most three sharper questions (spec 15.7), each the presenter's (ChartsTracesPresenter
> .session.questions, ChartsRelationsPresenter .thisShot.asks / .relations.asks), each shown only because the
> data answers it. A press asks it: the register applies it IN PLACE (lights the `lit` shots on the Brush,
> sets the facet or band it names, or steps the view `go`), and the asked one stands in ink with its answer
> under it. Co-occurrence answers carry their caveat (the presenters write it). Print sets them as footnotes.
> objectName: chQuestions; each question chQuestion (+ questionId).

- **Properties:** `QtObject roles`, `var questions`, `string active`, `bool interactive`, `bool atom`, `readonly var keyKinds`
- **Signals:** `asked(var q)`
- **Used by (3):** `PpChartsRelationsView`, `PpChartsSessionView`, `PpChartsShotView`

### PpChReadingPicker
`src/Gui/charts/PpChReadingPicker.qml` · 68 lines · root `Column` · no direct `Theme.*` refs

> PPCHREADINGPICKER — choose a reading, IN PLACE (never a pop-up or a menu; design §2.3 "quiet Up / Across
> pickers that open in place"): a quiet question, then each family with its readings flowed as quiet links,
> the current one in ink. It pushes what is below it down and closes when the register sets `open` false (a
> pick, the control again). The catalogue is ChartsRelationsPresenter relations.catalogue.
> objectName: chReadingPicker; each reading chReadingOption (+ readingId).

- **Properties:** `QtObject roles`, `bool open`, `string title`, `string current`, `string exclude`, `var catalogue`, `bool interactive`, `readonly var keyKinds`
- **Signals:** `picked(string readingId)`
- **Used by (1):** `PpChartsRelationsView`

### PpChReadingTrace
`src/Gui/charts/PpChReadingTrace.qml` · 139 lines · root `Item` · no direct `Theme.*` refs

> PPCHREADINGTRACE — one reading's curve through this swing, set against the session AT ITS INSTANTS
> (ChartsRelationsPresenter TRACE; B's Trace): the 40 ms windowed mean in the metric's hue (the figures come
> from it), the raw samples faint behind it, a hairline at every P-position, and at each P-position the
> reading is taken at a small upright box of all the shots there with this shot ringed. The playhead follows
> the video; a click or drag seeks (seekUsAtShotMs). The value at the playhead is the presenter's text.
> objectName: chReadingTrace (+ readingId).

- **Properties:** `QtObject roles`, `var brush`, `QtObject presenter`, `var trace`, `bool interactive`, `real playMs`, `string playText`, `real plotH`, `bool atom`, `readonly var keyKinds`, `readonly string readingId`, `readonly string key`, `readonly real t0`, `readonly real t1` … (+9 more)
- **Signals:** `shotActivated(int ordinal)`
- **Functions:** `tx(t)`, `vy(v)`, `tAt(px)`
- **Uses:** `PpChBoxCell`, `PpChPlot`
- **Used by (1):** `PpChProfile`

### PpChReplaySession
`src/Gui/charts/PpChReplaySession.qml` · 93 lines · root `Column` · no direct `Theme.*` refs

> PPCHREPLAYSESSION — Replay's Session follows the playhead (D6; design §3): the lead reading shot by shot at
> the latest P-position the playhead has passed (ChartsRelationsPresenter.replaySession, which notifies only
> when that P-position or its readout changes — never per frame for nothing). The P-positions it is read at
> are links that seek there; "now" against "typical" in one sentence; the run with its box; the strongest
> partner's scatter while two columns fit; and three "moves with it" lines that end on the chance line and the
> co-occurrence caveat (the presenter's sentences).
> objectName: chReplaySession.

- **Properties:** `QtObject roles`, `var brush`, `QtObject presenter`, `var info`, `bool interactive`, `bool cast`, `bool atom`, `readonly var keyKinds`, `readonly bool ok`, `readonly bool side`
- **Signals:** `shotActivated(int ordinal)`
- **Uses:** `PpChRunBox`, `PpChScatter`, `PpChText`
- **Used by (1):** `PpChartsSessionView`

### PpChRoles
`src/Gui/charts/PpChRoles.qml` · 81 lines · root `PpTypeset` · 2 hard-coded colour(s)

> PPCHROLES — the Charts panel's vocabulary on top of the generic typeset helper (design system §15.3,
> docs/design/charts_panel_traces_relations.md §5). A Charts register makes ONE and passes it to every
> block as `roles`:      PpChRoles { id: ro; medium: root.medium }
> Never give it the id `roles` (see PpTypeset). It IS a PpTypeset, so the generic library blocks take it.
> WHAT IS HERE: the Charts design values (Theme.typeset.ch, with Theme.typesetPrint.ch over it on paper),
> the inks a metric is drawn in (Theme.metricColor at the ch alphas and weights — one hue per metric, never
> a second hue), the knock-out behind a label inside a plot, the Charts flow minimums, and ONE text helper:
> html(sentence) sets a presenter Sentence ({ text, runs: [{ text, fig }] }) with its figure runs in the
> figure face. NO number and NO word is made here: every one comes from the presenters
> (ChartsTracesPresenter, ChartsRelationsPresenter) — "QML positions and paints, C++ decides the numbers".

- **Properties:** `readonly var ch`, `readonly real envAlpha`, `readonly real boxFill`, `readonly color knock`, `readonly color surface`
- **Functions:** `hue(key)`, `hueA(key, a)`, `dash(pat)`, `html(s, tone)`, `has(s)`, `str(x)`
- **Uses:** `PpTypeset`
- **Used by (1):** `PpChartsRegister`

### PpChRunBox
`src/Gui/charts/PpChRunBox.qml` · 132 lines · root `Item` · no direct `Theme.*` refs

> PPCHRUNBOX — one reading through the session (ChartsRelationsPresenter RUN): one PpMark per shot in the order
> struck (warn where a condition reading it fired), this shot ringed, a not-measured shot a STUB on the foot
> (never a gap), and right of it the session's box on the same value axis (spec 15.8's run + box). The drift
> line (least squares on shot order) is drawn only when the presenter gives one (it clears chance); the
> caption always carries r and its interval. Ticks are the presenter's (ticks()).
> objectName: chRun (+ readingId).

- **Properties:** `QtObject roles`, `var brush`, `QtObject ticker`, `var run`, `bool interactive`, `bool showCaption`, `bool showBox`, `real plotH`, `bool atom`, `readonly var keyKinds`, `readonly string readingId`, `readonly string key`, `readonly var pts`, `readonly int n` … (+8 more)
- **Signals:** `shotActivated(int ordinal)`
- **Functions:** `sx(ord)`, `vy(v)`
- **Uses:** `PpChBoxCell`, `PpMark`
- **Used by (4):** `PpChProfile`, `PpChReplaySession`, `PpChWristSession`, `PpChartsRelationsView`

### PpChScatter
`src/Gui/charts/PpChScatter.qml` · 169 lines · root `Item` · no direct `Theme.*` refs

> PPCHSCATTER — one reading against another (ChartsRelationsPresenter SCATTER; design §2.3): a mark per shot
> (ink: a mark stands for a SHOT; warn + a tick where the facet's condition fired on it), this shot ringed,
> each axis' session box on its edge in the reading's hue, the axis titles in their hues, and the least-
> squares line: ink2 and solid when it clears chance, ink3 dashed (ch.chanceDash) inside chance. The caption
> is the presenter's ("r .62 (.17 to .86) · n 15 · inside chance"). Ranges are shared across facet panels
> (the presenter gives the same xRange / yRange to every panel). Ticks are the presenter's.
> objectName: chScatter.

- **Properties:** `QtObject roles`, `var brush`, `QtObject ticker`, `var scatter`, `string xKey`, `string yKey`, `string title`, `bool interactive`, `bool showMarginals`, `bool showYLabels`, `bool showAxisNames`, `bool showCaption`, `real plotH`, `bool atom` … (+13 more)
- **Signals:** `shotActivated(int ordinal)`
- **Functions:** `px(v)`, `py(v)`, `byOrdinal(field)`, `firedByOrdinal()`
- **Uses:** `PpChBoxCell`, `PpMark`
- **Used by (2):** `PpChReplaySession`, `PpChartsRelationsView`

### PpChSeqChain
`src/Gui/charts/PpChSeqChain.qml` · 82 lines · root `Column` · no direct `Theme.*` refs

> PPCHSEQCHAIN — one shot's kinematic sequence as ONE typeset chain (design §2.1: "Lead arm — 58 ms →
> Club — 69 ms → Pelvis, late"), from ChartsTracesPresenter.thisShot.sequence: the placed peaks in the order
> they happened, the gap on each arrow, and the segment that peaked late in warn, underlined, with its word
> (D4: only that one). Then one quiet line: the order it should be, and what was not placed and why
> (sequence.note), and — wherever the pelvis is said late — the neutral note (thisShot.note, rule 5).
> objectNames: chSeqChain; each link chSeqLink:<segment>; the late one also chSeqLate:<segment>.

- **Properties:** `QtObject roles`, `var sequence`, `var lateNote`, `bool showNote`, `bool showOrderNote`, `bool atom`, `readonly var keyKinds`, `readonly var links`
- **Signals:** `trustClicked(string subject)`
- **Uses:** `PpChText`
- **Used by (1):** `PpChartsShotView`

### PpChSeqSession
`src/Gui/charts/PpChSeqSession.qml` · 194 lines · root `Column` · no direct `Theme.*` refs

> PPCHSEQSESSION — the sequence across the session (design §2.2): an ORDER BARCODE (each segment's rank on
> every shot, a late rank in warn and underlined — a mark plus a word in the head line) and a PEAK-TIME DOT
> PLOT on one ms axis (each shot a PpMark, late ones warn, the median a rule, this shot ringed; marks that
> would overprint stack upward). Every number is ChartsTracesPresenter.session.sequence's; the stand-first
> sentence (stand) says the order count and the late pelvis neutrally. Hover a shot and it lights in both.
> objectNames: chSeqBarcode, chSeqDots.

- **Properties:** `QtObject roles`, `var brush`, `var sequence`, `bool interactive`, `bool atom`, `readonly var keyKinds`, `readonly var segs`, `readonly int nShots`, `readonly real em`, `readonly real nameW`, `readonly bool figsSide`, `readonly real figW`, `readonly real plotW`, `readonly real pitch` … (+7 more)
- **Signals:** `shotActivated(int ordinal)`
- **Functions:** `xOf(ms)`
- **Uses:** `PpChText`, `PpMark`
- **Used by (1):** `PpChartsSessionView`

### PpChSeqTable
`src/Gui/charts/PpChSeqTable.qml` · 200 lines · root `Column` · no direct `Theme.*` refs

> PPCHSEQTABLE — the per-segment peak table (CONTRACT Addendum, F31): in the order the segments should peak,
> should / actual rank ("?" on an unplaced one, the late one in warn with its word), the peak's time ± σ,
> its speed ± σ and its % of pro (the tour average, Cheetham 2008) ± σ; a trunk not peaked by impact keeps
> its open chevron and "rising" (S8). Every text is ChartsTracesPresenter.thisShot.sequence.table's.
> Tiers: segment, should, actual, peak — 1; °/s and % of pro — 2 (below compact they fold to line 2).
> objectNames: chSeqTable; each row chSeqRow:<segment>.

- **Properties:** `QtObject roles`, `var sequence`, `bool atom`, `readonly var keyKinds`, `readonly var rows`, `readonly bool wide`, `readonly real em`, `readonly real gap`, `readonly real segW`, `readonly real rankW`, `readonly real peakW`, `readonly real speedW`, `readonly real pctW`
- **Signals:** `trustClicked(string subject)`
- **Used by (1):** `PpChartsShotView`

### PpChSessionFigure
`src/Gui/charts/PpChSessionFigure.qml` · 361 lines · root `Item` · no direct `Theme.*` refs

> PPCHSESSIONFIGURE — Session's figure (design §2.2, §4): every shot's curve for each metric of the preset,
> P-ALIGNED (D9: the session's P-positions, no clock-time view), as shared-axis small multiples. Drawn from
> ChartsTracesPresenter.session only; QML maps values to pixels.
> • band "middle": the middle half as the metric's hue at envAlpha and the median (weight, not a 2nd hue);
> "every": each other shot the same hue at otherShotAlpha (hover one and it lights in every row);
> "fit" (D2): the group's curve (long dash) with ± 1 sd as the wash, blank where < 3 shots contribute,
> refused (with the presenter's reason) for a group < 3, and under the plot the coverage strip: one
> hairline per shot where it was measured, this shot first in ink;
> • this shot at full strength in its own group's panel, with its honesty runs;
> • where the curves part (the presenter's part.canonMs) as a dotted rule;
> • facets side by side while a panel keeps 22 em (ch.facetEm), else OVERLAID: this shot's group, the
> other group's median / fit dashed (panel.alt) — a diagram below its minimum changes form (rule 8);
> • the P tags along the top and, once under the figure, the middle half of WHEN each P-position happened
> across the shots in ms from impact (ticks[].when: the timing the warp removed, kept quietly);
> • the playhead at the picked shot's canonical time (now.canonMs); click or drag seeks the picked shot;
> over a shot's curve (every shot) the pointer lights that shot, and a click puts it in the carousel.
> …

- **Properties:** `QtObject roles`, `var brush`, `QtObject presenter`, `bool interactive`, `var session`, `var nowRows`, `real playCanon`, `string figMode`, `int pickedOrdinal`, `real plotH`, `readonly var keyKinds`, `bool atom`, `readonly var rows`, `readonly var groups` … (+22 more)
- **Signals:** `shotActivated(int ordinal)`
- **Functions:** `covH(r)`, `ownIndex()`, `byKey(list, key)`, `panelOf(r, group)`, `layersFor(r, p, withPicked, withAlt)`
- **Uses:** `PpChPlot`
- **Used by (1):** `PpChartsSessionView`

### PpChSummary
`src/Gui/charts/PpChSummary.qml` · 127 lines · root `Column` · no direct `Theme.*` refs

> PPCHSUMMARY — the window-scoped readings of today's summary cards (CONTRACT Addendum: F33–F40, S9, S10),
> as a ruled ledger: one row per preset member (the traced rows, then the curve-less cards), each with its
> tiles — the card's own instants ("at impact"), PEAK, Δ and PK RATE recomputed for the reader's window —
> each value with its ± and unit as the presenter wrote them; "partial" when part of the window was never
> measured; "-" when the window has no valid data; the value in warn plus its word when its band says out.
> Line 2 states what backs the row's ± (σ provenance, F36) and the ⚠ wrong-structure caveat (F37) inline —
> never a tooltip. Hovering a row shows its tiles' own notes there instead ("Recorded sample there: raw 503").
> Companion readings (shaft lie on shaft lean, F39) arrive as tiles on their card: drawn as given.
> A tile a gate left without a value carries the gate's words in its place and `trust` (the reading): the
> words in the link colour with " ›", a click emits trustClicked (trust stage 6 session 2). A member a gate
> refused outright arrives as a stub card: its name and that one tile.
> objectNames: chSummary; each row chSummaryRow (+ key); a gated tile chSummaryWhy:<subject>.

- **Properties:** `QtObject roles`, `var rows`, `var cards`, `bool interactive`, `bool atom`, `readonly var keyKinds`, `readonly var all`, `readonly real em`, `readonly bool side`, `readonly real nameW`, `readonly real cellW`, `readonly real gap`
- **Signals:** `trustClicked(string subject)`
- **Used by (1):** `PpChartsShotView`

### PpChText
`src/Gui/charts/PpChText.qml` · 41 lines · root `Text`

> PPCHTEXT — one presenter Sentence ({ text, runs: [{ text, fig }] }) set as a paragraph: the words in the
> role given, the figure runs in the figure face (PpTypeset.figHtml). Wraps, never elides. Hidden when the
> sentence is empty, so a block can always place it. Held to the prose measure unless `measure` is false.

- **Properties:** `QtObject roles`, `var sentence`, `bool measure`, `var tone`, `readonly bool present`
- **Used by (10):** `PpChReplaySession`, `PpChSeqChain`, `PpChSeqSession`, `PpChWristSession`, `PpChWristShotSection`, `PpChartsKindsView`, `PpChartsReadingsView`, `PpChartsRelationsView`, `PpChartsSessionView`, `PpChartsShotView`

### PpChTraceFigure
`src/Gui/charts/PpChTraceFigure.qml` · 576 lines · root `Item` · no direct `Theme.*` refs

> PPCHTRACEFIGURE — This shot's figure (design §2.1, CONTRACT Addendum): the preset's 40 ms windowed-mean
> traces as shared-axis small multiples, or OVERLAID on one shared value axis (F6), on ONE time axis of ms
> from impact over the reader's window. Everything it draws is a field of ChartsTracesPresenter.thisShot /
> .reference / .now: QML maps values to pixels and nothing else.
> • a row: the metric's curve in its own hue (Theme.metricColor), its raw samples faint behind it (F17); the
> honesty runs (F18): bridged dashed, outside the metric's phase window dimmed; NaN draws nothing;
> • peaks with their σ (a timing whisker; the ± is in the figures), the late one ringed in warn plus its
> word (D4: only the segment that peaked late), an unplaced peak a dim ring, a trunk not peaked by impact
> the open chevron "rising" at the impact edge (S8);
> • P dots by verdict (F23) — filled = pass, ring = watch, ring + dot in warn = outside, small ink3 =
> "position, not judged" — with their key in words above the figure (thisShot.dotKey); toggled (F7);
> • the playhead, one ink hairline (F24); click or drag seeks through the presenter (F25, scrub bracketed);
> • the hover crosshair (F26), toggled (F7): one hairline through every row, the time over the P tags, and
> each row's figure column turned IN PLACE into the reading there ("raw N" beside the mean, "-" where not
> measured, the ⚠ wrong-structure word) — no floating tooltip (§15.7);
> • Against (design §2.1): the reference curve in the same hue (median thin, a shot dashed, "like this one"
> …

- **Properties:** `QtObject roles`, `var brush`, `QtObject presenter`, `bool interactive`, `bool seekable`, `var rows`, `var ticks`, `var msTicks`, `real x0`, `real x1`, `bool overlay`, `var overlayAxis`, `bool showDots`, `bool showCursor` … (+29 more)
- **Signals:** `trustClicked(string subject)`
- **Functions:** `hoverAtMs(ms)`, `xOf(t)`, `tOf(px)`, `refRowOf(key)`, `byKey(list, key)`, `refStyle()`, `layersFor(r)`, `itemsFor(r, still)`
- **Uses:** `PpChPlot`
- **Used by (1):** `PpChartsShotView`

### PpChWindowBrush
`src/Gui/charts/PpChWindowBrush.qml` · 159 lines · root `Item` · no direct `Theme.*` refs

> PPCHWINDOWBRUSH — the time window over the whole recording (CONTRACT Addendum, F14): the overview sparkline
> of each visible metric in its own hue (thisShot.overview, each scaled to its own vLo…vHi), one ink3 track
> for the recording, the P-positions as hairlines with their tags (thisShot.recordingTicks; top and impact
> stronger, tags thinned by width, impact kept), the reader's window as the one darker span with two
> handles, and the playhead. Drag the window to move it, an edge to resize it, or sweep anywhere else for a
> new one; the result is a CUSTOM window handed to the body (customRequested), which the presenter then reads
> and reduces for. Pixels ↔ µs only: every time it shows is the presenter's or shotReplay's.
> objectName: chWindowBrush.

- **Properties:** `QtObject roles`, `bool interactive`, `real spanUs`, `real winStartUs`, `real winEndUs`, `var ticks`, `var overview`, `real playUs`, `readonly real trackH`, `readonly real sparkH`, `string drag`, `real dA`, `real dB`, `real grabUs` … (+2 more)
- **Signals:** `customRequested(real startUs, real endUs)`
- **Functions:** `xOf(us)`, `usOf(px)`
- **Used by (1):** `PpChartsShotView`

### PpChWristLedger
`src/Gui/charts/PpChWristLedger.qml` · 118 lines · root `Column` · no direct `Theme.*` refs

> PPCHWRISTLEDGER — FINDINGS / WORKING WELL in This shot's wrist section (W6, W7): the wrist-domain diagnostics conditions
> that fired on this shot (or read clean on every tail: a strength), in the presenter's order. A row: the mark
> and its word (the session tier: pattern filled, watching a ring — a mark AND a word, never colour alone; a strength ink3), the name,
> the points it cost, the positions (the first a seek link), the magnitude and the confidence pips, the
> explanation; "more" opens coaching (or protect), linked to and corroborated by IN PLACE. Every word is the
> presenter's FINDING; nothing is composed here.
> objectNames: chWristFindings / chWristStrengths (by `strengths`); each row chWristFinding (+ findingId).

- **Properties:** `QtObject roles`, `string title`, `var items`, `bool strengths`, `string openId`, `string emptyText`, `bool interactive`
- **Signals:** `seek(real us)`, `toggled(string id)`
- **Used by (1):** `PpChWristShotSection`

### PpChWristProfile
`src/Gui/charts/PpChWristProfile.qml` · 300 lines · root `Item` · no direct `Theme.*` refs

> PPCHWRISTPROFILE — THE CHECKPOINT PROFILE (docs/design/wrist_motion_into_charts.md §4 B, W1 W2 W10–W13): one
> row per wrist DOF, the eight checkpoints as a discrete x axis, Δ from address on y. The band is the chart
> wash per checkpoint; this swing is drawn in its metric's hue (Theme.metricColor, identity); a value outside
> its band is a warn mark (filled = fault, ring = watch) AND a word under its number ("fault · 19° open"), never
> colour alone; the playhead's checkpoint is the picked column through every row; the Against ghost is dashed
> with hollow points; in Session scope a box per checkpoint (PpChBoxCell, this shot ringed) and, under it, the
> median and how wide the middle half is; the curve behind on demand. The numbers under every column ARE the
> old grid; the grid-only DOF (lead elbow) is a numbers-only row.
> Every word, number and scale is ChartsWristPresenter's (profile.rows: ROW, positions: POS); QML maps to pixels.
> objectNames: chWristProfile; each row chWristRow (+ dof); each column head chWristCol (+ tag).

- **Properties:** `QtObject roles`, `var brush`, `QtObject ticker`, `var rows`, `var positions`, `int selected`, `string mode`, `bool interactive`, `readonly bool ok`, `readonly bool sideLabel`, `readonly real labelW`, `readonly real poleW`, `readonly real gutY`, `readonly real plotX` … (+9 more)
- **Signals:** `columnActivated(int index)`, `trustClicked(string subject)`
- **Functions:** `cx(i)`, `aboveH()`, `rowH(r)`
- **Uses:** `PpChBoxCell`
- **Used by (2):** `PpChWristSession`, `PpChWristShotSection`

### PpChWristSession
`src/Gui/charts/PpChWristSession.qml` · 170 lines · root `Item` · no direct `Theme.*` refs

> PPCHWRISTSESSION — THE WRIST SECTION OF SESSION (docs/design/wrist_motion_into_charts.md §4; PpChartsSessionView
> draws it under its own section head "Wrist", after the standard session content): "what is my wrist style, and
> how repeatable is it?" The head and standfirst (the style each shot was graded as, the score's median, the most
> and least repeatable checkpoint, the resemblance index against what Auto chose, the session's size said);
> the checkpoint profile as one box per checkpoint across the shots over the picked shot's band, this shot
> ringed, the median and how wide the middle half is under each; then — in a right rail at wide — the score shot
> by shot with its box (PpChRunBox), the style table, and what keeps firing as a mark per shot that picks that
> swing. Every word and number is ChartsWristPresenter.session's.
> objectName: chWristSession.

- **Properties:** `QtObject roles`, `var brush`, `var body`, `bool interactive`, `readonly var se`, `readonly bool ok`, `readonly bool available`, `readonly bool rail`, `readonly real railW`, `readonly real mainW`
- **Uses:** `PpChRunBox`, `PpChText`, `PpChWristProfile`, `PpMark`, `PpSectionHead`
- **Used by (1):** `PpChartsSessionView`

### PpChWristShotSection
`src/Gui/charts/PpChWristShotSection.qml` · 295 lines · root `Item` · no direct `Theme.*` refs

> PPCHWRISTSHOTSECTION — THE WRIST SECTION OF THIS SHOT (docs/design/wrist_motion_into_charts.md §4, as Mark
> re-surfaced it on 16 Oct: the standard curves first, the domain read added when it is meaningful). "At each
> checkpoint, where was the wrist against its band, and what did it cost?" PpChartsShotView draws it under its
> traces only when this shot carries a wrist reading AND a wrist metric is among the curves on show
> (body.wristShotShown); it is a section of the page, never a page of its own.
> Analyse: the section head "Wrist"; the wrist head (the first wrist finding; with none, the out-of-band
> checkpoints said, W-D6), the standfirst at the playhead's checkpoint, the wrist score with its sum (a
> figure at the right at medium, one line under the head below); the quiet controls — Bands (Auto →
> resolved), Against (address · previous · reference ★ · session median), Curves behind, and "★ set this
> swing as your reference" — folded to one summary line below medium (decision rule 5); the resemblance
> line; the checkpoint profile (PpChWristProfile, a column seeks); Findings | Working well
> (PpChWristLedger) and what moves with the lead wrist at impact (PpChMovesWith), in a right rail at wide.
> Replay (the glance at the mat, and the cast): the head, the standfirst, the score line and the profile;
> no controls, no ledgers.
> Every word and number is ChartsWristPresenter's (body.w*, each bound ONCE on the body); the choices go to the
> body (body.choose: wristAgainst, wristCurves, wristArchetype — persisted per screen × mode by the panel). The
> …

- **Properties:** `QtObject roles`, `var brush`, `var body`, `string reg`, `bool interactive`, `bool cast`, `readonly bool replay`, `readonly var ty`, `readonly var wc`, `readonly var sh`, `readonly bool has`, `readonly var score`, `readonly var prof`, `readonly bool rail` … (+11 more)
- **Functions:** `seekTo(us)`, `openTrust(subject)`, `esc(t)`, `setReference()`, `picked(set, opt)`
- **Uses:** `PpChChoices`, `PpChFoldHead`, `PpChMovesWith`, `PpChText`, `PpChWristLedger`, `PpChWristProfile`, `PpSectionHead`, `PpTrustInlineFor`, `TrustFocus`
- **Used by (1):** `PpChartsShotView`

### PpChartsAnalyse
`src/Gui/charts/PpChartsAnalyse.qml` · 28 lines · root `PpChartsRegister`

> PPCHARTSANALYSE — the Charts panel's Analyse register (design §3; spec 15.6): PpChartsRegister with its
> register set. The read: head and controls (in a left rail at wide), the traces with every single-shot
> capability of today's panel, the session behind them, Relations and Kinds, questions answered in place.

- **Uses:** `PpChartsRegister`
- **Used by (1):** `PpChartsBody`

### PpChartsBody
`src/Gui/charts/PpChartsBody.qml` · 347 lines · root `Item`

> THE CHARTS PANEL'S BODY: the reader's choices and view state, the frame on surfaces that have no stage card
> round them, and the TYPESET REGISTER that draws everything else (design system §15; docs/design/
> charts_panel_traces_relations.md; build/charts-build/STEP2B.md).
> IT TAKES THE PRESENTERS AS SOURCES AND OWNS NONE OF THEM. PpChartsPanel owns the SessionSeriesModel and the
> presenters (ChartsTracesPresenter, ChartsRelationsPresenter, ChartsWristPresenter), feeds them, persists what this body
> chooses, and keeps the reader's place; this file binds each presenter output map ONCE (every read of a
> QVariantMap property is a deep copy) and hands the views those bindings: t* (traces), r* (relations) and
> w* (wrist).
> THE WRIST READ IS A SECTION, NOT A TAB (Mark, 16 Oct; docs/design/wrist_motion_into_charts.md §4): the standard
> curves first, the domain read added when it is meaningful. wristShotShown / wristSessionShown say when This
> shot / Session add their "Wrist" section: the shot (or some shot of the session) carries a wrist reading AND a
> wrist metric is among the curves on show — a member of the traces' preset that is on (tChoices.metrics[].on)
> whose key is one of the wrist presenter's strip rows (profile.rows[].key, "hm." prefix ignored on both sides).
> THE REGISTER IS CHOSEN, NOT THE LAYOUT (§15.6): forceRegister (tests) → surface "cast" → Replay →
> SessionMode: Analyse reads in depth (PpChartsAnalyse); Capture and Replay glance (PpChartsReplay).
> The register is handed `body: root` and reads and drives everything through this API.
> …

- **Properties:** `QtObject tracesP`, `QtObject relationsP`, `QtObject wristP`, `readonly var tChoices`, `readonly var tShot`, `readonly var tRef`, `readonly var tSession`, `readonly var tNow`, `readonly var rStatus`, `readonly var rShot`, `readonly var rOpen`, `readonly var rRel`, `readonly var rKinds`, `readonly var rReplay` … (+50 more)
- **Signals:** `choiceMade(string name, var value)`, `sectionToggled(string name, bool collapsed)`, `navChanged()`, `demoReset()`, `shotRequested(string swingDir, int shotId)`
- **Functions:** `choose(name, value)`, `toggleMetric(key)`, `chooseCustom(startUs, endUs)`, `chooseUp(id)`, `toggleSection(name)`, `openReading(id)`, `ask(q, brush)`, `clearAsk(brush)`, `questionById(id)`, `pickOrdinal(ord)` (+2)
- **Uses:** `PpCardShell`, `PpChartsAnalyse`, `PpChartsReplay`, `PpMicro`, `SessionMode`
- **Used by (1):** `PpChartsPanel`

### PpChartsKindsView
`src/Gui/charts/PpChartsKindsView.qml` · 114 lines · root `Column` · no direct `Theme.*` refs

> KINDS — "what kinds of swing were there, and what tells them apart?" (design §2.4, B + A; D7). Analyse
> only: Replay has no Kinds form and reads Session there (the body's rule). How weak the structure is comes
> FIRST, in the presenter's words (silhouette; "no structure" then the one split, labelled a split, not kinds);
> then the members and the most typical shot of each, what else separates them (each kind's box on one scale,
> d against the d chance gives), each kind's pooled curve with each shot's own measured stretches under it,
> A's curve-shape groups as a second grouping, and the method, fit and alignment notes.
> objectName: chKindsView.

- **Properties:** `QtObject roles`, `var brush`, `var body`, `string reg`, `bool interactive`, `bool cast`, `real pageH`, `readonly var kd`, `readonly bool available`
- **Uses:** `PpChKindCurve`, `PpChKindTable`, `PpChText`, `PpNotes`, `PpSectionHead`
- **Used by (1):** `PpChartsRegister`

### PpChartsPanel
`src/Gui/charts/PpChartsPanel.qml` · 380 lines · root `Item` · no direct `Theme.*` refs

> THE CHARTS STAGE PANEL: this swing's metric curves and the session behind them, in two voices — Traces (the
> instrument) and Relations (the workbench) — across four views: This shot · Session · Relations · Kinds
> (docs/design/charts_panel_traces_relations.md; build/charts-build/CONTRACT.md D1–D9 and its Addendum). The wrist
> read is no tab of its own: This shot and Session add a "Wrist" section when a shot carries a wrist reading and a
> wrist metric is on show (docs/design/wrist_motion_into_charts.md §4; PpChartsBody.wristShotShown).
> THIS FILE IS THE WIRING AND NOTHING ELSE (the Session diagnostics pattern, INVENTORY §7). Everything the
> panel LOOKS like is PpChartsBody, which takes the presenters as sources and owns none of them. Here:
> • THE MODEL AND THE PRESENTERS ARE INSTANTIATED, NOT REACHED FOR: one SessionSeriesModel and the three
> presenters (ChartsTracesPresenter, ChartsRelationsPresenter, ChartsWristPresenter), each with a private worker thread. The
> model is fed exactly as its header says: the session folder (the same binding as the diagnostics
> panel), ShotProcessor.shotProcessed → ingestShot, the carousel's re-analysis → regradeShot, and the
> diagnostics ledger written (SessionMode.sessionDiagnostics busy → false) → refreshDiagnostics.
> • THE _screenActive GATE STAYS (INVENTORY F41, cbb63aae): all four session screens live in one
> StackLayout, and both hosts (ScreenSessionMode, ScreenWrist) carry this panel. An invisible screen's
> presenters are fed nothing: the picked swing, its detail, the span and the playhead are copied in only
> while this screen is the visible one, so a replay on another screen costs this one no recompute.
> …

- **Properties:** `string title`, `int sessionType`, `string defaultTab`, `string defaultPreset`, `alias framed`, `readonly string cardAside`, `alias asideHosted`, `readonly bool asideActive`, `readonly bool asideOn`, `alias interactive`, `alias surface`, `alias demoState`, `alias tab`, `readonly string view` … (+3 more)
- **Functions:** `activateAside()`
- **Uses:** `PpChartsBody`, `SessionMode`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`
- **Exercised by tests/tools:** `ChartsHarness`

### PpChartsReadingsView
`src/Gui/charts/PpChartsReadingsView.qml` · 204 lines · root `Column` · no direct `Theme.*` refs

> READINGS — every reading against the session, as its own tab (Mark, 17 Oct 2026: it lets you look at any curve,
> and at the foot of This shot nobody found it). By family, two columns at wide, with a filter on the name. A row
> opens IN PLACE: the picked shot's curve with the session's box at every P-position, or, for a reading with no
> curve, its run through the session (PpChProfile; the relations presenter's thisShot.groups and openRow). It
> answers for the shot and the session at once, so both registers have it.
> Beside the name, this shot's verdict (Mark, 17 Oct 2026: "just glance at the readings that need attention"): All ·
> Faults (past the fault line) · Outside (out of the corridor, faults included) · Inside (graded clean), each with its
> count under the name filter; a reading no condition graded shows under All only (row.verdict, the presenter's).
> objectName: chReadingsView, chReadingsFilter, chReadingsVerdict:<all|fault|outside|inside>.

- **Properties:** `QtObject roles`, `var brush`, `var body`, `string reg`, `bool interactive`, `bool cast`, `real pageH`, `readonly var rs`, `readonly bool available`, `string filter`, `string verdict`, `readonly var named`, `readonly var groups`, `readonly var counts` … (+3 more)
- **Functions:** `inVerdict(r, v)`
- **Uses:** `PpChProfile`, `PpChText`
- **Used by (1):** `PpChartsRegister`

### PpChartsRegister
`src/Gui/charts/PpChartsRegister.qml` · 216 lines · root `Item` · no direct `Theme.*` refs

> PPCHARTSREGISTER — what the Charts registers share (PpChartsReplay and PpChartsAnalyse are this with their
> `register` set; design §3, spec 15.6). A register is how much the panel says, never its encodings: the
> same views (PpChartsShotView, PpChartsSessionView, PpChartsRelationsView, PpChartsKindsView) are drawn by
> both, told which register they are in.
> • the tab row: quiet PpTabs (underline, no capsule) — This shot · Session · Relations · Kinds in Analyse;
> Replay has no Kinds form, so its tab row stops at Relations and a Kinds choice reads Session there.
> There is no Wrist tab (Mark, 16 Oct): the wrist read is a section of This shot and of Session, drawn by
> those views when it is meaningful (body.wristShotShown / wristSessionShown). A saved "wrist" reads This shot.
> The register never owns the tab: a press writes body.tab (the body persists it).
> • Replay keeps the preset as a quiet chip at the right of the tab row; it opens an inline list under the
> row (never a menu).
> • the cast (surface "cast"): always this register in Replay; an auto-closing cast (interactive false)
> has no tab row but a running head (the shot on the left, the view and preset on the right), takes no
> clicks, and is zoomed as a WHOLE for viewing distance — 1 to 1.6, from an em estimate of the view's
> rows (data), never measured from its own layout, never below compact.
> • each view is its own named Flickable (chShotFlick, chSessionFlick, chRelationsFlick, chKindsFlick),
> …

- **Properties:** `string register`, `var body`, `string view`, `string surface`, `bool interactive`, `string demoState`, `string medium`, `readonly var demoStates`, `readonly bool replay`, `readonly bool cast`, `readonly bool castHeadOnly`, `readonly string shown`, `real demoHoverMs`, `string demoPicker` … (+6 more)
- **Functions:** `applyDemo()`, `pickView(key)`
- **Local components:** `ViewFlick`
- **Uses:** `PpBrush`, `PpChRoles`, `PpChartsKindsView`, `PpChartsReadingsView`, `PpChartsRelationsView`, `PpChartsSessionView`, `PpChartsShotView`, `PpInlinePicker`, `PpTabs`
- **Used by (2):** `PpChartsAnalyse`, `PpChartsReplay`

### PpChartsRelationsView
`src/Gui/charts/PpChartsRelationsView.qml` · 192 lines · root `Column` · no direct `Theme.*` refs

> RELATIONS — "what moves together, and what drifts?" (design §2.3, B).
> Analyse: the head ("What moves with clubhead speed") and its standfirst; quiet Up / Across / Facet
> controls whose reading pickers open IN PLACE (no menu, no dialog); the key; the scatter of the two
> readings (or, faceted by a fault, its small multiples, each with its own r and the facet's sentence)
> beside the Up reading's run and box; the chance line said out loud; the "Moves with" ledger (ten, then
> the rest on demand); its note; up to three questions; the family matrix; the drifts that clear chance.
> Replay: the head and its short form, the scatter, the chance sentence and the top three "moves with".
> Every sentence ends where the presenter ends it: co-occurrence, not a cause. The presenter's refusal (n < 3)
> is what an empty view says.
> objectName: chRelationsView.

- **Properties:** `QtObject roles`, `var brush`, `var body`, `string reg`, `bool interactive`, `bool cast`, `real pageH`, `string openPicker`, `readonly bool replay`, `readonly var rl`, `readonly bool available`, `readonly bool side`, `string pickerOpen`, `readonly string picker` … (+1 more)
- **Uses:** `PpChChoices`, `PpChMatrix`, `PpChMovesWith`, `PpChQuestions`, `PpChReadingPicker`, `PpChRunBox`, `PpChScatter`, `PpChText`, `PpPageKey`, `PpSectionHead`
- **Used by (1):** `PpChartsRegister`

### PpChartsReplay
`src/Gui/charts/PpChartsReplay.qml` · 28 lines · root `PpChartsRegister`

> PPCHARTSREPLAY — the Charts panel's Replay register (design §3; spec 15.6): PpChartsRegister with its
> register set. The glance beside the video, and the cast: answer heads, key figures, the traces with the
> readings at the playhead (click or drag seeks), Session following the playhead (D6); no Kinds form.

- **Uses:** `PpChartsRegister`
- **Used by (1):** `PpChartsBody`

### PpChartsSessionView
`src/Gui/charts/PpChartsSessionView.qml` · 240 lines · root `Column` · no direct `Theme.*` refs

> SESSION — "what do my curves usually look like, and where do they part?" (design §2.2, A; D2, D3, D6, D9).
> Analyse: the head built from the data (where the curves part, the facet, the fit's payoff), its
> standfirst, the alignment note (§4.1), the mixed-club note, the co-occurrence caveat; the P-aligned
> figure; "How far can I trust this?"; the Controls fold (Metrics, Window, Show: middle half · every shot ·
> group fit, Facet: none by default, D3); up to three questions answered in place (a facet or band set, the
> lit shots); then the sequence across the session (order barcode and peak-time dot plot), folded.
> Replay (D6: follows the playhead): the head, the figure with "now" against "typical" in its column, and
> the lead reading at the latest P-position the playhead has passed (PpChReplaySession).
> A session the presenter refuses (n < 3, nothing aligned) says its own sentence (session.why).
> Then, in Analyse, the wrist section ("Wrist", PpChWristSession) when some shot of the session carries a wrist
> reading and a wrist metric is among the curves on show (body.wristSessionShown; Mark, 16 Oct: the standard
> curves first, the domain read added where it means something). The glance never adds it.
> objectNames: chSessionView, chSessionHead, chSessionControls, chFacetPicker (Analyse), chFacetPickerGlance.

- **Properties:** `QtObject roles`, `var brush`, `var body`, `string reg`, `bool interactive`, `bool cast`, `real pageH`, `readonly bool replay`, `readonly var se`, `readonly var tc`, `readonly var tn`, `readonly bool available`, `readonly int nRows`, `readonly real headEst` … (+8 more)
- **Functions:** `currentLabel(list)`, `picked(set, opt)`, `firstClause(sentence)`
- **Uses:** `PpChChoices`, `PpChFoldHead`, `PpChQuestions`, `PpChReplaySession`, `PpChSeqSession`, `PpChSessionFigure`, `PpChText`, `PpChWristSession`, `PpInlinePicker`, `PpTrustSummaryFold`
- **Used by (1):** `PpChartsRegister`

### PpChartsShotView
`src/Gui/charts/PpChartsShotView.qml` · 477 lines · root `Column` · no direct `Theme.*` refs

> THIS SHOT — "what did this swing's curves do, and how does it differ from my usual swing?" (design §2.1).
> One view, drawn in either register; the register chooses how much it says, never its encodings.
> Replay (glance): the answer head, the key figures with their place words, the sequence chain, and the
> traces with the readings AT THE PLAYHEAD; click or drag seeks.
> Analyse (read): the head, key figures and the quiet controls — preset and its metrics on/off ("Custom"),
> window (segment chips, Full recording, the data window, Custom from the brush), Against and its
> Difference, small multiples / overlay, P dots, cursor — fold into a left rail at wide; the window header
> and brush; the traces (peaks with σ, P dots by verdict with their key in words, honesty runs, hover
> crosshair with its in-place readout, the reference and its difference strip); then the Summary
> (window-scoped PEAK / Δ / PK RATE, partial, σ provenance, ⚠), the per-segment Sequence table (% of
> pro), "Apart from the session" with its questions, and every reading against the session (D5), a row
> opening its curve or run in place.
> The wrist section (PpChWristShotSection, "Wrist"), under the traces in either register, when this shot carries a
> wrist reading and a wrist metric is among the curves on show (body.wristShotShown): the standard curves first,
> the domain read added where it means something (Mark, 16 Oct).
> Every word and number is a presenter's (body.t* = ChartsTracesPresenter, body.r* = ChartsRelationsPresenter,
> …

- **Properties:** `QtObject roles`, `var brush`, `var body`, `string reg`, `bool interactive`, `bool cast`, `real pageH`, `var demoHoverMs`, `string openPicker`, `readonly bool replay`, `readonly var ts`, `readonly var tc`, `readonly var tr`, `readonly var tn` … (+22 more)
- **Functions:** `openTrust(subject, origin)`, `trustReadings(origin)`, `againstLabel()`, `windowLabel()`, `picked(set, opt)`
- **Uses:** `PpChChoices`, `PpChFoldHead`, `PpChKeyFigures`, `PpChProfile`, `PpChQuestions`, `PpChSeqChain`, `PpChSeqTable`, `PpChSummary`, `PpChText`, `PpChTraceFigure`, `PpChWindowBrush`, `PpChWristShotSection`, `PpInlinePicker`, `PpNotes`, `PpPageKey`, `PpTrustInline` (+2)
- **Used by (1):** `PpChartsRegister`
