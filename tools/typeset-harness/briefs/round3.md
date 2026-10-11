# Round 3: one aesthetic, three registers, with interaction

Mark, after round 2: "I absolutely agree … replay needs to be short and to the point as it will likely be
on screen for the duration of a replayed screen, whilst analyse needs to be insightful and lead to in-depth
review when needed. I would hate to lose any of the incredible thinking that has gone into all 3."
Then: "yes go ahead, and capture and cast share replay — can we fold in interactivity?"
Earlier, on interactivity: "exploit the interactivity of an app display … sensitively — getting into the UX a
little and guiding the user to understand the data and ask sharper questions."

Read first: BRIEF.md (round 1), BRIEF2.md (round 2), RULES.md (the eleven house rules, now binding), and
the three round-2 files variant_{a,b,c}/Variant.qml with their NOTES2.md.

## The decision: one aesthetic, three registers
A REGISTER is how much the panel says and how it says it, chosen by the situation (the session mode), not by
the view. Every register uses the same blocks, the same type roles and the same encodings. Moving from
Replay to Analyse must feel like zooming into the same page, never like opening a different app.

| | REPLAY · glance | ANALYSE · read | PRINT · report |
|---|---|---|---|
| Used in | Replay, Capture (live), the second-screen cast | Analyse | the printed report (PDF later) |
| Job | on screen for the whole replay: the answer in one look, quiet beside the video | explain, then lead into depth | stand alone with nothing to click |
| Lead voice | C · figures | A · ledger, with B's sentences for the "why" | B · editorial |
| This shot | C dot plot of the FIRED conditions only, values beside; the clean ones as one quiet count line | C dot plot (all conditions) + A ledger rows for the fired ones (corridor, out by, strip) | A table + B standfirst |
| Session | one sparkline row per pattern: name, 15-shot strip, this shot's value | A ledger table; B's direction sentence as line 2; a row opens Detail | B numbered entries with marginal sparklines |
| Watching | C barcode | C barcode + this shot's values | C barcode |
| Detail | not offered (a glance never drills) | B feature, C figures, A key figures | B feature |

Replay has the shortest height of all: in Replay the carousel takes space, so the panel is ~734 × 611
at half and ~1522 × 603 at full. The cast is a separate window, about 1040 × 760, read from a distance:
its viewing-distance zoom scales the WHOLE surface (like browser zoom). That is allowed. Type growing
INSIDE a panel is not.

## Interaction: sensitively
Principles (binding):
1. QUIET BY DEFAULT. Nothing moves or appears until the reader moves. Hovering reveals in place, into space
   that was already reserved: no layout jump, no floating tooltips, no pop-ups, no menus, no native dialogs
   (house rule: everything in-app).
2. ONE FOCUS AT A TIME. A hovered or picked thing is shown ONCE in every block that contains it ("brushing"):
   hover shot 12 in any strip and shot 12 lights in every strip, dot plot, run chart and barcode on the panel.
   The picked shot's mark (from the carousel) stays; a hovered shot is a lighter, second mark.
3. EVERY CLICK GOES SOMEWHERE REAL, through the real body (root gets `property var body`, the live sdBody):
   - a condition → `body._openDetail(id)` (Analyse only); BACK → `body._closeDetail()`;
   - a shot → `body.shotRequested(swingDir, shotId)`: the carousel picks it and the whole panel re-reads it;
   - focus → `body._declareFocus(id, on)`.
   In Replay, a click on a condition switches to Analyse at that condition (`SessionMode.enterAnalyse()`, then
   `body._openDetail(id)`). That is the glance's only door into depth.
4. MOTION IS TIME, NOT DECORATION: Theme.durationFast for hover, durationNormal for a change of focus;
   reduceMotion is respected (Theme durations already go to 0).
