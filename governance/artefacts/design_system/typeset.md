# The typeset system

The second look of the app: a panel set the way a typesetter sets a page, with a measure, a grid and a type scale; the figures do the comparing and the sentences the explaining; every fact appears once; and the same page prints without a second design[^s15-1]. It binds every typeset panel: every panel drawn with the typeset blocks (`src/Gui/typeset/` and the blocks built on them; the panels are listed on the [PpTypeset](components/PpTypeset.md) page). A panel whose own design records an exception follows it there: inside the launch monitor's figures, its design (`docs/design/lm_panel_fsx.md`) puts Foresight's grammar before the no-boxes and colour-only-for-out rules, and allows its hover card, which outlives the pointer so its controls can be used[^lm][^q11].

The values are data in Theme.qml: `Theme._typesetDefault`, each theme's optional `typeset` block, and `Theme.typesetPrint`. A panel reads them through one [PpTypeset](components/PpTypeset.md), which holds the scale (`u()` size, `g()` space), the type roles, the inks, the marks, the size classes, the flows and the tiers. This page states the rules; the component pages state the mechanics and the numbers, and it points to them rather than repeating them.

## Inside a stage panel

- The coaching-card shell frames each stage panel; inside the frame the content follows this page, and where the two disagree, this page wins[^s15]:
  - colour means only "outside its corridor" (with the exceptions house rule 7 names), so a typeset panel has no `colorGood` ticks, tone badges or how-often meters;
  - the selected tab's rule is ink, never the panel's tone[^s15][^ruled];
  - every stage card takes the quiet tone[^quiet].
- Colour is never the only channel: an out-of-corridor state also has a position (a dot past the fault line), a shape (a bar where a clean shot has a tick) or a heading it sits under[^s15].

## The house rules

