# Session diagnostics — three typeset variants (design brief)

Owner: Mark Liversedge (PinPoint Studio). Date 11 Oct 2026.
Mark's words: "far too noisy, dense walls of text with no finesse and fonts that are too large. I would
prefer something more akin to a typeset published document." He likes the Folio theme and the
matplotlib growth-chart look (Inter, generous spacing, hairlines, professional). He wants future
aesthetic changes to be driven from THEMING, and is open to widening the Theme to support new ideas.

The panel is the Session diagnostics stage card on the Wrist screen, Analyse mode, a reviewed
session (4 Jul 2026, 7 iron, 15 shots), shot 8 picked. It has FOUR views:
  1. THIS SHOT — every condition (78) for the picked swing; 10 fired, 66 clean, 2 not measurable.
  2. SESSION — the 8 session patterns (cards), with the "session bookends" strip and filter chips
     (All / Reaches the ball / Roots).
  3. WATCHING — 15 conditions seen but not yet a pattern ("tap one to trace it").
  4. DETAIL — one condition drilled into (Over the top): header, "How far, shot by shot" spread
     (dot strip against corridor zones), the shot-by-shot run chart, "Across sessions" history,
     "What might have caused this" (causal paths), "What this leads to" (effects), rivals note.

## Current renders (read these first)
`out/before_<vector|folio>_<light|dark>_<shot|session|watching|detail>_window.png` (1680×1050 window;
the panel is the lower card, 1522×699) and `out/tall_*_detail_panel.png` (the whole detail page on a
2600-tall screen; ignore black — that grab is transparent). Use the `_window` images to judge colour.

## Review of the current panel (what makes it ugly)
1. Same weight everywhere. Every line is the same size and almost the same grey; nothing leads.
   In Vector, all prose is letter-spaced Space MONO — the "wall of text" is mostly a monospace face
   set at body size for sentences.
2. Six lines per card: name, recurrence, value + corridor, direction sentence, 15-tick strip, trend +
   "firings after". Most of them are TRUNCATED with "…" ("fault outside -…", "pass up to 8.7 · fa…"),
   which is both noisy and loses the information.
3. Redundant encodings. "Fired here" is said five ways at once: the pill, the 5-bar strength meter,
   the orange numeral, the orange value text, the orange tick. "15 of 15 measurable shots" and the 15
   ticks say the same thing. "session median 22 / this shot 22" repeats "22.1 % hand rise".
4. Colour is spent everywhere: orange on value, corridor, trend sentence, pill, ticks, numerals;
   green on IN, ticks and numerals; amber/olive bars. Nothing stands out because everything does.
5. THIS SHOT gives the 66 clean conditions the same box as the 10 that fired: 78 equal cells in a
   5-column grid, each with icon + bars + IN/OUT + value + truncated corridor + tier tag.
6. Type is too large and it SCALES WITH THE PANEL (`k`, up to 2.4×, on top of the user's
   fontScale 1.25): extra room becomes bigger type instead of margin. Meanwhile the smallest
   captions (8–9 px "7 more firings after this shot") are too small to read. Big + tiny, no middle.
7. No grid, no measure. Lines run the full 1500 px; columns don't align; numbers aren't aligned.
8. DETAIL: saturated zone fills (green / olive / brown blocks) dominate the charts and the dots get
   lost; three charts in three styles; causes as boxed cards with dashed connectors, then 7 more
   paths each repeating "screened root · needs a physical screen · measured · clean on every
   measurable shot"; the page has no rhythm.
9. Chrome noise: pills, dashed rings, chip outlines, "▸" arrows, link-blue on every bookend title.

## Principles every variant follows
- ONE encoding per fact. A shot's state is shown once (e.g. fault colour on its value, or one small
  mark), never pill + bars + colour + tick at once.
- Colour is RESERVED: Theme.colorWarn only for what is out of its corridor (faults stay colorWarn —
  a settled house decision). Clean = ink/grey, i.e. the absence of colour. Theme.colorGood rarely
  or never. Accent only for genuinely interactive text, and sparingly.
- A real type scale: at most 4 sizes and 2 weights on a view. Prose in the text face (never a
  letter-spaced mono). Figures in tabular numerals (Theme.fontData). Caps / small-caps labels only
  for short labels.
