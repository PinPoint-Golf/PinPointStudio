# Round 2 — one design, every space: full, stage, half, third, and print

Mark's verdict on round 1: "blown away … all of them are really good. They have used the full screen
space well, but I do wonder how they would adapt to being on a half-screen panel (remember each view
layout can have multiple things going on at once). … consider 3 modes — on-screen full, on-screen half,
printed (landscape or portrait). … it is going to become really hard to make them work on smaller
spaces. … this work is developing an aesthetic that will be applied more broadly when we get it right."
Later: "the print path we need to add when the output is right, almost certainly print to pdf … a later
change." So print is a DESIGN target now (what the pages look like); the app's PDF export comes later.

Keep your variant's identity. This round is about making it hold up — and about finding the RULES that
make it hold up, because those rules become the house aesthetic for the whole app.

## The real spaces (measured in the app, 1680×1050 window, fontScale 1.25)
The stage packs its panels per arrangement (src/Gui/session/PpModeStage.qml). Session diagnostics
always leads, so these are the sizes it actually gets:
| layout | arrangement | the panel | neighbours |
|---|---|---|---|
| full  | tabs              | ~1522 × 699 | none (other panels are tabs) |
| stage | stage (62 % + side column) | ~940 × 699 | Charts + Table stacked in a side column |
| half  | split, 2 panels   | ~734 × 707 | Charts |
| third | split, 3 panels   | ~480 × 707 | Charts, Table |
Heights stay ~700; on a laptop or a smaller window they shrink too, so don't rely on height.
In em at a 14 px body: full ≈ 109 em, stage ≈ 67 em, half ≈ 52 em, third ≈ 34 em.

## The responsive grammar (follow it; improve it if you find a better rule, and say so in NOTES)
1. SIZE CLASSES BY MEASURE, not by pixels: compute `em` from your body role's rendered size and classify
   the width you are given: wide ≥ 90 em · medium 60–90 · compact 42–60 · narrow < 42. Your QML must
   adapt to the width it is GIVEN (bind to width) — never to a layout name.
2. TYPE NEVER SCALES with the panel. Roles keep their size in every class. Space gives way first: gutters
   and margins step down one notch in compact and narrow.
3. PRIORITY, NOT SHRINKAGE. Every element of a view has a tier: P1 always shown, P2 from compact up, P3
   from medium up (or wide only). A lower-tier item that drops out must still be reachable — moved to a
   second line, folded into a sentence, or left to DETAIL — and NOTES must say where it went. Write the
   tiers into a table at the top of the file next to the role table, so the rule is data, not scattered ifs.
4. FLOWS COUNT THEIR OWN COLUMNS: multi-column things (indexes, newspaper columns, small multiples,
   dot-plot blocks) take columns = floor(available / minimum column width of that role) — a minimum stated
   in em (e.g. prose ≥ 26 em, index ≥ 18 em, a small multiple ≥ 15 em). No fixed column counts.
5. RAILS FOLD: a left rail, margin column or sidenote column becomes a block above (or notes below) the
   content when the class drops to compact or narrow.
6. ABOVE THE FOLD: in every class, the first ~700 px must answer the view's question (This shot: what went
   wrong on this swing; Session: what keeps happening; Watching: what is emerging; Detail: how far, how
   often, and why). Vertical scrolling is fine below that. NEVER horizontal scrolling, never overlap,
   never clipped or ellipsised meaningful text.
7. CHARTS keep their grammar at every size: label and figure sizes stay fixed, tick density thins,
   side-by-side figures stack below a width, a chart has a minimum useful width (state it).
8. Chrome thins: in narrow, the tab row may drop its counts; a "Back" stays.

## Print (A4, portrait 794×1123 and landscape 1123×794 logical px at 96 dpi; rendered at 2×)
- The print layout is the SAME grammar: the page's text block width puts portrait in compact/medium and
  landscape in medium/wide — reuse the classes, don't write a separate design. Print type is a little
  smaller (body ≈ 10 pt ≈ 13 px) through the role table's `print` overrides.
- Paper: always the theme's LIGHT palette (the harness renders print with themes 6 / 12); white page,
  warn still marks out-of-corridor, tints lighter, hairlines thinner.
- Page furniture: a running head (PinPoint Studio · golfer · session date · club · shot 8 of 15),
  a folio (page n of N), a quiet footer (final session state; generated <date>). Margins ≈ 15–18 mm
  (57–68 px).
- No affordances: no tabs, Back, Open, Focus, filters, "tap to…". Links become cross-references
  ("see p. 3", "path 2 below").
- The report is one document, paginated in this order: the session (patterns) → this shot → watching
  (may share a page) → the detail of the open condition (Over the top) → anything else that is natural.
  Never split a table row, an entry, a figure or a path across pages; a heading always travels with what
  follows it. Decide pagination yourself (measure as you lay out; a fixed page plan per orientation is
  fine if it is honest about overflow).
- Contract (new): your root also declares
  `property string medium: "screen"` ("screen" | "print"), `property string printOrientation: ""`,
  `property int printPage: 0`, `readonly property int printPageCount`. In print the harness sizes your
  root to the page, sets medium/printOrientation, then steps printPage 0 … printPageCount-1 and grabs each.
  `view` is meaningless in print.

## Harness v2 (replaces variant.sh for this round)
`$D/variant2.sh <slot> <Variant.qml> <tag> <layout> [themes=7,12] [views] [detailId] [tall]`
- layout = full | stage | half | third — the probe switches the app's REAL arrangement first, so the
  window grab shows your panel with its real neighbours (Charts, Table).
- layout = print-portrait | print-landscape — renders the A4 pages (themes 6,12 for print).
- `tall` (8th arg) uses the 1680×2600 screen — for whole DETAIL pages at half/third.
- Output: `$D/out/<tag>_<layout>_<aesthetic>_<light|dark>_<view>_{window,panel}.png`,
  print: `$D/out/<tag>_print-<orient>_<aesthetic>_light_p<N>.png` (1588×2246 or 2246×1588).
- A QML error in your file can leave the app waiting until its 500 s timeout: if the first output lines
  show an error, `pkill -f "render_variant2.*<your Variant path>"` and fix it.
- Same rules as round 1: your slot only, one app instance at a time, no repo edits, no builds, no sound.

## Deliverables
- Update your `$D/variant_<x>/Variant.qml` in place (keep a copy of round 1 as `Variant_r1.qml` first).
- Final renders, tag `r2_<x>`:
  - screen: layouts full, stage, half, third × themes 7,12 × all four views;
  - DETAIL tall: half and third × theme 12 (`tall`);
  - print: print-portrait and print-landscape × themes 6,12 (all pages).
  Check themes 6 and 13 at half at least once while iterating.
- Delete your intermediates (keep r2_* and the round-1 final_* files).
- `$D/variant_<x>/NOTES2.md` (≤ 80 lines): per view, what happens at each class (a small table is ideal);
  the tier table; minimum column widths; how print paginates; the role/tier additions proposed for Theme;
  and — most important — the 5–10 RULES you would make house rules for the whole app, from what you learnt.