1. **Measure in em, not pixels.** A component's size class comes from the width it is given, in ems of the body role (the classes and their bounds: [PpTypeset](components/PpTypeset.md), "Size classes and tiers"). No component ever asks which layout it is in[^s15-2].
2. **Type is fixed and space gives way.** Each type role keeps its size at every width. Gutters step down one notch below medium before anything else changes. There are no fit factors. A second-screen cast's zoom is not an exception: it scales the whole surface, for viewing distance[^s15-2].
3. **Fold, don't drop.** Every element has a tier and an "else": a column that cannot fit becomes a labelled item on its row's second line, a sentence absorbs a figure, a note moves under its chart. Nothing is ellipsised and nothing silently disappears. Tiers live in tables, as data[^s15-2][^ruled].
4. **Flows count their own columns** from a minimum in em (the minimums: [PpTypeset](components/PpTypeset.md), "Size classes and tiers"). Never a fixed column count[^s15-2].
5. **Rails fold.** A side rail or margin survives only while two content columns still fit beside it; otherwise its heading goes above the content and its notes below[^s15-2].
6. **Pages are made of blocks.** A view is an ordered list of blocks that never split: an entry, a row, a figure, a path. A heading travels with its first block. The screen scrolls the blocks; print measures them and packs them onto pages. Only lists may split, and then they repeat their heading ("continued")[^s15-2].
7. **One encoding per fact.** Colour means "outside its corridor" and nothing else, except where this page names an exception (the heat of a table cell; a metric's identity colour)[^s15-2], the inks [PpTypeset](components/PpTypeset.md) lists for a variability mark's grades[^ts-inks], and the shot-mix scheme: a shot-mix part is in good, in cool when it is a value (neither praise nor fault), in attention when it is Low, and in warn when it is a fault[^mix]. The picked shot gets one mark. Accent means a live control. Where accent is too close to warn (Vector), links are ink with an underline[^s15-2].
8. **Charts keep their grammar at every size.** Labels keep their size, ticks thin out, panels that share a scale stack below a stated width, and a diagram below its minimum changes form instead of shrinking[^s15-2].
9. **Above the fold, every view answers its question at every width.** As the width drops, the "why" sentence moves up[^s15-2].
10. **Print is a medium, not a separate design.** It uses the same blocks and classes: A4 portrait is compact and landscape is medium. Print sets its own sizes, hairlines and tints (the print block: [PpTypeset](components/PpTypeset.md), "The scale: size, space, medium"), and always uses the light palette on white[^ruled]. Every page gets a running head, a folio and a footer. Affordances disappear and links become cross-references ("detail on p. 5"). Keys appear once per page and show only that page's marks[^s15-2].
11. **Chrome thins last and least.** At narrow, tab counts and breadcrumbs go; Back, the view toggles and the subject's name never do[^s15-2].
12. **Height is a resource too.** A list splits into columns only when its rows don't fit the height. Spare height admits the next tier of content, never bigger type[^s15-2].

## Type

- The type roles, their families and sizes per theme are on the [PpTypeset](components/PpTypeset.md) page; the values are in `Theme._typesetDefault` and the theme blocks[^s15-3].
- Weights: text Normal, strong Medium. Leading: 1.32, small 1.25 (Vector 1.28 and 1.22)[^s15-3].
- A view uses at most four sizes and two weights, and nothing renders under 10 px at the reference text size of 125 %[^s15-3][^q4].
- Folio's display and standfirst are the serif. Vector's display is Space Grotesk Light, not the theme's Space Mono, because a headline is prose[^s15-3].
- A panel that keeps its own design values keeps them in a sub-block of the typeset table (`lm` for the launch monitor, `ch` for Charts), merged one level deeper so a theme lists only the keys it changes. Everything in the Charts block is drawn in the metric's own hue or in ink, never a second hue[^s15-3].

## The shared encodings

These bind every typeset panel and every register[^s15-4].

1. **Colour means "out".** Warn appears on values, fired marks, strip bars and the fault line, and nowhere else.
   - **A metric keeps its own colour**[^identity]: a curve, line, dot or box that stands for a metric is drawn in `Theme.metricColor(key)`. That is identity, not judgement.
   - A mark that stands for a shot (a scatter dot, a strip cell, the picked-shot ring) stays ink.
   - One metric across many shots stays in its one hue: other shots at low alpha, and the median, group fit or ghost told apart by weight and dash, never by a second hue.
   - Because a metric's hue can sit near warn, "out" in a figure is never colour alone: it is a warn mark (ring, tick or underline) plus a word ("late", "out by 4°"), and it reads in greyscale.
   - Print uses the light palette's metric colours[^ruled].
2. **One strip, three marks, never a gap.** A fired shot is a warn bar, a clean shot a short ink3 tick, a shot not measured a shorter ink3 stub on the baseline: an absent measure is unavailable, never a pass. The barcode, the sparkline, the run charts and a ledger's shots column use the same three marks ([PpShotStrip](components/PpShotStrip.md), [PpStub](components/PpStub.md)). Nothing assumes a session length: strips size their pitch to the shot count and the width given, thin their labels or wrap below a minimum pitch, and never clip; shot labels come from data; "Shot N of M" uses the ordinal, never the shot id.
3. **One mark for the picked shot**: an ink ring where the shot is a point, an ink column where it is a cell. A hovered shot takes the same form, lighter: its wash is ink at 0.045 light and 0.065 dark, against the pick's 0.07 and 0.10[^code]. A highlight dims everything else ([PpBrush](components/PpBrush.md)). A table cell that can carry heat is picked with an ink outline instead, because its wash has taken the column's job ([PpChMatrix](components/PpChMatrix.md)).
4. **One chart wash, on the corridor.** The corridor gets a neutral ink wash; the fault line is a warn hairline and the pass edge a dashed ink3 line. A region is never washed in warn: colour is spent on data, not on a region.
   - **One exception: the heat of a table cell.** One hue, `colorAttention`, at the `colorHeat*` strengths (fainter on paper). It says how much, never good or bad, so it is never `colorWarn`. The word, or the mark and its key, stays in the cell. It belongs to a cell of data, never to a plot or a region.
5. **One mark shape per theme** (Folio round with clean marks hollow; Vector square with clean marks filled).
6. **Links:** Folio in the accent; Vector in ink2 with an underline.
7. **Hairlines** are 1 px on screen and 0.5 px in print. `rule` is the heavy top of a table or a figure, and `hair` separates rows.
8. **Numbers** are right-aligned in tabular figures. A unit has its own column, or rides on the figure when it is a symbol (°, %).
9. **Words** are plain, in sentence case, with no ellipsis[^ruled]. A qualifier that would repeat on every row becomes a footnote mark (\* † ‡ §), defined once.
10. **Link grades keep their honesty.** Each of the model's five link grades keeps its own stroke and its own glyph in text: ⇒ moved together, → conditionally dependent, ╌ coherent, ∼ present together, ⋯ unanchored[^code]. Only the two grades that support a direction (moved together, conditionally dependent) get an arrowhead, and they are the strongest strokes; coherent, present together and unanchored get none, and unanchored is drawn at half opacity. One key defines the strokes and glyphs once. A strong grade never falls into a weak style, and no arrow is drawn that the evidence does not support (the grades: [PpTypeset](components/PpTypeset.md), "Inks and marks (read-only)").

## Size classes, flows and tiers

The classes, flows, tiers and shared elements, with their numbers, are on the [PpTypeset](components/PpTypeset.md) page. The rules[^s15-5]:

- A block binds to the width it is given: `sizeClass(width)` is the only question it asks.
- Minimums are chosen so a real layout does not straddle them; otherwise a column folds on its own before the whole block changes form.
- Every element with a tier above 1 names its else: where it goes when it drops.
- A panel's own tier tables (a block's columns, its extra elements) live as data in that panel's PpTypeset subclass (`extraTiers`, `extraElements`); the shared element tiers live on PpTypeset[^code].
- Gutters are space: `gutter(w)` is a `g()`, so on screen they follow the Density setting and the theme's spacing scale; in print they equal `u()`[^code].
- Fold, don't drop, mechanically: a folded column joins its row's second line in a fixed order, with its label; a shaded picked-shot column becomes per-strip cell shading once the strips fold; a rail folds to a head above and notes below, a margin to notes under its figure, a graph to chains or an outline, a long standfirst to its short form with the rest moved to the section that continues it.
- If you cannot say where an element went, it was dropped. Fix it.