5. SHARPER QUESTIONS (Analyse only). Where the data supports it, a view ends with at most three questions the
   reader can ask with one click. Each is computed from the data shown, phrased as a question, and answered
   IN PLACE by highlighting or filtering what is already on the panel, or by stepping the carousel. They are
   never a coaching verdict, and they say co-occurrence, never cause (house decision: "co-movement, never
   cause"). Show a question ONLY when its answer exists. Candidates (pick the honest ones, add your own):
   - "Which shots were clean?" (a pattern with clean shots) → light those shots in the strips; offer
     "step through them" (carousel).
   - "What fired with it?" → the conditions whose fired shots overlap this one's most, as "fired together on
     9 of its 10 shots" (counted from readout.conditions[].ticks). Light them in the dot plot.
   - "Is it getting worse?" (only when trendKnown) → light the first and last third of the run.
   - "Worst against best": the bookend shots, two clicks to put each in the carousel.
   - "Why might this happen?" (Detail) → expands the likely-driver path in place.
6. PRINT has none of this: affordances become cross-references (house rule 10). The questions may appear as
   printed footnotes ("Clean on shots 2, 9 and 11"), because the answer itself is content.
7. Interactive states must be RENDERABLE: each register declares `property string demoState` and a short
   list of named states (e.g. "hoverShot12", "question-clean", "question-together") that put the panel in that
   state statically. The harness renders them for the review PDF. Mark will also click the real thing.

## A design problem for this round: fault-forward figures (Mark, mid-round)
"in figures the detail view has a lot of wasted space when the characteristic is a fault (the below the line
space is dead). bear in mind that users will drill into faults not things that are going well (!)"
The same dead band is why C's Session tiles at third width show only 2–3 patterns above the fold. Today every
figure gives the corridor (pass zone, ideal/good/watch) its true share of the axis. For a fault, where every
shot sits past the fault line, that is most of the chart, and it is empty.
Design the figure grammar for the case people actually open: a fault.
- The axis should spend its height where the shots are. Options to weigh (choose, and say why in NOTES):
  (a) a clearly marked BROKEN axis: the corridor collapses to a thin labelled band below the fault line;
  (b) a COMPRESSED corridor zone: a linear axis above the fault line, with the corridor squeezed into a fixed
      small share, its scale marked as different;
  (c) plotting DISTANCE PAST THE FAULT LINE, so 0 is the line, using the measure's own units.
  Whatever you choose must stay honest. A break or a compression is always drawn and labelled, never hidden,
  and the true values stay readable.
- Turn the dead space into the INSIGHT it represents: the distance the golfer has to travel. For example,
  "shot 8: 9 past the fault line, 13 to get back inside the pass edge", drawn as a measured gap or arrow,
  so the empty zone becomes the target.
- The same grammar must still work for a CLEAN condition opened from the index, and for a two-sided corridor
  (faults on either side). Give the clean case the economical layout instead.
- Apply it to every figure block that has a corridor: Detail's spread, run and history; the Session small
  multiples; the Replay sparklines.

## Architecture: a block library and three register files
```
$D/lib/      the blocks: ONE file per block, each reads the shared roles and takes plain data.
             Roles.qml: the role / tier / flow tables (per aesthetic, with print overrides), size classes,
             and helpers (em, sizeClass(width), columns(width, minEm), ink colours) — the Theme proposal.
             Brush.qml: the shared focus state (hoveredShot, pickedShot, hoveredCondition, highlightShots[],
             highlightConditions[]); every block reads it.
$D/reg/Replay.qml   Analyse.qml   Print.qml   — compose blocks per view; each obeys the round-2 contract
             (view, readout, cards, source, header, detail, medium, printOrientation, printPage,
             printPageCount) PLUS `property var body`, `property string demoState`, `property string surface`
             ("panel" | "cast").
$D/reg/Panel.qml    picks the register from SessionMode.mode (capture/replay → Replay, analyse → Analyse);
             used by the live try-out.
```
Blocks come from the variant that did them best, and keep its thinking:
- From A: LedgerTable (the fold-don't-drop table and its column tiers), IndexList (leader dots),
  KeyFigures, the print pagination (whole rows; add repeating heads "continued").
- From B: Standfirst / Masthead (sentences built from data), EntryFlow (run-in head entries),
  FeatureHead (detail opening), PathChains (typeset arrow chains with footnote marks), the page packer.
- From C: DotPlot (common corridor-normalised scale), SmallMultiple, Barcode, RunChart / SpreadStrip /
  HistoryStrip (one figure grammar), CauseGraph + its outline fallback, page keys.
- Shared: ShotStrip (the 15-shot strip, ONE drawing used everywhere), RunningHead/Folio, Tabs, Notes.
The encodings are reconciled ONCE in the library: the same orange for "out", the same strip, the same mark for
the picked shot, the same marker shape per theme, the same chart wash. Where A, B and C disagreed, use
RULES.md and record the choice.

## Harness v3
`$D/variant3.sh <slot> <file.qml> <tag> <layout> [themes] [views] [detailId] [tall|normal] [mode] [states]`
- layout: full | stage | half | third | cast | print-portrait | print-landscape
- mode: analyse (default) | replay. The probe switches that mode's real arrangement; replay keeps the carousel.
- states: comma list, which sets `demoState` before each grab; the file name gains `_<state>`.
- The root also receives `body` (the live sdBody). Clicks you make in a render run would really move the
  carousel; in renders use demoState, not real clicks.
- Output names as before: `<tag>_<layout>_<aesthetic>_<mode>_<view>[_<state>]_{window,panel}.png`.

## Rules (unchanged)
No repo edits, no builds, no commits, no sound; own slot only; one app instance at a time; delete
intermediates; never ellipsise; RULES.md binds.
