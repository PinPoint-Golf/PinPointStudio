# Typeset

*Directory:* `src/Gui/typeset/`  
*18 components · 2,113 lines of QML*

The 'typeset system' (design system §15): the per-panel helper PpTypeset plus a shared plot/typography vocabulary (box plot, zone plot, brush, marks, section heads, pager, folio) that draws identically on screen and in print.

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [PpBoxPlot](#ppboxplot) | 260 | `Item` | 3 | 2 | 1 local comp. |
| [PpBoxReading](#ppboxreading) | 154 | `Item` | 1 | 0 |  |
| [PpBrush](#ppbrush) | 81 | `QtObject` | 4 | 0 |  |
| [PpFigureScale](#ppfigurescale) | 121 | `QtObject` | 2 | 0 |  |
| [PpFolio](#ppfolio) | 49 | `Item` | 2 | 2 |  |
| [PpInlinePicker](#ppinlinepicker) | 74 | `Column` | 5 | 1 |  |
| [PpMark](#ppmark) | 86 | `Item` | 13 | 0 |  |
| [PpNotes](#ppnotes) | 54 | `Column` | 5 | 1 |  |
| [PpPageKey](#pppagekey) | 170 | `Flow` | 6 | 3 |  |
| [PpPager](#pppager) | 210 | `Item` | 1 | 3 |  |
| [PpPulseWash](#pppulsewash) | 60 | `Rectangle` | 5 | 0 |  |
| [PpRunningHead](#pprunninghead) | 41 | `Item` | 2 | 1 |  |
| [PpSectionHead](#ppsectionhead) | 55 | `Item` | 5 | 1 |  |
| [PpShotStrip](#ppshotstrip) | 116 | `Item` | 4 | 0 |  |
| [PpStandfirst](#ppstandfirst) | 80 | `Column` | 3 | 1 |  |
| [PpTabs](#pptabs) | 93 | `Item` | 4 | 1 |  |
| [PpTypeset](#pptypeset) | 300 | `QtObject` | 18 | 0 |  |
| [PpZonePlot](#ppzoneplot) | 109 | `Item` | 3 | 1 |  |

### PpBoxPlot
`src/Gui/typeset/PpBoxPlot.qml` · 260 lines · root `Item` · no direct `Theme.*` refs

> BOXPLOT — THE box plot. Every box in the app draws through this (Mark, 18 Oct 2026: "consolidate into a
> reusable component and make hover work everywhere a box plot is shown"): the Charts rows, runs, traces,
> kinds, scatter margins and Wrist rows (PpChBoxCell), Session diagnostics' Spread box (PpBoxStrip) and its
> across-sessions boxes (PpHistoryStrip). No number is worked out here: the statistics come from the one C++
> implementation (typeset/box_stats.h) and the labels from whoever owns their formatting.
> Along an axis the caller maps (`map(v)` → px; across the item, or down it when `vertical`), centred on
> `cross`: the middle half as an outlined box with a faint fill, the median rule, whiskers to `lo` and `hi`
> with short caps, the caller's shot marks (PpMark: the outliers, the picked shot ringed, a hovered echo),
> and — across only — a corridor: the typeset's wash where the condition does not fire, a fault tick at each
> fault line. EXAMINED (its own passive hover with `valuesOnHover`, or `examined` from a caller that owns the
> hover) it lays its values on the plot as text (Mark: "all the values you would see on a box plot"): across,
> the median, whisker ends and fault lines above and the quartiles and corridor edges below, a label that
> would collide with a more important one in its lane left out; stood up, PpBoxReading beside the box with a
> tick from each true height and the count line.
> PUBLIC API
> roles          a PpTypeset (PpChRoles / PpDiagRoles)
> …

- **Properties:** `QtObject roles`, `bool vertical`, `var map`, `real cross`, `var stats`, `real bodyW`, `real capW`, `real medianOver`, `real lw`, `real medW`, `color ink`, `color medianInk`, `color fill`, `var marks` … (+21 more)
- **Signals:** `markActivated(var mark)`, `markHovered(var mark, bool on)`
- **Functions:** `fin(v)`, `px(v)`
- **Local components:** `BoxMark`
- **Uses:** `PpBoxReading`, `PpMark`
- **Used by (3):** `PpBoxStrip`, `PpChBoxCell`, `PpHistoryStrip`

### PpBoxReading
`src/Gui/typeset/PpBoxReading.qml` · 154 lines · root `Item` · no direct `Theme.*` refs

> BOXREADING — a box plot's full reading, shown while the box is EXAMINED (hovered): the upper whisker
> end, Q3, the median, Q1 and the lower whisker end, each a small number at its own height with a
> hairline tick from the box edge, plus "15 shots · 1 outlier" under them. The labels stand on one side
> of the box — right by default, left when the right lacks the room or would run into a zone label in
> the figure's right gutter at the reading's heights (`forceSide` overrides); labels closer than a line are
> nudged apart as a group centred on their true heights, and each tick bends to its label from the
> TRUE height, so nothing points at a value it does not hold. A paper backing keeps the numbers legible
> over whatever they cross (the next column's shots, the zone labels).
> Drawn by PpBoxPlot (the one box plot) for a VERTICAL box: Session diagnostics' Spread box and across-sessions
> strip, and the Charts boxes stood up at the P-positions of an opened reading.
> PUBLIC API:
> items              [{ y, text, strong, f }] in any order: each value's TRUE height in this item's coordinates and
> its label, formatted by whoever owns the numbers; strong = the median, set in ink; f, the
> caller's own value, rides along on the label
> footLimit          how far down the reading may reach before its count line goes above it instead
> footer             the count line ("15 shots · 1 outlier")
> …

- **Properties:** `QtObject roles`, `var items`, `real footLimit`, `string footer`, `real edgeL`, `real edgeR`, `string forceSide`, `real roomLeft`, `real roomRight`, `real gutterX`, `var gutterYs`, `color paper`, `readonly bool ok`, `readonly var texts` … (+18 more)
- **Used by (1):** `PpBoxPlot`
- **Exercised by tests/tools:** `tst_session_diagnostics`

### PpBrush
`src/Gui/typeset/PpBrush.qml` · 81 lines · root `QtObject` · no direct `Theme.*` refs

> BRUSH — the shared focus state. ONE instance per register, passed to every block as `brush`.
> A thing that is hovered or picked is shown ONCE in every block that contains it.
> picked shot   — the carousel's shot: the ink ring (points) or the pick column (strips, barcode). Stays.
> hovered shot  — the same form, lighter: a hairline ring / a fainter column. Follows the pointer.
> highlight*    — the answer to a question: everything NOT highlighted recedes to roles.dim.
> Blocks WRITE hoveredShot / hoveredCondition themselves on hover (so brushing needs no wiring);
> clicks are only signals, which the register routes to the real body.

- **Properties:** `int pickedShot`, `int hoveredShot`, `string pickedCondition`, `string hoveredCondition`, `var highlightShots`, `var highlightConditions`, `string question`, `var cards`, `readonly var shotDirs`, `readonly bool shotFilter`, `readonly bool conditionFilter`, `var pulseIds`, `int pulseToken`, `int pulseStagger` … (+1 more)
- **Functions:** `dirOf(shotId)`, `isPicked(id)`, `isHovered(id)`, `shotLit(id)`, `conditionLit(id)`, `shotOpacity(id, dim)`, `conditionOpacity(id, dim)`, `hoverShot(id, on)`, `hoverCondition(id, on)`, `lightShots(ids, q)` (+4)
- **Used by (4):** `PpChartsRegister`, `PpDiagAnalyse`, `PpDiagPrint`, `PpDiagReplay`
- **Exercised by tests/tools:** `typeset_figures`, `typeset_gallery`

### PpFigureScale
`src/Gui/typeset/PpFigureScale.qml` · 121 lines · root `QtObject` · no direct `Theme.*` refs

> FIGURESCALE — the value axis that PpRunChart, PpBoxStrip and PpHistoryStrip share inside a PpFigureRow
> (C's "one figure grammar": panels in a row share one y scale). It maps the model's normalised
> position f (0..1 along spread.axisLo..axisHi) to a pixel y, and names the zone edges once.
> FAULT-FORWARD (round 3). The axis spends its height where the shots are. Give it the shots' f values
> (`values`) and it keeps a LINEAR WINDOW over [the shots ∪ the pass edge they must get back to], padded
> a little; whatever lies outside that window — for a fault, the empty corridor; for a clean condition,
> the empty watch and fault zones — collapses into a thin COMPRESSED BAND at that end of the axis,
> separated from the window by a drawn BREAK. True values stay readable: ticks and zone labels are in
> the measure's own units, a band is labelled with the values it holds, and anything in a band is still
> drawn there (compressed, never hidden). With no `values`, or `faultForward: false`, it is linear.
> Panels only ever call y(f), region(f) and read corridor / edges / bandLo / bandHi.

- **Properties:** `var bands`, `var faultLines`, `var yTicks`, `real plotTop`, `real plotHeight`, `var values`, `bool faultForward`, `real bandPx`, `real breakPx`, `real minOmit`, `bool clean`, `real axisLo`, `real axisHi`, `readonly var edges` … (+11 more)
- **Functions:** `valueOf(f)`, `region(f)`, `y(f)`, `tickVisible(f)`, `edgesIn(r)`
- **Used by (2):** `PpFigureRow`, `PpSparkline`
- **Exercised by tests/tools:** `tst_session_diagnostics`, `typeset_gallery`

### PpFolio
`src/Gui/typeset/PpFolio.qml` · 49 lines · root `Item`

> FOLIO — print furniture at the foot of a page: the page's key (only the marks this page uses, C), a
> hairline, a quiet footer (the final session state; generated <date>) and "Section · Page n of N".

- **Properties:** `QtObject roles`, `var readout`, `string footer`, `string section`, `int page`, `int pageCount`, `var keyKinds`
- **Uses:** `PpPageKey`, `PpTypeset`
- **Used by (2):** `PpLmGraphicsBody`, `PpPager`

### PpInlinePicker
`src/Gui/typeset/PpInlinePicker.qml` · 74 lines · root `Column` · no direct `Theme.*` refs

> INLINEPICKER — a choice that opens IN PLACE, never a pop-up (BRIEF3 §1; round 4, F67: the declared-miss
> picker opens under the tab row). A quiet question, then one row per choice — the current one marked —
> then the clearing row. It pushes what is below it down; it closes by the register setting `open` false
> (a pick, the chip again, or Esc). Nothing here is coloured: it is a control, not a finding.
> items: [{ id, label, note }]; current: the chosen id ("" = none). picked(id) — "" for the clearing row.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `bool open`, `string title`, `var items`, `string current`, `string clearLabel`, `string itemObjectName`, `string clearObjectName`, `readonly var keyKinds`
- **Signals:** `picked(string id)`
- **Uses:** `PpTypeset`
- **Used by (5):** `PpChartsRegister`, `PpChartsSessionView`, `PpChartsShotView`, `PpDiagAnalyse`, `PpDiagReplay`
- **Exercised by tests/tools:** `typeset_gallery`

### PpMark
`src/Gui/typeset/PpMark.qml` · 86 lines · root `Item` · no direct `Theme.*` refs

> MARK — the ONE point mark used by every chart (dot plot, spread, run, sparkline, history, key).
> Shape per theme (Folio round, Vector square); fired = warn, filled; clean = ink3, hollow in Folio,
> filled in Vector. The picked shot adds the ink ring; a hovered shot adds a hairline ink3 ring.
> Centred on (cx, cy). Hovering it brushes the shot; clicking it emits activated().
> (Bindings are guarded: a PpMark is created in the hundreds and torn down with its chart.)

- **Properties:** `QtObject roles`, `var brush`, `int shotId`, `bool fired`, `bool other`, `real cx`, `real cy`, `readonly bool ok`, `real size`, `real ringSize`, `bool interactive`, `bool ringed`, `readonly bool hovered`, `readonly bool square` … (+1 more)
- **Signals:** `activated()`
- **Used by (13):** `PpBoxPlot`, `PpCauseGraph`, `PpChMovesWith`, `PpChRunBox`, `PpChScatter`, `PpChSeqSession`, `PpChWristSession`, `PpDotPlot`, `PpHistoryStrip`, `PpPageKey` (+3)

### PpNotes
`src/Gui/typeset/PpNotes.qml` · 54 lines · root `Column`

> NOTES — small print: a footnote paragraph (A), optionally under a kicker and a short rule (B).
> tone 3 = ink3 (default), 2 = ink2. Held to 1.3 × the prose measure. Rich text allowed.
> keepPrev binds a footnote to the table above it when print cuts pages.

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `string text`, `string kicker`, `bool rule`, `int tone`, `bool italic`, `bool small`, `bool atom`, `bool keepPrev`, `readonly var keyKinds`
- **Uses:** `PpTypeset`
- **Used by (5):** `PpChartsKindsView`, `PpChartsShotView`, `PpDiagAnalyse`, `PpDiagPrint`, `PpLmGraphicsBody`
- **Exercised by tests/tools:** `typeset_gallery`

### PpPageKey
`src/Gui/typeset/PpPageKey.qml` · 170 lines · root `Flow` · no direct `Theme.*` refs

> PAGEKEY — C's key: each entry a drawn mark and its word, flowed. On screen one key per view, above its
> figures (never repeated per figure); in print the PpPager draws one key per page in the footer holding
> only the marks that page uses (every block exposes `keyKinds`).
> kinds: any of fired clean ring hover median fault pass corridor other bar tick stub (gap = stub) strip
> box fit chance dots (the Charts panel's: a session box, a least-squares line clear of / inside
> chance, raw samples)
> n-fired n-clean n-root n-ghost n-outcome
> l-movedTogether l-conditionallyDependent l-coherent l-presentTogether l-unanchored
> or set: "shot" | "session" | "watch" | "figure" | "paths" | "pathsShort". extraWords overrides a kind's word
> for one instance (a panel's own vocabulary); nothing else changes.
> Round 4: a page holding a ledger strip AND a barcode keys the strip ONCE ("strip" absorbs bar/tick/stub);
> the not-measured mark is a stub, never a gap (D5); the five link grades key with their own strokes (D6).

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `var readout`, `string set`, `var kinds`, `bool atom`, `bool keepNext`, `readonly var keyKinds`, `readonly string shotLabel`, `var extraWords`, `readonly var words`, `readonly var sets`, `readonly var list`
- **Uses:** `PpMark`, `PpShotStrip`, `PpTypeset`
- **Used by (6):** `PpBarcode`, `PpCauseGraph`, `PpChartsRelationsView`, `PpChartsShotView`, `PpDiagAnalyse`, `PpFolio`
- **Exercised by tests/tools:** `typeset_gallery`

### PpPager
`src/Gui/typeset/PpPager.qml` · 210 lines · root `Item` · no direct `Theme.*` refs · 2 hard-coded colour(s)

> PAGER — print: one document on A4. The blocks you put inside it are laid out ONCE as a galley at the
> text-block width (so the same size classes apply: portrait ≈ compact, landscape ≈ medium), then cut
> into pages only between ATOMS (A's galley + B's greedy packer):
> • an atom is any item with `atom: true` (a table row, an entry, a figure, a note, a section head);
> a library block (it has `keyKinds`) with no atoms inside is one atom; plain containers are walked.
> • keepNext binds an atom to the next (headings travel with what follows); keepPrev binds a footnote
> to its table; breakBefore starts a new page (a PpSectionHead with breakBefore: true).
> • a page that continues a section repeats its heading: "<section>, continued" (a PpSectionHead, or any
> block with a `section` property, e.g. a PpStandfirst, names it).
> • an atom taller than a page overflows honestly (it is clipped at the foot, never split).
> Round 4 (from the report's PrintPager, now the library's):
> 1. REPEATING HEADS. A block with a non-empty `printGroup` names a list; its first atom is the list's
> head (a table's column heads, a barcode's shot numbers, a path list's crosshead). A page that opens
> part-way through the list draws that head again under "<section>, continued", at its TRUE height and
> at 2× (a live ShaderEffectSource), and the page's room shrinks by it. Blocks may share one group.
> 2. NO STRANDED PAGES. A head always travels with the next atom. When a keep-chain would leave a page
> …

- **Properties:** `QtObject roles`, `var brush`, `var readout`, `var source`, `string footer`, `string title`, `int printPage`, `bool showKeys`, `real minFill`, `int minWidow`, `real blockGap`, `alias content`, `readonly int pageCount`, `var pages` … (+4 more)
- **Functions:** `paginate()`, `pageOf(item)`, `repaginate()`
- **Uses:** `PpFolio`, `PpRunningHead`, `PpTypeset`
- **Used by (1):** `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_gallery`

### PpPulseWash
`src/Gui/typeset/PpPulseWash.qml` · 60 lines · root `Rectangle`

> PULSEWASH — the after-shot pulse (round 4, D1): ONE soft ink wash over a row (or a tile, a mark's
> cell) whose condition changed state on the shot just surfaced. It rises in Theme.durationFast and
> fades over Theme.durationSlow, staggered by the row's place in brush.pulseIds (the focused condition
> first). Inert under reduceMotion, in print, and when the condition is not in the pulse.
> brush.pulseHold (renders only) holds the wash at full so a static grab can show it.
> Put it as the FIRST child of a row so it sits under the text: PpPulseWash { anchors.fill: parent; … }.

- **Properties:** `QtObject roles`, `var brush`, `string conditionId`, `real level`, `readonly int order`, `readonly bool held`, `readonly bool pulsing`
- **Functions:** `fire()`
- **Used by (5):** `PpBarcode`, `PpDotPlot`, `PpLedgerTable`, `PpSmallMultiple`, `PpSparkRow`

### PpRunningHead
`src/Gui/typeset/PpRunningHead.qml` · 41 lines · root `Item`

> RUNNINGHEAD — print furniture: "PinPoint Studio · Session diagnostics" on the left, golfer · date ·
> club · shot n of N on the right, a half-pixel rule under it. The PpPager draws it on every page; its
> `title` (PpPager.title) replaces the left side, and this default stays Session diagnostics' own.

- **Properties:** `QtObject roles`, `var source`, `var readout`, `string title`, `readonly var meta`
- **Uses:** `PpTypeset`
- **Used by (2):** `PpLmGraphicsBody`, `PpPager`

### PpSectionHead
`src/Gui/typeset/PpSectionHead.qml` · 55 lines · root `Item` · no direct `Theme.*` refs

> SECTIONHEAD — a numbered section head for a report or a long scroll ("1  Session patterns", with a
> quiet sub-line). It names the section the PpPager repeats as "<section>, continued" when a list runs onto
> the next page; breakBefore starts it on a new page (the detail feature always does).

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `string number`, `string title`, `string sub`, `bool rule`, `bool breakBefore`, `bool atom`, `bool keepNext`, `readonly string section`, `readonly var keyKinds`
- **Uses:** `PpTypeset`
- **Used by (5):** `PpChWristSession`, `PpChWristShotSection`, `PpChartsKindsView`, `PpChartsRelationsView`, `PpDiagPrint`
- **Exercised by tests/tools:** `typeset_gallery`

### PpShotStrip
`src/Gui/typeset/PpShotStrip.qml` · 116 lines · root `Item` · no direct `Theme.*` refs

> SHOTSTRIP — the session strip, ONE drawing used everywhere (ledger rows, folded lines, Replay rows,
> and each row of the PpBarcode). A cell per shot in the order struck, THREE distinct marks (D5):
> fired         = a warn bar (colour means "outside its corridor")
> clean         = an ink3 hairline tick, shorter than the bar
> not measured  = a short flat ink3 STUB on the baseline: present, never a gap
> The picked shot is the pick column (a faint ink wash behind its cell); hovered, a fainter one.
> Under a shot highlight, the other shots recede.
> ANY SESSION LENGTH (D7): ticks.length cells. Given `availableWidth` (> 0) the pitch narrows to fit, down
> to roles.stripMinPitch, and then the strip WRAPS onto further lines; it never clips and never drops a
> shot. Without it the strip is ticks.length × pitch wide, as before.

- **Properties:** `var roles`, `var brush`, `string medium`, `var ticks`, `real pitch`, `real barH`, `real barW`, `real availableWidth`, `real rowGap`, `bool pickBand`, `real bandPad`, `bool interactive`, `readonly int count`, `readonly var fit` … (+3 more)
- **Signals:** `shotActivated(string swingDir, int shotId)`, `shotHovered(int shotId)`
- **Functions:** `cellX(i)`, `cellY(i)`
- **Used by (4):** `PpBarcode`, `PpLedgerTable`, `PpPageKey`, `PpSparkRow`
- **Exercised by tests/tools:** `typeset_gallery`

### PpStandfirst
`src/Gui/typeset/PpStandfirst.qml` · 80 lines · root `Column` · no direct `Theme.*` refs

> STANDFIRST — B's head block: a kicker, a display head, a standfirst sentence built from the data,
> and (screen only) quiet filter links. size "head" for a view, "feature" for a masthead or a detail
> opening. The standfirst is held to the prose measure. The sentence comes from the panel's own helper
> (Session diagnostics: PpDiagRoles.shotStandfirst / sessionStandfirst / watchStandfirst(…, long));
> long = roles.shows("longStandfirst", width).

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `string kicker`, `string head`, `string standfirst`, `string size`, `var chips`, `bool interactive`, `string section`, `bool atom`, `bool keepNext`, `readonly var keyKinds`
- **Signals:** `chipActivated(string key)`
- **Uses:** `PpTypeset`
- **Used by (3):** `PpDiagAnalyse`, `PpDiagPrint`, `PpMasthead`
- **Exercised by tests/tools:** `typeset_gallery`

### PpTabs
`src/Gui/typeset/PpTabs.qml` · 93 lines · root `Item` · no direct `Theme.*` refs

> TABS — the view toggles: label + quiet count, the current one ruled in ink. Chrome thins last and
> least: counts go below compact (each view's head states its own), the review note on the right goes
> below medium (to a note at the foot of the view); the toggles never go. Screen only.
> Round 4: `interactive` (false on an auto-closing cast: the tabs are drawn, never pressed); an optional
> CHIP sits quietly at the far right of the row (the declared miss, F66: "Declare miss" / "Miss: Slice"),
> a link while interactive, plain ink3 text otherwise; `chipOn` draws it in ink (its picker is open).

- **Properties:** `QtObject roles`, `var brush`, `string medium`, `bool interactive`, `var tabs`, `string current`, `string note`, `string chip`, `bool chipOn`, `string chipObjectName`, `readonly var keyKinds`, `readonly bool counts`
- **Signals:** `tabActivated(string key)`, `chipActivated()`
- **Uses:** `PpTypeset`
- **Used by (4):** `PpChartsRegister`, `PpDiagAnalyse`, `PpDiagReplay`, `PpLmGraphicsBody`
- **Exercised by tests/tools:** `typeset_gallery`

### PpTypeset
`src/Gui/typeset/PpTypeset.qml` · 300 lines · root `QtObject` · 1 hard-coded colour(s)

> PPTYPESET — the per-panel helper of the typeset system (design system §15.3). It turns Theme's typeset
> DATA (Theme.typeset for the aesthetic, Theme.typesetPrint for paper) into what a block draws with: the
> type roles, the inks, the marks, the size classes, the flows and tiers, and the small generic text
> helpers. Not a singleton: the MEDIUM belongs to the instance, so one window can show a panel and lay
> out its printed report at the same time. A panel (or a register) makes ONE and passes it to every
> block as `roles`:
> PpTypeset { id: ts; medium: root.medium }        …        PpLedgerTable { roles: ts; … }
> Never give it the id `roles`: in `roles: roles` the block's own property shadows the id, and the block
> silently uses its default. Declare it LAST in the file (children are destroyed in creation order).
> GENERIC only. A panel's own vocabulary (its sentences, its column tier tables, its extra flows and
> element tiers) lives in a subclass — PpDiagRoles for Session diagnostics — which adds tables through
> extraFlows / extraTiers / extraElements and adds its own functions. No word of one panel belongs here.

- **Properties:** `string medium`, `readonly bool isPrint`, `readonly var r`, `readonly var p`, `var extraFlows`, `var extraTiers`, `var extraElements`, `readonly var flows`, `readonly var tiers`, `readonly real em`, `readonly color ink`, `readonly color ink2`, `readonly color ink3`, `readonly color warn` … (+53 more)
- **Functions:** `u(n)`, `sizeClass(w)`, `rank(cls)`, `atLeast(w, cls)`, `shows(tier, w)`, `gutter(w)`, `columns(w, minEm, gap)`, `measureW(w)`, `railFolds(w)`, `marginFolds(w)` (+27)
- **Used by (18):** `PpCameraFrame`, `PpChRoles`, `PpDataViewer`, `PpDiagRoles`, `PpFolio`, `PpInlinePicker`, `PpLmGraphicsBody`, `PpNotes`, `PpPageKey`, `PpPager` (+8)

### PpZonePlot
`src/Gui/typeset/PpZonePlot.qml` · 109 lines · root `Item` · no direct `Theme.*` refs

> ZONEPLOT — the common ground every value panel stands on: the corridor wash, the zone edges (fault in
> warn, pass dashed, the rest grid hairlines) and the baseline, all through one PpFigureScale.
> Round 3: when the scale is broken (fault-forward), the wash is drawn piece by piece so the BREAK stays
> clear, edges that fall in a compressed band are drawn there at reduced weight, and the break itself is
> marked at both ends of the panel with the conventional pair of slashes — never hidden.
> FIGURE-INTERNAL (used by PpRunChart, PpBoxStrip, PpHistoryStrip).

- **Properties:** `QtObject roles`, `var scale`, `readonly var sc`, `readonly var washPieces`
- **Uses:** `PpTypeset`
- **Used by (3):** `PpBoxStrip`, `PpHistoryStrip`, `PpRunChart`