## The registers

- A **register** is how much a panel says, and how it says it. The situation chooses it, never the view and never the width. Every register uses the same blocks, type roles and encodings, so moving between them is a change of depth, never a change of app[^s15-6].
- On the stage the session mode picks the register: Capture and Replay take Replay (the glance), Analyse takes Analyse (the read), and `medium: "print"` takes Print (the report). Within a register, the size class still comes from the width[^s15-6].
- **The cast** (the second-screen window) is always the Replay register, whatever mode the stage is in. Its `surface` and `interactive` are both passed to the register, and neither is inferred from the other. An auto-closing cast takes no clicks, never switches the stage's mode, has no tab row and shows a running head in its place; a persistent cast keeps its tabs. A cast never offers a detail[^s15-6].
- **The cast's zoom** scales the whole surface, from 1 to 1.6; it is computed from the data, never measured from the laid-out page, and it never takes the page below compact[^s15-6].
- **A detail opened in Replay is read in place**: it loads the Analyse register with the stage still in Replay, the glance held under it, and Back returns to the glance with its tab and scroll intact. Clicking a condition in Replay never switches the stage's mode[^code]. A tab press with a detail open closes the detail first; Esc closes it[^d9].
- A Replay head is the answer itself ("Ten outside the corridor"), with one ink3 line of context beside it, or under it when the two don't fit[^s15-6].
- A register chooses blocks and voice, never its own colours, marks or type[^s15-6].

## Interaction