- Sizes from Theme.sp(n) (respects the user's fontScale = 1.25 here) — NOT multiplied by a panel
  fit factor. Target RENDERED sizes in this window around: body 13–14 px, secondary 11–12 px,
  view headline 17–22 px, figures in charts 10–11 px. Nothing under 10 px rendered. Spare panel room
  becomes margin, gutters, columns — never bigger type.
- No ellipsis truncation of meaningful text. Rewrite shorter, wrap, or move the detail to DETAIL.
- Hairline rules and whitespace instead of boxes, pills and outlines. A column grid; numbers
  right-aligned; prose measure ≤ ~75 characters.
- Charts: one style throughout; hairlines; corridor zones as very light tints or just boundary
  hairlines with direct labels; dots carry the data; the picked shot (8) highlighted once.
- Information must not be lost. Duplicates can go. Anything moved (e.g. corridor text only in DETAIL)
  must still exist somewhere in the four views. Missing values show "-" (never 0).
- Affordances stay visible but quiet: the three view tabs, the filter chips, bookend links, BACK in
  detail, "Show" for the tail, "Open" on a path, focus toggle. (Mocks: they need not work.)
- EVERYTHING FROM THE THEME. Any new design value (type roles, rule weights, zone tint strength,
  label case, figure style …) goes in ONE table at the top of your file, keyed by Theme.aesthetic
  (with a default), e.g. `readonly property var roles: ({ folio: {...}, vector: {...}, _: {...} })`.
  That table is the proposal for what moves into Theme.qml. The same QML must look right — and
  DIFFERENT, in each theme's own character — in Vector (Space Grotesk / Space Mono, square, techy)
  and Folio (Inter / Inter Tabular / Inter Display, system serif Theme.serifFamily, sentence case).
  Theme.caps(s) and Theme.capsFont give each theme its heading case. Bundled fonts available:
  Inter, Inter Display, Inter Tabular, Space Grotesk, Space Mono, Source Serif 4, Literata,
  Fraunces, Gelasio (and system Georgia via Theme.serifFamily), DM Sans, DM Mono, DM Serif Display,
  Geist, Geist Mono, Hanken Grotesk, Instrument Sans, JetBrains Mono, Playfair Display.
  A role table may propose a new font for a theme (say why in the notes).

## The three directions
A — LEDGER. The annual-report / FT data-table page. Ruled tables, small-caps column heads, tabular
    figures aligned, colour only on out-of-corridor figures. Inline 15-shot micro-strips as a column.
B — EDITORIAL. The magazine feature / journal page. Serif display heads, a standfirst sentence per
    view, entries written as short typeset paragraphs with figures inline, marginal sparklines,
    newspaper columns with column rules, figures captioned ("Fig. 1").
C — FIGURES. The Tufte / scientific small-multiples page: the data IS the page. Identically-scaled
    mini-charts per pattern (shots × distance from the corridor), a dot-plot of every condition on a
    common corridor-normalised scale for THIS SHOT, a condition × shot barcode for WATCHING, and a
    chart-led DETAIL with direct labelling and sidenotes.

## Harness (no builds; the real app renders your QML over the real panel with real data)
Directory: the session scratchpad's `diag/` folder, called $D (the harness now lives in `tools/typeset-harness/`).
- Your variant is ONE QML file `$D/variant_<x>/Variant.qml` (inline `component` definitions are fine;
  sibling files in the same dir can be imported with `import "."`). `import QtQuick`,
  `import QtQuick.Shapes`, `import QtQuick.Layouts`, `import QtQuick.Controls`, `import PinPointStudio`
  (gives Theme, the Pp* components, etc.).
- Its root must declare and only READ these (the probe sets them):
  `property string view` ("shot" | "session" | "watching" | "detail"),
  `property var readout` (THIS SHOT data), `property var cards` (session patterns),
  `property var source` (everything: watching, bookends, headerInfo, chains, expectations, thisShot…),
  `property var header` (= source.headerInfo), `property var detail` (the open condition).
  The root is laid exactly over the real panel body (1522×699 in the normal window). Draw your own
  background only if you need to — the stage card is behind you.
- Render: `$D/variant.sh <slot> $D/variant_<x>/Variant.qml <tag> [themes] [views] [normal|tall] [detailId]`
  slot = your letter (a, b, c — your own copy of the session + settings; never use another's).
  themes: 6 = Vector light, 7 = Vector dark, 12 = Folio light, 13 = Folio dark (default 7,13).
  views default shot,session,watching,detail. `tall` uses a 1680×2600 screen (panel 1522×2249) — use it
  for the full DETAIL page. detailId default over_the_top (also try early_extension).
  Output: `$D/out/<tag>_<aesthetic>_<light|dark>_<view>_window.png` and `…_panel.png` (panel crop
  with 16 px margin, opaque). A run takes ~60–90 s. Errors from your QML are printed with line numbers.
  ONE app instance at a time per agent; it is muted (no sound). Look at every render with Read.
- The data, dumped as JSON for reading: `$D/data/{readout,cards,source,header,detail,detail2}.json`
  (detail = over_the_top, detail2 = early_extension). Key fields:
  readout.conditions[] {id,name,state,stateKind fired|clean|notAssessable, strength 0-5,
    strengthExcess (×distance to corridor), valueText, corridorText, recurrence, tierTag
    pattern|watching|"clean all session", ticks[15]{state,selected}, tail, reason, firingsAfterText};
  readout.{headline,note,shotIndex,shotCount,firedCount,cleanCount,tailSummary}.
  cards[] {rankText,name,phase,measure,recurrence,fired,assessable,valueText,corridorText,
    directionText,trendText,trend,trendKnown,firingsAfterText,statePill,thisShot fired|clean,
    strength,fresh,reachesBall,rootHere,consequence,ticks[15],
    spread{bands[{lo,hi,f0,f1,grade}],faultLines[{f,value,text}],dots[{f,value,state,current,stack}],
    run[{fx,fy,state,current,readout}],median[{fx,fy}],medianNumber,currentNumber,medianText,
    caption,dirLeft,dirRight,normTag,axisLo,axisHi,shape,highMeans}} — f/fx/fy are already 0..1.
  source.watching[] {id,name,recurrence}; source.bookends; source.expectations; source.coverageLine;
  header.{countLine,shotLabel,reviewNote,stageLabel,reviewBadge}.
  detail {name, header (a card), history{columns[{label,median,q1,q3,values[{f}],fMedian,fQ1,fQ3}],
    yTicks[{f,text}],bands,faultLines,caption,unit}, causeHeadline, causes[{nodes[{name,kind,mark,
    state,statePill,evidence,recurrence,phase,measure}],links[{word,note,grade}],primary}],
    outcomeHeadline, effects[same], rivals[{text}], noCausesLine, noEffectsLine}.
  Read the JSON for exact shapes; the live objects are the same.
- The current implementation for reference (read, don't edit): src/Gui/diagnostics/
  (PpSessionDiagnosticsBody.qml, PpReviewShotStrip.qml, PpPatternCard.qml, PpWatchingRow.qml,
  PpConditionDetail.qml, PpCorridorStrip.qml, PpValueRun.qml, PpSessionHistory.qml, PpChainRail.qml),
  Theme at src/Gui/theme/Theme.qml, design system docs/design/pinpoint_qml_design_system.md (§13, §14).

## Rules
- Do NOT edit anything in the repo. Do NOT build. Do NOT commit. Work only in $D/variant_<x>/ and $D/out/.
- Never run the app except through variant.sh with your own slot. Never play sound.
- Iterate: render, look, critique yourself against the principles, fix. Expect 6–12 render cycles.
  Check both themes and both modes (6,7,12,13) before you finish, plus the tall DETAIL (both themes)
  and DETAIL for early_extension once.
- Final renders: tag `final_<x>` for 6,7,12,13 × all four views (normal), and `final_<x>_tall` for
  7,12 detail (tall). Delete your intermediate PNGs from $D/out when done (keep final_* only).
- Write `$D/variant_<x>/NOTES.md` (≤ 60 lines): the idea in two sentences; per view what changed
  and why; what information moved where; the proposed Theme additions (the role table, explained);
  anything you could not do.
