# Session diagnostics

*Directory:* `src/Gui/diagnostics/`  
*32 components · 7,373 lines of QML*

The Session-diagnostics stage panel and its three registers (Analyse, Replay, Print), built from typeset blocks: ledger, sparklines, small multiples, cause graph, history strips, questions.

**C++ in this area:** `SessionDiagnosticsModel`, `WorkOnsController`, `WristDiagnosticsModel`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [PpBarcode](#ppbarcode) | 267 | `Item` | 3 | 4 |  |
| [PpBoxStrip](#ppboxstrip) | 178 | `Item` | 1 | 3 |  |
| [PpCauseGraph](#ppcausegraph) | 418 | `Item` | 2 | 3 | 1 local comp. |
| [PpDetailBar](#ppdetailbar) | 63 | `Item` | 1 | 1 |  |
| [PpDiagAnalyse](#ppdiaganalyse) | 757 | `Item` | 1 | 22 | 4 local comp. |
| [PpDiagPrint](#ppdiagprint) | 292 | `Item` | 0 | 17 | ⚠ unreferenced |
| [PpDiagReplay](#ppdiagreplay) | 556 | `Item` | 1 | 15 | 3 local comp. |
| [PpDiagReplayCold](#ppdiagreplaycold) | 52 | `Column` | 2 | 1 |  |
| [PpDiagReplaySessionRows](#ppdiagreplaysessionrows) | 75 | `Item` | 1 | 2 |  |
| [PpDiagReplayState](#ppdiagreplaystate) | 106 | `QtObject` | 2 | 0 |  |
| [PpDiagRoles](#ppdiagroles) | 229 | `PpTypeset` | 24 | 2 |  |
| [PpDotPlot](#ppdotplot) | 257 | `Item` | 2 | 3 |  |
| [PpEntryFlow](#ppentryflow) | 186 | `Item` | 1 | 2 |  |
| [PpExpectations](#ppexpectations) | 91 | `Column` | 1 | 1 |  |
| [PpFeatureHead](#ppfeaturehead) | 172 | `Item` | 2 | 1 |  |
| [PpFigureRow](#ppfigurerow) | 341 | `Item` | 2 | 7 | 2 local comp. |
| [PpHistoryStrip](#pphistorystrip) | 191 | `Item` | 1 | 4 |  |
| [PpIndexList](#ppindexlist) | 155 | `Item` | 1 | 1 |  |
| [PpKeyFigures](#ppkeyfigures) | 96 | `Item` | 2 | 1 |  |
| [PpLedgerTable](#ppledgertable) | 556 | `Item` | 3 | 5 |  |
| [PpMasthead](#ppmasthead) | 42 | `PpStandfirst` | 1 | 1 |  |
| [PpPathChains](#pppathchains) | 259 | `Item` | 2 | 1 |  |
| [PpQuestions](#ppquestions) | 155 | `Column` | 2 | 1 |  |
| [PpRunChart](#pprunchart) | 242 | `Item` | 1 | 3 |  |
| [PpSessionDiagnosticsBody](#ppsessiondiagnosticsbody) | 400 | `Item` | 1 | 5 |  |
| [PpSessionDiagnosticsPanel](#ppsessiondiagnosticspanel) | 431 | `Item` | 3 | 2 |  |
| [PpShotRuler](#ppshotruler) | 141 | `Item` | 1 | 0 |  |
| [PpSmallMultiple](#ppsmallmultiple) | 284 | `Item` | 1 | 3 |  |
| [PpSmallMultiples](#ppsmallmultiples) | 97 | `Column` | 1 | 2 |  |
| [PpSparkHead](#ppsparkhead) | 51 | `Item` | 1 | 1 |  |
| [PpSparkRow](#ppsparkrow) | 103 | `Item` | 1 | 4 |  |
| [PpSparkline](#ppsparkline) | 130 | `Item` | 2 | 3 |  |

### PpBarcode
`src/Gui/diagnostics/PpBarcode.qml` · 267 lines · root `Item` · no direct `Theme.*` refs

> BARCODE — C's Watching figure: condition × shot. Each row IS a PpShotStrip (the same drawing as every
> other strip, at a wider pitch); a firings-per-shot row closes it, and the picked shot is one column
> through the whole figure. Names inline from compact up (else above their row); this shot's value from
> medium up, else FOLDED onto the row's name line (as the ledger folds); sidenotes in a right margin at
> wide (else the register puts a PpNotes block below).
> rows: source.watching ([{id, name, recurrence}]); readout supplies each row's ticks and value.
> Round 4:
> • PRINT: the shot-number head, every row and the per-shot row are atoms of their own, and the head is
> the list's head (printGroup), so the PpPager repeats it when the rows run onto the next page.
> • rowsAll: give the whole list when `rows` is a slice; "firings per shot" counts from all of it.
> • ANY LENGTH (D7): the cell narrows to fit the shots (shot numbers thin as it does), down to a
> minimum; past that the barcode WRAPS into bands of shots (1–30, 31–50), each with its own head and
> per-shot row. It never clips.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `bool conditionsInteractive`, `var rows`, `var rowsAll`, `var readout`, `var source`, `bool showMarginNotes`, `bool showValues`, `bool foldValues`, `string printGroup`, `bool atom` … (+33 more)
- **Signals:** `conditionActivated(string id)`, `conditionHovered(string id)`, `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Functions:** `rowOf(x)`, `valueHtml(r)`
- **Uses:** `PpDiagRoles`, `PpPageKey`, `PpPulseWash`, `PpShotStrip`
- **Used by (3):** `PpDiagAnalyse`, `PpDiagPrint`, `PpDiagReplay`
- **Exercised by tests/tools:** `typeset_gallery`

### PpBoxStrip
`src/Gui/diagnostics/PpBoxStrip.qml` · 178 lines · root `Item` · no direct `Theme.*` refs

> BOXSTRIP — "Spread": the session as a whole, as a narrow box plot standing at the right of the run
> (its marginal summary: the run is the time series, the box is where the session sits). Q1–Q3 as an
> ink2-outlined box with a faint fill, the median as a 1.5 px ink rule, whiskers to the furthest shots
> within 1.5 × IQR with short caps; a shot beyond them is a PpMark (fired / clean), clickable and
> hover-linked. The picked shot is a ringed PpMark on the box's centre line at its value, and the
> hovered shot (the shared brush) lights there the same way, so "this shot" reads against "the session".
> At rest the median's value stands just right of the box (ink); EXAMINED (the pointer anywhere over the
> column, or `examined` for a render) the box shows its full reading (PpBoxReading): whisker ends,
> quartiles, median, the shot and outlier counts. The hover is a passive HoverHandler, so the outlier
> marks keep their clicks and the run's shot-hover brush is untouched. Print / not interactive: rest only.
> Replaced PpSpreadStrip (a beeswarm + the norm's density curve) on 12 Oct 2026: the corridor bands
> drawn by PpZonePlot already carry the norm's judgement.
> The box is drawn by PpBoxPlot (typeset/), THE box plot, and its numbers are the model's: spread.box, from
> the one C++ box implementation (typeset/box_stats.h) — edges as axis fractions for scale.y(f), the
> outliers as shot ids, the examined reading's labels at the axis's precision. Until 18 Oct 2026 this file
> worked the quartiles and whiskers out again from the dots.
> …

- **Properties:** `readonly int hoveredShot`, `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `var spread`, `var scale`, `string title`, `string unit`, `int decimals`, `bool examined`, `real rightRoom`, `real gutterX`, `var gutterYs` … (+22 more)
- **Signals:** `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Functions:** `num(v)`, `isOutlier(id)`, `dotOf(id)`, `py(f)`
- **Uses:** `PpBoxPlot`, `PpDiagRoles`, `PpZonePlot`
- **Used by (1):** `PpFigureRow`
- **Exercised by tests/tools:** `typeset_gallery`

### PpCauseGraph
`src/Gui/diagnostics/PpCauseGraph.qml` · 418 lines · root `Item` · no direct `Theme.*` refs

> CAUSEGRAPH — C's causes / effects as a layered graph: every path merged into one diagram, columns by
> distance from the condition, links drawn by the model's FIVE grades (round 4, D6, ported from
> PpChainLink): moved together = solid, heaviest, ink, arrow; conditionally dependent = solid, ink2,
> arrow; coherent = dashed, no arrow; present together = dotted, no arrow; unanchored = dotted at half
> opacity, no arrow. An arrowhead ONLY where the evidence supports a direction. The likely driver's path
> is drawn at full strength, the others recede. A screened root asks for its screen (screenRequested),
> never opens a page. Below its minimum (a column < roles.flows.dagCol
> em, or below medium) the diagram CHANGES FORM into an indented outline rather than shrinking.
> At wide its key and notes sit in a right margin; otherwise they follow it.
> which: "causes" | "effects". Clicking a node → conditionActivated(id); hovering brushes it.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `readonly bool isPrint`, `bool interactive`, `var detail`, `string which`, `bool showKey`, `bool atom`, `readonly var keyKinds`, `readonly var gradesUsed`, `readonly bool causes`, `readonly bool margin`, `readonly real marginW` … (+6 more)
- **Signals:** `conditionActivated(string id)`, `pathActivated(string which, int index)`, `screenRequested(string id)`
- **Functions:** `buildDag(ps, isCauses)`, `buildTree(ps, isCauses)`, `nodeKind(n)`, `strokeStyleOf(st, fade)`, `strokePolyline(ctx, pts, st, fade, wMul)`, `strokeLink(ctx, p, st, fade)`, `arrowHead(ctx, x, y, st, fade, dx, dy)`
- **Local components:** `NodeMark`
- **Uses:** `PpDiagRoles`, `PpMark`, `PpPageKey`
- **Used by (2):** `PpDiagAnalyse`, `PpDiagRoles`
- **Exercised by tests/tools:** `typeset_gallery`

### PpDetailBar
`src/Gui/diagnostics/PpDetailBar.qml` · 63 lines · root `Item` · no direct `Theme.*` refs

> DETAILBAR — Detail's chrome: "‹ Back" (never goes), the breadcrumb (from compact up), and the focus
> toggle on the right (its long form from compact up). Screen only. Wire backActivated → body._closeDetail(),
> focusActivated → body._declareFocus(id, !focused). Round 4: `interactive` (false: drawn, not pressed;
> the focus toggle then reads as a plain statement and Back stays — it never goes).

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `string crumb`, `bool focused`, `string backLabel`, `readonly var keyKinds`
- **Signals:** `backActivated()`, `focusActivated()`
- **Uses:** `PpDiagRoles`
- **Used by (1):** `PpDiagAnalyse`
- **Exercised by tests/tools:** `typeset_gallery`

### PpDiagAnalyse
`src/Gui/diagnostics/PpDiagAnalyse.qml` · 757 lines · root `Item` · no direct `Theme.*` refs

> ANALYSE — the read. The ledger leads (A): every view opens on the table that answers its question in
> exact figures; B's sentences carry the "why" (standfirsts, the direction line under each pattern, the
> feature head, the causal headlines); C's figures put the answer among everything (the dot plot of all
> conditions, the barcode, the detail's figure row). One PpBrush: hover anything and it is shown once in
> every block; a click goes somewhere real through `body`. Each view ends its lead table with at most
> three SHARPER QUESTIONS, computed from the data, answered in place (co-occurrence, never cause).
> Round 4 (the old panel's states and features, INVENTORY §1-2):
> • tense: an empty This shot says why in the old words (F19); live with nothing picked it reads the
> newest swing (D3); a quiet shot is said in one calm line and the read is never gated (D2, F21);
> zero fired is one line, not an empty table (S38).
> • Session: Cold says the n is too small + expectations (F41-42); the filter is body.cardFilter (D8)
> and an emptying filter explains itself (F35); focus on the rows (F33); evidence line (F30).
> • Watching: empty, the old sentence (F45).
> • Detail: measure tabs (F49), "Run the screen" (F64 → body.screenRequested), Esc closes (F47). The
> honesty mark (F56), the norm's source (F51) and the hover readout (F53) are the lib's (PpFeatureHead,
> PpFigureRow) from round 4.
> …

- **Properties:** `readonly string register`, `string view`, `var readout`, `var cards`, `var source`, `var header`, `var detail`, `string medium`, `string printOrientation`, `int printPage`, `readonly int printPageCount`, `var body`, `string demoState`, `string surface` … (+26 more)
- **Functions:** `requestScreen(id)`, `openCondition(id)`, `pickView(key)`, `pickShot(dir, id)`, `setFilter(k)`, `stepThrough(ids)`, `firePulse()`, `q(card, id, only)`, `cleanQ(card)`, `togetherQ(card, only)` (+1)
- **Local components:** `ViewFlick`, `Crosshead`, `Asks`, `ReviewFoot`
- **Uses:** `PpBarcode`, `PpBrush`, `PpCauseGraph`, `PpDetailBar`, `PpDiagReplayCold`, `PpDiagReplayState`, `PpDiagRoles`, `PpDotPlot`, `PpFeatureHead`, `PpFigureRow`, `PpInlinePicker`, `PpKeyFigures`, `PpLedgerTable`, `PpNotes`, `PpPageKey`, `PpPathChains` (+6)
- **Used by (1):** `PpSessionDiagnosticsBody`

### PpDiagPrint
`src/Gui/diagnostics/PpDiagPrint.qml` · 292 lines · root `Item` · ⚠ **no app file instantiates this** · no direct `Theme.*` refs

> PRINT — the report register (B's editorial voice). The same blocks as Replay and Analyse, laid out as a
> document on A4 and cut into pages by the library PpPager (repeating list heads, no stranded pages):
> masthead · 1 Session patterns (numbered entries, marginal sparklines, answers as footnotes)
> · 2 This shot (standfirst, the fired table, the clean index, what could not be measured)
> · 3 Watching (the barcode) · 4 the open pattern as a feature, always from a fresh page.
> Nothing here is clickable: links are cross-references ("The feature on p. 5 reads it in full").
> Round 4: on the library's PpPager and PpPathChains (PrintPager / PrintPaths are gone); the barcode prints
> row by row with its head repeated; PpKeyFigures excludes by key; shot labels are ordinals.
> "Every pattern" would repeat the feature block once per card, each from a fresh page (see NOTES3_print).

- **Properties:** `string view`, `var readout`, `var cards`, `var source`, `var header`, `var detail`, `string medium`, `string printOrientation`, `int printPage`, `readonly int printPageCount`, `var body`, `string demoState`, `string surface`, `readonly var conds` … (+18 more)
- **Functions:** `listOf(xs)`, `groupOf(card)`, `answersFor(card)`, `answerHtml(card)`
- **Uses:** `PpBarcode`, `PpBrush`, `PpDiagRoles`, `PpEntryFlow`, `PpFeatureHead`, `PpFigureRow`, `PpIndexList`, `PpKeyFigures`, `PpLedgerTable`, `PpMasthead`, `PpNotes`, `PpPager`, `PpPathChains`, `PpQuestions`, `PpSectionHead`, `PpStandfirst` (+1)
- **Exercised by tests/tools:** `diag_print_pdf`

### PpDiagReplay
`src/Gui/diagnostics/PpDiagReplay.qml` · 556 lines · root `Item` · no direct `Theme.*` refs

> REPLAY — the glance register (Replay, Capture, and the second-screen cast). It sits beside the video
> for the whole replay, so it gives the answer in one look and then stays still.
> This shot  C's PpDotPlot of the FIRED conditions with their values; one quiet line for the rest;
> the shot ruler (how many went outside on each shot: the view's one shot axis).
> Live with nothing picked it reads the NEWEST swing (D3); a quiet shot says so (D2);
> with nothing to read it says why, in the old panel's words for the tense (F19).
> Session    one PpSparkRow per pattern (or the small multiples when all fit the height); Cold says the
> n is too small and shows the expectations (F41-F42); the coverage line at the foot (F39).
> Watching   C's PpBarcode, emerging first; empty, the old sentence (F45).
> Detail     never drawn here. A condition clicked in the glance opens its detail, and the body reads an
> open detail in PpDiagAnalyse with the stage still in Replay, holding this register hidden
> underneath; Back returns to it as it was. Only the cast shows this register under an open
> detail (D9): it stays on the reader's tab, and a tab press closes the detail first.
> A click on a condition is the glance's one door into depth (never from the cast). A click on a shot
> moves the carousel.
> Cast (surface "cast", always this register, D4): a running head names the shot; the WHOLE surface is
> …

- **Properties:** `readonly string register`, `string view`, `var readout`, `var cards`, `var source`, `var header`, `var detail`, `var body`, `string demoState`, `string surface`, `string medium`, `string printOrientation`, `int printPage`, `readonly int printPageCount` … (+28 more)
- **Functions:** `n(x)`, `openCondition(id)`, `pickShot(dir, id)`, `pickView(key)`, `firePulse()`, `applyDemo()`
- **Local components:** `Head`, `ReviewFoot`, `ViewFlick`
- **Uses:** `PpBarcode`, `PpBrush`, `PpDiagReplayCold`, `PpDiagReplaySessionRows`, `PpDiagReplayState`, `PpDiagRoles`, `PpDotPlot`, `PpInlinePicker`, `PpLedgerTable`, `PpShotRuler`, `PpSmallMultiples`, `PpTabs`, `PpTrustInlineFor`, `PpTrustSummaryFold`, `TrustFocus`
- **Used by (1):** `PpSessionDiagnosticsBody`

### PpDiagReplayCold
`src/Gui/diagnostics/PpDiagReplayCold.qml` · 52 lines · root `Column`

> REPLAYCOLD — the Session view while the model is Cold (S2-S4, F41-F42), for both registers.
> "Say the n is too small": the model's coldLine as the head ("Too few swings to call a pattern.") and its
> countLine under it. Then the lib's PpExpectations (dashed: the one thing here that is not evidence from
> this session), drawn ONLY when the athlete has a fault profile — an empty heading would read as the
> model having looked and found none.

- **Properties:** `QtObject roles`, `var header`, `var expectations`, `readonly var list`
- **Uses:** `PpExpectations`
- **Used by (2):** `PpDiagAnalyse`, `PpDiagReplay`

### PpDiagReplaySessionRows
`src/Gui/diagnostics/PpDiagReplaySessionRows.qml` · 75 lines · root `Item` · no direct `Theme.*` refs

> REPLAYSESSIONROWS — Replay's Session figure: one PpSparkRow per pattern (name · shot strip · this shot's
> value · sparkline), under ONE quiet column head that names what each column is. The rows flow into as
> many columns as fit at 40 em each, at most two, filled down then across so the rank order still reads
> top to bottom. The head is the lib's PpSparkHead, which shares PpSparkRow's geometry (roles.sparkGeo),
> so it follows any session length (D7).

- **Properties:** `QtObject roles`, `var brush`, `var cards`, `var readout`, `bool interactive`, `bool conditionsInteractive`, `int maxCols`, `readonly var list`, `readonly real g`, `readonly int cols`, `readonly real cw`, `readonly int per`, `readonly var slices`, `readonly int nShots`
- **Signals:** `conditionActivated(string id)`, `shotActivated(string swingDir, int shotId)`
- **Uses:** `PpSparkHead`, `PpSparkRow`
- **Used by (1):** `PpDiagReplay`

### PpDiagReplayState
`src/Gui/diagnostics/PpDiagReplayState.qml` · 106 lines · root `QtObject` · no direct `Theme.*` refs

> REPLAYSTATE — what the session IS right now, read once for both registers (PpDiagReplay and PpDiagAnalyse
> each make one). The old body branched on the model's stage, the review badge,
> the readout and `interactive`; the round-3 registers branched only on the readout. This restores the
> old panel's states (INVENTORY §1) without the registers asking the stage separately.
> tense        "none" (no source) · "live" · "closing" · "review"
> shot         what This shot reads: the picked readout, or (D3) in a live session with nothing picked
> the NEWEST swing, built from source.afterShotDelta + source.thisShot (a partial readout:
> fired conditions only, the clean / not-measured counts as numbers; `partial: true`).
> empty*       the old panel's words for a This shot with nothing to read (F19), by tense.
> quiet        bandwidth cadence held the newest swing back (S8, D2).
> label        "shot N of M" from the ORDINAL (shotIndex + 1), never the shot id (D7).

- **Properties:** `var source`, `var header`, `var readout`, `var cards`, `bool interactive`, `readonly string stage`, `readonly bool reviewing`, `readonly bool closing`, `readonly bool cold`, `readonly bool forming`, `readonly string tense`, `readonly bool live`, `readonly int sessionShots`, `readonly var delta` … (+16 more)
- **Functions:** `missCandidates()`, `focusLine(name, state)`, `byId(list, id)`, `buildNewest()`
- **Used by (2):** `PpDiagAnalyse`, `PpDiagReplay`

### PpDiagRoles
`src/Gui/diagnostics/PpDiagRoles.qml` · 229 lines · root `PpTypeset` · no direct `Theme.*` refs

> PPDIAGROLES — the Session diagnostics vocabulary on top of the generic typeset helper (design system
> §15.3). Everything a typeset block draws with comes from PpTypeset; what is particular to the
> diagnostics model lives here: its extra flow minimums and element tiers, the ledger's column tier
> tables (A's fired / session / watching / path), the Replay session row's geometry, and the text
> helpers that turn the model's strings and shapes (valueText, recurrence, corridorText, trend, cards,
> the readout) into figures and sentences. A diagnostics register makes ONE and passes it as `roles`:
> PpDiagRoles { id: ro; medium: root.medium }
> Never give it the id `roles` (see PpTypeset). Generic blocks accept it too: it IS a PpTypeset.

- **Functions:** `sparkGeo(w, n, spark)`, `fig(v)`, `unitOf(v)`, `shortUnit(s)`, `recParts(rec)`, `frac(rec)`, `fracOf(rec)`, `rate(rec)`, `shortCount(rec)`, `corridorShort(t)` (+13)
- **Uses:** `PpCauseGraph`, `PpTypeset`
- **Used by (24):** `PpBarcode`, `PpBoxStrip`, `PpCauseGraph`, `PpDetailBar`, `PpDiagAnalyse`, `PpDiagPrint`, `PpDiagReplay`, `PpDotPlot`, `PpEntryFlow`, `PpExpectations` (+14)
- **Exercised by tests/tools:** `tst_session_diagnostics`, `typeset_figures`, `typeset_gallery`

### PpDotPlot
`src/Gui/diagnostics/PpDotPlot.qml` · 257 lines · root `Item` · no direct `Theme.*` refs

> DOTPLOT — C's This-shot figure: every condition on ONE common, corridor-normalised scale. In those
> steps a reading sits at 0 in the middle of its corridor, the pass edge is at 2 (where a condition
> fires) and the fault line at 3; x = strengthExcess for this shot. Fired conditions are warn marks on a
> warn stem from the pass edge; clean ones are clean marks inside the wash; "‹" = out on the other side,
> so clean here. Rows sort by distance. Laid out in as many blocks as fit: rich (session tier · name ·
> plot · value) from roles.flows.dotRich em a block, tight (names wrap, values never) from dotTight.
> filter "fired" draws only the fired rows (Replay); the scale stays the same.
> Round 4:
> • showTier: the tier column FOLDS before the plot goes tight — between dotRich − the tier column and
> dotRich the rows stay rich and the tier word joins the name ("pattern · Over the top").
> • rowsFitting(h[, list]): how many rows (of allRows, or `list`) fit a height as ONE block — the
> plot's own arithmetic, so a register never re-implements it.
> • PRINT: the column heads are ONE atom across every block (and the list's head: printGroup); every
> row is an atom, so print cuts between rows even with several blocks side by side.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `var readout`, `string filter`, `var rowsOverride`, `int maxBlocks`, `real availableHeight`, `bool showHead`, `bool showTier`, `string printGroup`, `readonly var keyKinds`, `readonly var allRows` … (+13 more)
- **Signals:** `conditionActivated(string id)`, `conditionHovered(string id)`
- **Functions:** `wrapLines(t, w)`, `geoFor(bw, rich, tierCol)`, `label(r, g)`, `rowHFor(r, g)`, `rowH(r)`, `rowsFitting(h, list)`, `xs(v)`
- **Uses:** `PpDiagRoles`, `PpMark`, `PpPulseWash`
- **Used by (2):** `PpDiagAnalyse`, `PpDiagReplay`
- **Exercised by tests/tools:** `typeset_gallery`

### PpEntryFlow
`src/Gui/diagnostics/PpEntryFlow.qml` · 186 lines · root `Item` · no direct `Theme.*` refs

> ENTRYFLOW — B's run-in head entries in newspaper columns with column rules.
> kind "fired":   items = fired conditions; each entry "Name  22.1% hand rise. The corridor … ; seven more after
> this one." Columns ≥ roles.flows.firedEntry em, balanced by estimated height.
> kind "session": items = cards; numbered entries (margin numeral), the sentence, a meta line, and a
> PpSparkline in the margin when a column is ≥ flows.entryMargin em, else on the head line.
> Clicking an entry's name → conditionActivated(id); hovering brushes the condition.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `readonly bool isPrint`, `bool interactive`, `string kind`, `var items`, `string label`, `var cards`, `var readout`, `string xrefId`, `int xrefPage`, `bool showFocus`, `int cols` … (+10 more)
- **Signals:** `conditionActivated(string id)`, `conditionHovered(string id)`, `focusActivated(string id)`, `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Functions:** `plain(h)`, `linesFor(len, px, w)`, `balance(list, k, est)`, `entryHtml(c)`, `metaHtml(c)`
- **Uses:** `PpDiagRoles`, `PpSparkline`
- **Used by (1):** `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_gallery`

### PpExpectations
`src/Gui/diagnostics/PpExpectations.qml` · 91 lines · root `Column` · no direct `Theme.*` refs

> EXPECTATIONS — the Cold session's prior (round 4, F41/F42): what this golfer's fault profile says to
> look for, before the session has enough swings to call a pattern. Each expectation is a dashed row:
> its name, then the profile's sentence. Headed "Usually yours · expectations to test, not findings" and
> closed by the caption that it biases ranking and presentation only. NOT drawn at all when the list is
> empty (the restraint rule). The cold line ("Too few swings to call a pattern.") is the register's head.
> items: source.expectations [{ name, text }].

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `var items`, `string title`, `string sub`, `string caption`, `bool atom`, `readonly var keyKinds`, `readonly bool hasItems`
- **Uses:** `PpDiagRoles`
- **Used by (1):** `PpDiagReplayCold`
- **Exercised by tests/tools:** `typeset_gallery`

### PpFeatureHead
`src/Gui/diagnostics/PpFeatureHead.qml` · 172 lines · root `Item` · no direct `Theme.*` refs

> FEATUREHEAD — B's detail opening: kicker (No. 1 · P4 · measure), the condition's name as a feature
> head, the standfirst (direction + consequence), the pull figures (this shot in warn when it fired,
> the session median) and the fact file (corridor, recurrence, bookends, measures, reads, norm).
> One row from roles.flows.openingRow em; standfirst then pull | facts from 36 em; all stacked below.
> `longForm` false gives the short standfirst; the rest of the consequence then belongs to PpPathChains("effects").
> Round 4: showPull / showFacts (false when PpKeyFigures carries the figures, so they are not said twice);
> the HONESTY MARK of a non-card condition (detail.header.mark, F56: "ghost · authored as asserted, no
> measure") under the kicker; the norm's citation INLINE (no tooltip): "the source" opens it in place on
> screen, print prints it.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `var detail`, `var readout`, `var source`, `bool longForm`, `bool showPull`, `bool showFacts`, `bool citationOpen`, `bool interactive`, `bool atom`, `bool keepNext`, `readonly var keyKinds` … (+7 more)
- **Signals:** `shotActivated(string swingDir, int shotId)`
- **Uses:** `PpDiagRoles`
- **Used by (2):** `PpDiagAnalyse`, `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_gallery`

### PpFigureRow
`src/Gui/diagnostics/PpFigureRow.qml` · 341 lines · root `Item` · no direct `Theme.*` refs

> FIGUREROW — C's chart-led detail figure: PpRunChart + PpBoxStrip | PpHistoryStrip on ONE value axis,
> with the axis labels on the left, the zone labels once at the right of the last panel, and a caption
> under each panel. The run and its box plot (the session's marginal summary, immediately right of the
> run) always stand together; the history sits beside them while the run keeps roles.flows.runPanel em,
> else under them at their full width. At wide the notes go to a right margin; below that they follow
> the figure in prose columns (≥ roles.flows.prose em). Same grammar at every size.
> (12 Oct 2026: the box plot replaced PpSpreadStrip, a beeswarm with the norm's curve, at the left.)
> PUBLIC API (stable; internals belong to the fault-forward figure work):
> detail     the open condition (header.spread, header.ticks, history)
> readout    for the picked shot's label
> showNotes  draw the notes (margin or below); false when the register sets them elsewhere
> roles, brush, medium, interactive
> signals shotActivated(swingDir, shotId), shotHovered(shotId), sessionActivated(sessionDir)

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `var detail`, `var readout`, `bool showNotes`, `bool showTravel`, `bool atom`, `bool boxExamined`, `int historyExamined`, `readonly var keyKinds`, `readonly string hoverReadout`, `readonly var runGate` … (+32 more)
- **Signals:** `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`, `sessionActivated(string sessionDir)`
- **Functions:** `bandText(r)`, `gutterXOf(loader)`
- **Local components:** `ValueRow`, `FigNotes`
- **Uses:** `PpBoxStrip`, `PpDiagRoles`, `PpFigureScale`, `PpHistoryStrip`, `PpRunChart`, `PpTrustInlineFor`, `TrustFocus`
- **Used by (2):** `PpDiagAnalyse`, `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_figures`, `typeset_gallery`

### PpHistoryStrip
`src/Gui/diagnostics/PpHistoryStrip.qml` · 191 lines · root `Item` · no direct `Theme.*` refs

> HISTORYSTRIP — "Across sessions": per session, its shots as clean marks and a small box plot in the
> grammar of the run's PpBoxStrip (Q1–Q3 box, median rule, whiskers to the furthest shots within
> 1.5 × IQR), its MEDIAN set as a small number beside the box (this session's in ink, the rest ink3;
> only this session's when the columns are too narrow for every label); sessions in date order on the
> same value axis as the run. A session with no reading of this measure is a centred "-", never a blank.
> EXAMINED (the pointer over a column, or `examinedColumn` for a render) that session's box shows its
> full reading (PpBoxReading: whisker ends, quartiles, median, shot and outlier counts) over the strip,
> and the other columns' resting median labels that it would cross step aside. The hover is a passive
> HoverHandler per column: the click that opens a session still works. Print / not interactive: rest only.
> PUBLIC API (stable; internals belong to the fault-forward figure work):
> history    detail.history  (columns[{label, values[{f, stack}], fQ1, fQ3, fMedian, medianText, current,
> box{n, fLo, fQ1, fMed, fQ3, fHi, outlierCount, texts}}]) — each column's box drawn by PpBoxPlot
> (typeset/), THE box plot, from the model's numbers (typeset/box_stats.h); until 18 Oct 2026 the
> whiskers were worked out again here
> scale      a PpFigureScale shared with the other panels of its PpFigureRow (required)
> unit       a symbol unit (°) to set on the median labels; "" for a worded unit
> …

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `var history`, `var scale`, `string title`, `string unit`, `int decimals`, `int examinedColumn`, `real rightRoom`, `real gutterX`, `var gutterYs`, `int hoverColumn` … (+14 more)
- **Signals:** `sessionActivated(string sessionDir)`
- **Functions:** `maxStackOf(c)`, `pitchOf(c)`, `marksLeft(c)`, `medianLabel(c)`, `restX(i)`
- **Uses:** `PpBoxPlot`, `PpDiagRoles`, `PpMark`, `PpZonePlot`
- **Used by (1):** `PpFigureRow`
- **Exercised by tests/tools:** `typeset_gallery`

### PpIndexList
`src/Gui/diagnostics/PpIndexList.qml` · 155 lines · root `Item` · no direct `Theme.*` refs

> INDEXLIST — A's index of the clean conditions: name, leader dots, value and unit on a right edge,
> in as many columns as fit (roles.flows.index em each, column-major so it reads down). Ink marks a
> session pattern clean on this shot, ° marks a watched condition. `grouped` adds B's group heads
> (patterns clean here / watched / clean all session), alphabetical within each group.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `var conditions`, `bool grouped`, `string title`, `bool showHead`, `string legend`, `readonly var keyKinds`, `readonly var groupsDef`, `readonly var items`, `readonly real cg`, `readonly int cols` … (+2 more)
- **Signals:** `conditionActivated(string id)`, `conditionHovered(string id)`
- **Uses:** `PpDiagRoles`
- **Used by (1):** `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_gallery`

### PpKeyFigures
`src/Gui/diagnostics/PpKeyFigures.qml` · 96 lines · root `Item` · no direct `Theme.*` refs

> KEYFIGURES — A's summary row of key figures under a heavy rule: label above, figure below, hairline
> dividers. It wraps n → n/2 → n/4 per row (never a ragged 3-of-8), each figure ≥ roles.flows.keyFigure em.
> Pass `items` [{ label, figure, warn, word }] or let it build them from `detail` (+ readout).

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `var detail`, `var readout`, `var items`, `var exclude`, `bool showFoot`, `bool atom`, `readonly var keyKinds`, `readonly var c`, `readonly var sp`, `readonly int shotNo`, `readonly var defaultItems` … (+6 more)
- **Uses:** `PpDiagRoles`
- **Used by (2):** `PpDiagAnalyse`, `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_gallery`

### PpLedgerTable
`src/Gui/diagnostics/PpLedgerTable.qml` · 556 lines · root `Item` · no direct `Theme.*` refs

> LEDGERTABLE — A's fold-don't-drop table. Columns come from a tier spec (roles.tiers.<kind>): a column
> whose tier fits the width given is laid on the grid; the rest FOLD, in `fo` order and with their
> label, into the row's second line. Nothing is dropped and nothing is ellipsised. While the strip is a
> column the picked shot is a shaded column down the table; folded, each strip shades its own cell.
> Built-in kinds read the live shapes directly:
> "fired"    rows = readout.conditions (any subset)    → line 2 = folded columns
> "session"  rows = cards                               → line 2 = B's direction sentence (+ xref in print)
> "watching" rows = source.watching (needs readout)     → values/strips from readout
> "path"     rows = detail.causes or detail.effects     → No. · typeset chain · links · Open
> or pass `spec` + `getter(row, key, foldedKeys) → { t, c, f, rich, ticks, gate }` for a custom table.
> A cell whose reading a gate or rule refused (`gate` = the model's gateNote { subject, gate, words,
> swingDir }) prints the gate's words in place of "-", quiet; a record's words are a link (" ›") that
> opens the reading in the Trust panel, else in place under the row (trust stage 6 session 2). A rule
> has no reading to open: words only. Print: words only.

- **Properties:** `readonly string rowName`, `QtObject roles`, `var brush`, `string medium`, `readonly bool isPrint`, `bool interactive`, `string kind`, `var rows`, `var spec`, `var getter`, `var readout`, `var source`, `var detail`, `string title` … (+32 more)
- **Signals:** `conditionActivated(string id)`, `conditionHovered(string id)`, `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`, `pathActivated(int index)`, `toolActivated(string key)`, `focusActivated(string id, bool on)`
- **Functions:** `cellName(key)`, `colW(c)`, `fixedCol(c)`, `headText(c)`, `rowId(r)`, `gateCell(g)`, `openGate(rid, g)`, `gateHtml(g)`, `cond(id)`, `bookend(id)` (+6)
- **Uses:** `PpDiagRoles`, `PpPulseWash`, `PpShotStrip`, `PpTrustInlineFor`, `TrustFocus`
- **Used by (3):** `PpDiagAnalyse`, `PpDiagPrint`, `PpDiagReplay`
- **Exercised by tests/tools:** `typeset_gallery`

### PpMasthead
`src/Gui/diagnostics/PpMasthead.qml` · 42 lines · root `PpStandfirst`

> MASTHEAD — B's report masthead, built entirely from the data: "Fifteen shots, eight habits" and a
> dek that names the club, the date, the shot read and how many of its conditions fired. Print opens
> with it; on screen it can head a cast or a report preview.

- **Properties:** `var readout`, `var cards`, `var source`, `readonly var meta`, `readonly var conds`, `readonly int nFired`, `readonly int nPats`, `readonly int shotCount`
- **Uses:** `PpStandfirst`
- **Used by (1):** `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_gallery`

### PpPathChains
`src/Gui/diagnostics/PpPathChains.qml` · 259 lines · root `Item` · no direct `Theme.*` refs

> PATHCHAINS — B's typeset causal paths: a crosshead ("What might have caused this" / "What this leads
> to"), the headline as a lead sentence, then numbered chains "A ⇒ B ∼ C" with footnote marks (*, †, ‡)
> and a footnote that states the link grades and marks ONCE. Names outside the corridor on this shot
> are in warn. Path 1 carries its evidence quote from compact up (else it sits behind Open).
> which: "causes" | "effects" | "both" (both = side by side from roles.flows.proseTwo em, else stacked).
> Round 4:
> • LINK GRADES (D6): one glyph per model grade, keyed once — ⇒ moved together, → conditionally
> dependent (the only two that carry a direction), ╌ coherent, ∼ present together, ⋯ unanchored.
> • OPEN IN PLACE (F58): "Open" expands the path under itself — each node with its reading and mark,
> each link with its grade word and the model's note; "Close" folds it. pathActivated still fires.
> • PRINT: stacked (or print), every section is a list — its crosshead + lead one atom (the list's head,
> printGroup "paths-<which>"), each path an atom, the key kept with the last path — so a long section
> splits between paths and repeats its head. (This replaces reg/PrintPaths.)

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `readonly bool isPrint`, `bool interactive`, `var detail`, `var readout`, `string which`, `string rest`, `bool rule`, `var openPaths`, `readonly bool atom`, `readonly var keyKinds`, `readonly int shotNo` … (+4 more)
- **Signals:** `pathActivated(string which, int index)`, `conditionActivated(string id)`
- **Functions:** `isOpen(k, i)`, `togglePath(k, i)`, `marksFor(paths)`, `glyphFor(marks, m)`, `linkGlyph(g)`, `linkHtml(l)`, `chainHtml(p, marks)`, `keyHtml(paths, marks, k)`, `nodeLine(n)`, `linkLine(l)`
- **Uses:** `PpDiagRoles`
- **Used by (2):** `PpDiagAnalyse`, `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_gallery`

### PpQuestions
`src/Gui/diagnostics/PpQuestions.qml` · 155 lines · root `Column` · no direct `Theme.*` refs

> QUESTIONS — Analyse only: at most three sharper questions a view can end with, each computed from the
> data on the panel and answered IN PLACE through the brush (light the shots or conditions that answer
> it; everything else recedes). Shown only when the answer exists. Co-occurrence, never cause.
> Build the list with forCondition(card, readout, source) (or your own [{ id, text, answer, shots |
> conditions, step }]); asking one applies it to the brush and emits asked(q); asking it again clears.
> `step` (shot ids) lets the register offer "step through them" via body.shotRequested.
> Print: the answers become footnotes (medium "print" draws `answer` sentences, no affordances).
> Round 4: forCondition(card, readout, source, opts) — opts.only: ids that "what fired with it" may name
> (the rows on screen); opts.subject: true phrases each question and answer about the card by name
> ("Over the top: which shots were clean?"). Every question also carries `parts`, its answer as DATA:
> clean { shots, firedCount } · together { list: [{ id, name, n, of }] } · worse { shots, trendText }
> · bookends { worst, best }  — so print can word the answers without recomputing them.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `readonly bool isPrint`, `var questions`, `string label`, `bool applyToBrush`, `bool interactive`, `bool atom`, `readonly var keyKinds`
- **Signals:** `asked(var q)`, `stepRequested(var shotIds)`
- **Functions:** `cleanShots(card)`, `firedShots(ticks)`, `firedTogether(readout, id, max)`, `thirds(card)`, `list(xs)`, `forCondition(card, readout, source, opts)`, `ask(q)`
- **Uses:** `PpDiagRoles`
- **Used by (2):** `PpDiagAnalyse`, `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_gallery`

### PpRunChart
`src/Gui/diagnostics/PpRunChart.qml` · 242 lines · root `Item` · 1 hard-coded colour(s)

> RUNCHART — "Shot by shot": one PpMark per shot in the order struck, the median of the last five as a
> line, a stub on the foot for a shot that could not be measured. Shot numbers thin with width: every
> shot from 22 px a shot, else 1 · 5 · 10 · 15 plus the picked and the hovered shot.
> PUBLIC API (stable; internals belong to the fault-forward figure work):
> spread     card.spread / detail.header.spread  (run[], median[])
> ticks      the card's 15 ticks (for the not-measured stubs)
> scale      a PpFigureScale shared with the other panels of its PpFigureRow (required)
> title, roles, brush, medium, interactive
> signals shotActivated(swingDir, shotId), shotHovered(shotId)

- **Properties:** `readonly int hoveredShot`, `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `var spread`, `var ticks`, `var scale`, `string title`, `string unit`, `bool showDistance`, `real leftRoom`, `real rightRoom`, `readonly real labelMaxW` … (+8 more)
- **Signals:** `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Functions:** `fitX(raw, w)`, `fmtD(x)`
- **Uses:** `PpDiagRoles`, `PpMark`, `PpZonePlot`
- **Used by (1):** `PpFigureRow`
- **Exercised by tests/tools:** `typeset_gallery`

### PpSessionDiagnosticsBody
`src/Gui/diagnostics/PpSessionDiagnosticsBody.qml` · 400 lines · root `Item`

> THE SESSION DIAGNOSTICS PANEL'S BODY: its data and view state, the frame on surfaces that have no
> stage card round them, and the TYPESET REGISTER that draws everything else (design system §15).
> IT TAKES A `source` AND OWNS NOTHING. Every string, count and tick state is read off one object
> shaped like SessionDiagnosticsModel; the wiring that produces one (the session directory, the shot
> processor, review) lives next door in PpSessionDiagnosticsPanel. So this file still loads offscreen
> with a fixture, and there is one answer to "where does this number come from": `source`.
> THE REGISTER IS CHOSEN, NOT THE LAYOUT (§15.6). A register is how much the panel says:
> 1. forceRegister ("Replay" | "Analyse"), set by tests only;
> 2. surface "cast": ALWAYS PpDiagReplay (D4), whatever mode the main stage is in;
> 3. SessionMode: Analyse reads in depth (PpDiagAnalyse); Capture and Replay glance (PpDiagReplay),
> except that an open detail is always read in PpDiagAnalyse: a condition clicked in Replay opens
> in place, and Back returns to the glance, with the stage still in Replay.
> The register is handed `body: root` and reads and drives everything through this API: the view is
> `detailOpen ? "detail" : _tab`, and interactive, cardFilter and pulseCue come straight through. It
> lays itself out by the size class of the width it is given and NEVER scales its type: there is no
> fit factor here any more (house rule 2). The cast's viewing-distance zoom scales the whole surface
> …

- **Properties:** `var source`, `var readout`, `bool watchingExpanded`, `string cardFilter`, `bool interactive`, `string surface`, `string forceRegister`, `string demoState`, `string tab`, `readonly string view`, `readonly string detailConditionId`, `readonly var detail`, `readonly bool detailOpen`, `var pulseCue` … (+21 more)
- **Signals:** `screenRequested(string screenRef, string conditionId)`, `shotRequested(string swingDir, int shotId)`
- **Functions:** `pulseAfterShot()`
- **Uses:** `PpCardShell`, `PpDiagAnalyse`, `PpDiagReplay`, `PpMicro`, `SessionMode`
- **Used by (1):** `PpSessionDiagnosticsPanel`
- **Exercised by tests/tools:** `session_spread`, `tst_session_diagnostics`

### PpSessionDiagnosticsPanel
`src/Gui/diagnostics/PpSessionDiagnosticsPanel.qml` · 431 lines · root `Item` · no direct `Theme.*` refs

> The session diagnostics stage panel: what keeps happening this session, and what the
> model thinks is causing it. Per session and cumulative, and most of its design is the
> restraint that keeps it from ever saying more than the ledger supports.
> THIS FILE IS THE WIRING AND NOTHING ELSE. Everything the panel LOOKS like is
> PpSessionDiagnosticsBody, which takes a `source` and owns none of it. The split is the
> one PpLaunchMonitorPanel makes with PpLmGraphicsBody, and it buys the same thing twice
> over: the body can be loaded offscreen with a fixture, so the states nobody can produce
> on demand — Cold with an empty fault profile, the bandwidth-quiet strip, a
> not-assessable tick — are asserted rather than eyeballed; and there is exactly one place
> where this panel touches the app's singletons, which is here.
> THE MODEL IS INSTANTIATED, NOT REACHED FOR. SessionDiagnosticsModel owns a characteristic
> pack, a norm provider and a worker thread that re-runs detection on every shot, and this
> panel is OFF by default — the user turns it on in View. A golfer who never opens it
> should not be paying for any of that, which is why the model is not a context property in
> main.cpp. See the class comment on SessionDiagnosticsModel.
> CADENCE GATES SURFACING, NEVER THE ENGINE. ingestShot() below is wired straight to
> …

- **Properties:** `string title`, `alias framed`, `readonly string cardAside`, `alias source`, `alias interactive`, `alias surface`, `bool remembersNav`, `readonly string sessionDir`
- **Signals:** `screenRequested(string screenRef, string conditionId)`
- **Uses:** `PpSessionDiagnosticsBody`, `SessionMode`
- **Used by (3):** `PpSessionDiagnosticsWindow`, `ScreenSessionMode`, `ScreenWrist`

### PpShotRuler
`src/Gui/diagnostics/PpShotRuler.qml` · 141 lines · root `Item` · no direct `Theme.*` refs

> SHOTRULER — the session's one shot axis (round 4, from Replay's ReplayShotRuler and the PpBarcode's
> "firings per shot" row): every shot in the order struck, each with how many conditions went outside
> their corridor on it. It answers "was this a worse swing than the others?" in one look, and it is a
> brushable, clickable shot axis: the picked shot is the pick column (ink figure on the 7 % wash), a
> hovered shot the fainter one. A click puts that shot in the carousel. Counts are ink2/ink3 figures,
> never warn (they are counts, not readings outside a corridor); "-" where no condition was measurable.
> Counted from `conditions` (default readout.conditions: EVERY condition on the shot, so a ruler under a
> subset still counts the whole shot). ANY LENGTH (D7): given availableWidth the pitch narrows to the
> figures' width, then the ruler wraps into further lines; shot numbers thin first, counts never.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `var readout`, `var conditions`, `real pitch`, `real availableWidth`, `real labelW`, `bool showShots`, `bool showCounts`, `string shotsLabel`, `string countsLabel`, `bool atom` … (+17 more)
- **Signals:** `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Used by (1):** `PpDiagReplay`
- **Exercised by tests/tools:** `typeset_gallery`

### PpSmallMultiple
`src/Gui/diagnostics/PpSmallMultiple.qml` · 284 lines · root `Item` · 1 hard-coded colour(s)

> SMALLMULTIPLE — C's Session tile: one pattern's shots (any session length) on THE COMMON SCALE (C): every corridor cut
> by the same ladder, 0 in the middle of the corridor, 2 at the pass edge, 3 at the fault line, folded
> so the side the pattern fires on is up. Identically scaled tiles compare at a glance. Header: rank,
> name, median and this shot; foot: measure, recurrence · trend, firings after, bookends.
> PUBLIC API (stable; internals belong to the fault-forward figure work):
> card, readout, source       the live shapes
> panelWidth                  the width of the whole grid (tiers read it, not the tile's width)
> hdrH                        header height shared across a row (PpSmallMultiples sets it)
> roles, brush, medium, interactive
> signals conditionActivated(id), shotActivated(swingDir, shotId), shotHovered(shotId)

- **Properties:** `readonly string cardId`, `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `bool conditionsInteractive`, `var card`, `var readout`, `var source`, `real panelWidth`, `real hdrH`, `bool labelBand`, `readonly var keyKinds`, `readonly var c` … (+22 more)
- **Signals:** `conditionActivated(string id)`, `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Functions:** `zPoints(s)`, `zOf(pts, v)`
- **Uses:** `PpDiagRoles`, `PpMark`, `PpPulseWash`
- **Used by (1):** `PpSmallMultiples`

### PpSmallMultiples
`src/Gui/diagnostics/PpSmallMultiples.qml` · 97 lines · root `Column` · no direct `Theme.*` refs

> SMALLMULTIPLES — the grid of PpSmallMultiple tiles: columns = min(4, ⌊w / roles.flows.tile em⌋), rows
> are atoms (print cuts between rows), a row shares one header height so the charts align.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `bool conditionsInteractive`, `var cards`, `var readout`, `var source`, `int maxCols`, `bool commonTop`, `readonly var keyKinds`, `readonly real gx`, `readonly int cols`, `readonly real tileW` … (+2 more)
- **Signals:** `conditionActivated(string id)`, `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Functions:** `lines(t, w)`
- **Uses:** `PpDiagRoles`, `PpSmallMultiple`
- **Used by (1):** `PpDiagReplay`
- **Exercised by tests/tools:** `typeset_figures`, `typeset_gallery`

### PpSparkHead
`src/Gui/diagnostics/PpSparkHead.qml` · 51 lines · root `Item`

> SPARKHEAD — the ONE quiet column head over a run of SparkRows (round 4; Replay asked for it so its rows
> need not mirror PpSparkRow's arithmetic): "pattern · shots 1–N · shot n · run", each label over its column,
> from the same roles.sparkGeo() the rows use. Give it the rows' width and the session's shot count.
> The labels come from the data: N = shots, n = the picked shot's ORDINAL (roles.shotNo).

- **Properties:** `QtObject roles`, `var readout`, `int shots`, `bool showSparkline`, `string nameLabel`, `string runLabel`, `bool atom`, `bool keepNext`, `readonly var keyKinds`, `readonly var geo`
- **Uses:** `PpDiagRoles`
- **Used by (1):** `PpDiagReplaySessionRows`
- **Exercised by tests/tools:** `typeset_figures`, `typeset_gallery`

### PpSparkRow
`src/Gui/diagnostics/PpSparkRow.qml` · 103 lines · root `Item` · no direct `Theme.*` refs

> SPARKROW — Replay's session line: one pattern per row — name · PpShotStrip · this shot's value · PpSparkline.
> The name wraps (never ellipsised) and the row grows with it. The PpSparkline is tier 2 (from compact up);
> below that the strip carries the run alone. Clicking the name → conditionActivated(id) (Replay: switch to
> Analyse at that condition); the strip and sparkline brush and pick shots. Its geometry is
> roles.sparkGeo(width, shots), which PpSparkHead shares, so a column head always sits over its column.
> Any session length (D7): the strip column is roles.stripWidthFor(n); past that the marks narrow, then wrap.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `bool conditionsInteractive`, `var card`, `bool showSparkline`, `bool atom`, `readonly var keyKinds`, `readonly var c`, `readonly string cardId`, `readonly bool hot`, `readonly int shots`, `readonly var geo` … (+6 more)
- **Signals:** `conditionActivated(string id)`, `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Uses:** `PpDiagRoles`, `PpPulseWash`, `PpShotStrip`, `PpSparkline`
- **Used by (1):** `PpDiagReplaySessionRows`
- **Exercised by tests/tools:** `typeset_figures`, `typeset_gallery`

### PpSparkline
`src/Gui/diagnostics/PpSparkline.qml` · 130 lines · root `Item` · no direct `Theme.*` refs

> SPARKLINE — one pattern's shots as a mini run chart, for Replay rows and B's marginal entries.
> Same grammar as PpRunChart, small: the corridor wash, the fault line (warn hairline), one PpMark per shot
> in the order struck, the picked shot ringed, a stub on the foot for a shot not measured.
> Round 3, FAULT-FORWARD: the same PpFigureScale as the detail figures, so the line spends its height where
> the shots are and the empty corridor collapses to a thin band past a break (marked by a pair of slashes
> at the left). Marks are inset by their ring's radius, so nothing spills out of the box into the next row.
> PUBLIC API (stable; the internals belong to the fault-forward figure work):
> card            a session card (uses card.spread.{run,bands,faultLines} and card.ticks)
> roles, brush, medium
> width (given), implicitHeight = roles.u(22)
> signals shotActivated(swingDir, shotId), shotHovered(shotId)

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `var card`, `readonly var sp`, `readonly var keyKinds`, `readonly real ringSz`, `readonly real pad`, `readonly var run`, `readonly int n`
- **Signals:** `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Functions:** `fy(f)`
- **Uses:** `PpDiagRoles`, `PpFigureScale`, `PpMark`
- **Used by (2):** `PpEntryFlow`, `PpSparkRow`
- **Exercised by tests/tools:** `typeset_figures`, `typeset_gallery`