- **Quiet by default.** Nothing moves or appears until the reader moves. Hovering reveals in place, into space already reserved: no layout jump, no floating tooltip, no pop-up, no menu and no native dialog (the launch monitor's hover card is its recorded exception, above)[^s15-7].
- **The one exception is the after-shot pulse**: after a surfaced shot, the rows and marks whose state changed get one soft ink wash that fades over `durationSlow`, the focused condition first; in both registers; never warn or accent; none under reduce-motion ([PpPulseWash](components/PpPulseWash.md))[^s15-7].
- **Place-keeping.** A panel keeps the reader's place through republishes and rebuilds: its filter lives on the body, its scroll areas carry stable `objectName`s and restore from the body's saved navigation, a hover survives a republish, and demo code runs only when `demoState` is set[^s15-7].
- **One focus at a time.** One shared [PpBrush](components/PpBrush.md) per panel holds the picked and hovered shot and condition; hovering a shot anywhere lights it in every strip, plot and chart on the panel. Blocks write to the Brush themselves, so hover needs no wiring[^s15-7].
- **Every click goes somewhere real**, through the panel's live body[^s15-7].
- **Motion is time, not decoration:** hover `durationFast`, a change of focus `durationNormal`, the pulse `durationSlow`, all 0 under reduce-motion[^s15-7].
- **Sharper questions (Analyse only).** A view may end its lead table with at most three questions the reader can ask with one click, each computed from the data shown, phrased as a question with the subject's name first, and answered in place (it lights or filters what is already on the panel, or steps the carousel). They sit straight after the lead table[^s15-7].
- **The honesty rules.** A co-occurrence answer ends "They occur together; that is not to say one causes another." A question the data cannot answer is not shown. Never a coaching verdict. No direction is claimed for a clean two-sided row[^s15-7].
- **Interactive states are renderable**: a register declares `demoState` and a short list of named states that put the panel in that state statically, for review renders[^s15-7].
- **Print has none of this.** Affordances become cross-references; an answer can appear as a footnote, because the answer itself is content[^s15-7].

## The figure grammar

- **A data-driven broken axis.** A figure gives the corridor its true share of the axis only when the shots are there; an empty end collapses into a band behind a drawn break ([PpFigureScale](components/PpFigureScale.md) holds the window, the padding and the thresholds). The axis stays in true units[^s15-8].
- **The break is honest.** It is always drawn ([PpBreakSlash](components/PpBreakSlash.md), and "≈" on the axis); ticks appear only where the scale is true; a band is labelled "≈ compressed", and a fault line inside a band reads "fault ≈"; shots, boxes and whiskers inside a band are still drawn there, and lines break at the gap; the figure's note explains the break[^s15-8].
- **The distance to travel.** For a fault, the empty zone becomes the target: the picked shot's distance to the pass edge on its side, labelled at the measure's own decimals ("9 past the fault line", "13 to get back inside"). When the same number is stated above the fold and in the figure on one screen, it is kept in one place[^s15-8].
- Every figure that has a corridor uses this grammar[^s15-8].
- A sparkline's marks are capped at 55 % of a shot's pitch, so fired shots never merge into one bar, and inset by the ring's radius, so nothing spills into the next row. Every mark is snapped to half pixels[^s15-8].
- Fired marks are filled and in warn; clean marks follow the theme; the picked shot is ringed once per chart. Zone edges are labelled once, in a right-hand gutter. One key per view sits above its figures, never repeated per figure. A caption names the measure the figures read. Panels that share a value axis sit in one row while there is room, and stack below that[^s15-8].
- Every box plot is the one [PpBoxPlot](components/PpBoxPlot.md)[^boxplot], and its values show on the plot while it is examined ([PpBoxReading](components/PpBoxReading.md))[^box].

## Print

- The register sets `medium: "print"`, and the same blocks lay out at the text block's width: A4 portrait is compact, landscape medium[^s15-9].
- On paper the body comes from `printScale`, never the user's text size; hairlines are 0.5 px and tints lighter (the print block: [PpTypeset](components/PpTypeset.md)). Warn still marks out-of-corridor[^s15-9].
- Every page has a running head, a quiet footer and a folio ([PpRunningHead](components/PpRunningHead.md), [PpFolio](components/PpFolio.md)), and one key with only the marks that page uses ([PpPageKey](components/PpPageKey.md))[^s15-9].
- No affordances: tabs, Back, Focus, filters, Open, "tap to…" and the questions' buttons are removed, and links become cross-references ("The feature on p. 4 reads it in full")[^s15-9].
- The running section label names the section the page starts in: a page that opens on "Over the top, continued" and ends in the Index is about Over the top[^s15-9][^ruled].
- Pagination is [PpPager](components/PpPager.md)'s: a galley cut only between atoms, keeps, "continued" heads, no stranded pages[^s15-9].

[^s15]: `docs/design/pinpoint_qml_design_system.md` §15, introduction (Design Lead, October 2026).
[^s15-1]: §15.1 (Design Lead, 11 Oct 2026).
[^s15-2]: §15.2, the twelve house rules (three design rounds, October 2026; rule 12 after the Design Lead's live try-out).
[^s15-3]: §15.3, roles as data.
[^s15-4]: §15.4, the shared encodings (reconciled in round 3; decisions D5, D6 and D7 taken for the Design Lead, 11 Oct 2026).
[^s15-5]: §15.5, size classes, flows and tiers.
[^s15-6]: §15.6, the registers (decision D4, 11 Oct 2026).
[^s15-7]: §15.7, interaction (decisions D1 and D8, 11 Oct 2026).
[^s15-8]: §15.8, the fault-forward figure grammar.
[^s15-9]: §15.9, print.
[^lm]: `docs/design/lm_panel_fsx.md`, introduction (the launch monitor panel's design).
[^q11]: Design Lead, 25 Nov 2026 (#195, Q11: the launch monitor panel's recorded exception gains its hover card).
[^ruled]: Design Lead, 25 Nov 2026 (#195, Q1, Q2, Q9 and Q12: the rule stands; the code that does not yet follow it is fixed under #279).
[^q4]: Design Lead, 25 Nov 2026 (#195, Q4: the floor is stated at the reference text size).
[^code]: Design Lead, 25 Nov 2026 (#195, Q3, Q5, Q6, Q7 and Q8: the code's rule stands, and §15's differing wording is retired).
[^ts-inks]: [`components/PpTypeset.md`](components/PpTypeset.md), "Inks and marks (read-only)".
[^mix]: The one shot-mix scheme, as Home's Tendencies row uses it (10 Nov 2026); named as an exception to this rule by the Design Lead, 25 Nov 2026 (#195, Q10).
[^boxplot]: [`components/PpBoxPlot.md`](components/PpBoxPlot.md) ("THE box plot").
[^quiet]: §15.11: Design Lead, 11 Oct 2026 ("Quiet tone please").
[^identity]: §15.4 item 1: Design Lead, 14 Oct 2026.
[^box]: §15.8, Detail's figure row (Design Lead, 12 Oct 2026: the spread strip became a box plot, and hover to examine).
[^d9]: §15.6, decision D9, revised 11 Oct 2026 after the Design Lead's try-out.
