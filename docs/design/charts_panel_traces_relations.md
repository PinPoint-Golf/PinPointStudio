# Charts panel — Traces + Relations

**Status:** design chosen 14 Oct 2026 (Mark: "I like A and B and want both. The Reads are for another
day"). Not built. This document records the chosen design, how A and B combine, what the build needs,
and what is still Mark's call. The decisions in §8 are proposals until he confirms them.

**Where the work is** (local, `build/` is not in git):
- Signature review: `build/run-me/chart-variants/charts-round1b-signatures.pdf` (25 pages).
- Reference grids: `charts-round1b-analyse.pdf` (46 pages, every width and theme, with print) and
  `charts-round1b-replay.pdf`; real A4 pages in `print/`.
- Mocks: `build/run-me/chart-variants/qml/variant_{a,b,c}/` (Panel, Replay, Analyse, Print, `lib/`,
  NOTES.md, SIGNATURE.md), the session fixture `qml/fixture/` with its builder `build_fixture.py`, and the
  briefs `BRIEF.md` and `BRIEF_1b.md`.
- Renders: `/mnt/swingdata/scratch/typeset-harness/out/r1b_{a,b,c}_*`.

Read first: `docs/design/pinpoint_qml_design_system.md` §15 (the typeset system) and
`docs/design/stage_typeset_playbook.md` (the method). This is the Charts entry of that programme.

---

## 1. What the panel is for

Today's panel shows **one swing's** metric curves: one preset, overlay or split, a scrub playhead, a
sequence strip and summary cards. Nothing across the session, nothing compared, nothing related.

The new panel keeps that and adds the session behind it, in two voices:

- **Traces (A), the instrument.** The curve is the evidence. With the shots aligned on their P-positions,
  how does this swing's shape differ from my others, and where along the swing do they come apart?
- **Relations (B), the workbench.** Every metric is a variable read at its instant. Where does this swing
  sit among my swings, number by number, and which numbers move together?

**Reads (C), the coach's notebook, is parked** (Mark: "for another day"). It is one golf question at a
time: Sequence, Speed, Rotation, Wrist (with a wrist-style classification), Body and Tempo, each answered
in a sentence built from the data. Its mocks and notes stay in `qml/variant_c/`. Two of its ideas are
worth keeping in view: the one-line written answer as a Replay headline, and change-point detection
("the hips turned further from shot 4 on").

## 2. The views (proposed merge, decision D1)

A had This shot · Session · Compare; B had This shot · Session · Kinds. Each view answers one question
(playbook step 2), and A's Compare is about **this** shot, so it folds into This shot as a reference.

| View | The question | From |
|---|---|---|
| **This shot** | What did this swing's curves do, and how does it differ from my usual swing? | A's traces and Compare, B's "apart" ledger and profile |
| **Session** | What do my curves usually look like, and where do they part? | A |
| **Relations** | What moves together, and what drifts? | B |
| **Kinds** | What kinds of swing were there, and what tells them apart? | B, with A's shape groups |

Quiet PpTabs (underline, no capsule). Replay and the cast open on This shot. Kinds has no Replay form and
falls back to Session there.

### 2.1 This shot
- Answer head built from the data ("Out of sequence: the pelvis peaked late"), KeyFigures with B's place
  words ("82 mph, median 72, 3rd highest").
- **Traces:** shared-axis small multiples of the preset's windowed-mean curves, direct labels, peaks marked
  with their σ, the playhead, click or drag to seek. The sequence as one typeset chain, gaps on the arrows
  ("Lead arm — 58 ms → Club — 69 ms → Pelvis, late").
- **Against** (A's Compare, a quiet control): nothing, the session median, the most typical shot, the least
  typical, the previous or next shot, or "shots like this one" (the group fit of the shots shaped like it,
  warped onto this swing's own P-positions). Optional "this minus the reference" strip under each trace,
  with the largest gap marked: *when* this swing left your normal one.
- **Apart from the session** (B): the readings that put this shot past the session's whiskers lead
  ("Seven readings apart from the session"), then every reading the swing produced, each at the instant
  its card reads it, beside a box plot of the session with this shot ringed. A row opens its curve in place,
  with a box of all shots at each P-position. Folds below the traces; in print it is an index at the back.

### 2.2 Session (A)
- Every shot's curve for each metric of the preset: this shot full strength, the others the same hue
  faint, a middle-half ribbon and the median. Or the group fit (§4).
- **P-aligned time only** (Mark, 14 Oct: "I would prefer we just keep P alignment"). Every shot is
  warped onto the session's P-positions (§4.1), so timing drops out and the curves compare shape. There is
  no clock-time view and no side-by-side "two clocks" (round 1b's `sigTwoClocks` is dropped). The timing
  the warp removes is kept as one quiet annotation under each P tag: the middle half of when that
  P-position happened across the shots, in ms from impact.
- **Where the curves part:** the instant of the widest middle half (edges excluded), named in the head.
- **Facets:** by a fault that fired on SOME shots (at least 3 each side; a fault that fires on every shot
  is never offered), by session half, by curve shape, by club when the session is mixed. Side by side from
  22 em a panel, else overlaid with the other group dashed. The co-occurrence caveat is always said.
- **The sequence across the session:** an order barcode (rank per shot, late ranks marked) and a
  peak-time dot plot on one ms axis.
- Up to three data-backed questions ("which shots lie outside the middle half where the curves part?").

### 2.3 Relations (B)
- **A scatter of any two readings** (quiet Up / Across pickers that open in place; no menus or dialogs),
  box plots on both edges, the least-squares line (solid when it clears chance, dashed inside chance).
- **"Moves with" ledger**, ranked by |r|: r, its 95 % interval drawn on −1…1, n, the slope in units, and
  the pair as a sparkline. The chance line said out loud: "|r| under .51 is chance at n = 15"; "4 of 67
  readings clear chance; chance alone would clear about 3". Every line ends: co-occurrence, not a cause.
  Same-quantity pairs (clubhead speed / club ω, pelvis turn / signed) are labelled, not ranked as findings.
- **Split by a fault:** the same pair as small multiples, fired on N / clean on M, each with its own r.
- **The family matrix** and **drift** (each reading against shot order, run chart plus box plot; drifts
  that clear chance are surfaced, e.g. shaft lean at P7, r −.88).

### 2.4 Kinds (B + A)
- **Say how weak the structure is first.** k-medoids on all complete readings; if the silhouette is low
  (.03 on 4 July), say "no structure" and use the one clean break instead: the largest gap in the most
  separating reading (an 8 mph clubhead-speed gap: 6 faster, 9 slower).
- A's curve-shape groups ("shaped like shot 12 · 11", "like shot 3 · 4") are a second grouping here.
- Members, the separation table (each kind's box on one scale, d against the d chance gives at these
  sizes), and **each kind's pooled curve** (§4).

## 3. Registers

- **Replay** (glance beside the video, and the cast). This shot: answer head, KeyFigures, the traces with
  the readings AT THE PLAYHEAD ("now"); click or drag seeks through `shotReplay.seekToUs`. Session: band,
  median and this shot, "now" against "typical", the lead reading at the latest P-position the playhead
  has passed. Default preset Club delivery (Club & speed merged into it, 16 Oct; Replay shows its curves and only path, face, attack angle, low point and dynamic loft). The cast takes no clicks, shows a running head instead of tabs,
  and zooms as a whole.
- **Analyse** (read and interact). Default preset Kinematic sequence. At wide the head and controls move
  to a left rail so the figures get the card's full height. Hovering a shot lights it in every block;
  clicking a mark drills in; questions answer in place.
- **Print** (A4 portrait and landscape): masthead; This shot (traces, sequence, apart ledger); Session
  (curves, where they part, sequence across the session); Relations (scatter, ledger top 12, matrix,
  drift); Kinds; "asked of the data" footnotes; the index of every reading at the back. Figures numbered,
  no affordances, nothing split across pages. Round-1 lengths: A 6–8 pages, B 11–12; combined, expect
  about 15.

## 4. The group fit (Mark, 14 Oct)

Mark: "fitting curves to shots that have a similar pattern — that way we have a lot more data and this
will help resolve areas where the data is sparse (ideally we fix the sparsity at source, but the
generalised chart is still useful)."

- One curve per group, fitted to the **pooled valid raw samples** of every shot in the group, P-positions
  aligned (the same warp as §2.2). Local-linear fit with tricube weights over ±20 ms (the windowed mean's
  40 ms support); B used a 12–60 ms adaptive window. One engine serves Session (groups from facets or
  shapes), Kinds (groups = kinds) and This shot ("shots like this one").
- **Coverage strip** under each plot, one hairline per shot: it shows what the group filled. On 4 July,
  shot 8's chest-speed gap (−13 to +20 ms around impact) is spanned by the other shots.
- **Honesty rules:** the curve is the group's, never a shot's. No figure for any shot is read from it. It
  is labelled with its group and n ("7 shots with Returns steep"), left blank where fewer than 3 shots
  contribute, and refused for a group under 3. Band choice: decision D2.

### 4.1 Alignment: every cross-shot analysis runs on P-aligned time (Mark, 14 Oct)

Mark: "when working with multiple shots they should be aligned to P positions before doing the next
level of analysis." Both mocks already do this; the build makes it the rule for the envelope, where the
curves part, the group fit, shape groups and the kinds' curves.

- **Canonical P-times:** the session median of each P-position's time from impact, for every P present on
  at least half the shots.
- **Anchors per shot:** the P-positions the shot and the canonical ladder share. An anchor is dropped if
  it would run time backwards, if its confidence is below the gate, or if its segment length is an outlier
  for the session (06-11 s1's P8 3.8 ms after impact; shot 9's Finish at +965 ms). Neighbours then bridge it.
- **Dilation:** the mocks use a piecewise-linear warp (each segment stretched uniformly, a pure shift
  outside the first and last anchors). **Proposed for the build: a monotone cubic (PCHIP) through the same
  anchors**: it still lands every P exactly, but the stretch varies smoothly, so curves get no kinks at the
  P-positions. Not free-form dynamic time warping, which aligns noise and can hide a real timing fault.
- **Only the time axis is warped.** Values keep their units, nothing is differentiated in warped time, and
  every number shown to the reader is unwarped back to the shot's own clock.
- **Impact only:** one anchor gives a pure shift: the shots are lined up at impact and nothing is dilated.
  The panel says so ("only impact is known: shots are lined up at impact, tempo differences stay in the
  curves"). Better, in order:
  1. landmarks found in the curves themselves (the club angular-speed zero crossing at the change of
     direction, the onset of motion, the hand-speed peak), each gated on its data, each a real anchor;
  2. one tempo scale per shot about impact, fitted to the median curve, labelled as fitted, because it can
     fold a timing difference into a shape difference.

## 5. Colour and marks

The rule (now in spec §15.4 item 1, written after round 1 drew every curve in ink):
- Anything that stands for a **metric** is drawn in `Theme.metricColor(key)`: curves, raw dots, bands,
  session boxes, swatches beside names, axis titles. One metric across many shots stays one hue: other
  shots faint, median / fit / ghost told apart by weight and dash, never a second hue.
- Anything that stands for a **shot** is ink: scatter points, strip cells, the picked-shot ring, coverage
  hairlines, the order barcode. The playhead is ink.
- **Warn = a mark plus a word**, never colour alone: a ring and "late" on the late peak, a tick and "fired"
  on a fired mark, an underline and "out of its corridor" on a value. It must read in greyscale, because
  some metric hues sit near warn (pelvis crimson, lead knee ochre).
- Proposed Theme roles (from both designs' NOTES): envelope α 0.12 light / 0.17 dark, other shots α 0.3,
  the dash set (fit long dash, ghost [4,3], other facet [2,2.5], like-this-one [5,3]), coverage pitch,
  box row 16 / body 9 / fill α .16 (print .12), swatch 7, fit line ink2, chance dash ink3 [4,3], fired tick
  warn 1.5 × 4. They go into Theme's `typeset` block, never into the panel.

## 6. What the build needs (model requests)

**The rule that shapes all of it:** no JavaScript logic in QML. Every number the mocks computed in JS
(`variant_a/lib/traces.js`, `variant_b/lib/Stats.js`) moves into C++ with tests.

1. **SessionSeriesModel (new, C++).** The live panel has only the picked shot today; the mocks read a
   2.5 MB fixture. Per shot: `{ ordinal, shotId, swingDir, club, impactUs, phases[], metrics{ key:
   { t_us[], value[], valid[], sigma, sigmaKind, phaseSamples[] } }, kinematicSequence, fired[] }`, plus
   `conditions{ id: { name, group } }`. It must load off the GUI thread and hold only the series, phases,
   sequence and diagnostics, never pose or the rest of the 20 MB analysis blob (the 28 MB document froze
   the GUI before). It must be cached, update when a live shot lands, and be sized for 50+ shot sessions.
2. **Session maths (C++, with tests),** probably ChartMetrics plus a new session-statistics class:
   - A: `canonicalPhases`, `warp` / `warpMs` / `unwarpMs`, `envelope` (type-7 quantiles, at least 3
     shots), `partAt`, `pooledFit` (the §4 engine: keys, shots, window, align, half-width 20 ms, tricube,
     min 3 shots, min 8 samples → t, v, sd, se, nShots, nSamples), `coverageRuns`, `shapeSplit` (two
     medoids, each at least 3), `typicalShot` / `leastTypicalShot`.
   - B: `readings[]` (`key@phase`, values per shot, box stats, ranks, side, fired), `corr` with Fisher
     interval, `rCritical(n)`, `pair`, `drift`, `kinds` (k-medoids, silhouette, gap split, separation table
     with d and `dCritical`), and a catalogue `sameQuantityAs`.
3. **Body and controller API:** a `view` property; the reader's choices (preset, window, alignment,
   band, facet, reference, difference, picked readings, open row) persisted on the controller so they
   survive a mode switch and the stage rebuilding; `shotRequested(swingDir, shotId)` so a figure can move
   the carousel (drill-in from a figure is wanted; a shot list in the panel is not); `interactive` for the
   cast. Library: `PpPager.title` and `PpRunningHead` title (both say "Session diagnostics" today), and
   PpPageKey kinds `box` and `fit`.
4. **A diagnostics link** from a condition to `{ metricKey, phase }`. The mock joins conditions to readings
   by equal values, which is fragile.

## 7. Defects the mocks found (not fixed)

| Defect | Effect | Where |
|---|---|---|
| `cardSpecFor.readAt` defaults to impact for readings taken only at address (stance, flare, ball position) | the profile reads them at the wrong instant | metric catalogue |
| `formatBare` rounds to whole units | tempo 2.6:1 prints "3", low point −1.4 in prints "−1", backswing 0.73 s prints "1" | formatting |
| `toeLineAngle` wraps across ±180° | 176.6 against −179.5 in one session | metric producer |
| Diagnostics `values[].lo/hi` are not the corridor | corridors cannot be drawn on the profile | diagnostics model |
| `sequenceTable.outOfTurn` marks both ends of every inverted pair | all three of shot 8's placed segments would be warn (D4) | ChartMetrics |
| Clubhead speed is bridged after impact, so the 40 ms mean is one-sided there | windowed peak 75 mph reads below the P7 value 82 mph | series validity |
| X-factor stretch reads 0 at the top by construction | the stretch is meaningless at P4 | metric producer |
| Shot 9's Finish at +965 ms (others about +275); Address equals Takeaway on most shots | phase ladder | timeline |
| `ballPosition` at P1 measured on 6 of 15; `shoulderPlaneAngle` at P4 missing on 8 of 15 | sparse readings in the profile | producers |
| Chest (thorax) angular speed is bridged from about −13 to +13 ms around impact on every shot; posture curves are bridged after impact | the single-shot curves have holes exactly where the sequence is read (the group fit spans them) | series validity |
| 06-11 swing 1: P8 lands 3.8 ms after impact | the sequence window ends at the ball and its edge value gets placed | phase ladder |
| A two-camera bound sits at the edge of the first raw crossing hole only ("reads only the first hole") | 06-11 s1 gains a "no later than 127 ms before impact" chest bound after the 14 Oct fix | sequence pair route, see `thorax_pair_20261014.md` §12 |
| Vector capitalises units in labels ("°/S") | unit text changes meaning | Theme caps applied to units |
| The swing's `truth.json` says DRIVER while the swing records 7 IRON | club labels disagree; the swing's club is shown | capture labels |
| A condition has no link to the metric and instant it reads (also a §6 request) | the mocks join conditions to readings by equal values | diagnostics model |
| Five conditions fire on every shot of 4 July (early extension, hips under-rotated at impact, over the top, pelvis thrust in the backswing; reverse spine on 14 of 15) | they cannot split a session, so they are never offered as facets; worth checking whether their corridors suit this golfer | diagnostics norms |

Data, not defects: on 4 July the pelvis peaks after impact on 14 of 14 placed shots and the chest is never
placed; shot 6 orders club, pelvis, lead arm, and on one shot the lead arm peaks 70 ms after impact. The panel says it neutrally: "may be the golfer (hips held at impact, then catching up) or the
two-camera measurement; it cannot yet tell them apart" (see `docs/research/data/kinematic_sequence/
thorax_pair_20261014.md`). IMUs (vest + belt) will settle it.

## 8. Decisions for Mark (proposals)

| | Decision | Proposal |
|---|---|---|
| D1 | Views | This shot · Session · Relations · Kinds, with Compare folded into This shot as "Against" (§2) |
| D2 | Group-fit band | the pooled spread (±1 sd of the pooled samples), as both mocks drew it; the fit's own se available |
| D3 | Default grouping | in Session: none (all shots); facets offered by fault, half, shape. In Kinds: the mining result |
| D4 | Out-of-order marking | warn only the segment that peaked late, not both ends of the pair (fix `outOfTurn`) |
| D5 | Profile size | all readings, grouped by family, boxes muted at rest and coloured on hover or when apart (B's suggestion, because 68 coloured boxes are busy) |
| D6 | Replay Session | follows the playhead (both mocks built it so) |
| D7 | Kinds with no structure | say so, then the largest gap in the most separating reading, as B did |
| D8 | Print | one combined report, sections in view order, the index at the back |
| D9 | Alignment (§4.1) | DECIDED (Mark, 14 Oct): P-aligned time only, no clock-time view. Proposed: PCHIP warp with gated anchors; impact-only falls back to curve landmarks, then a labelled tempo scale |

## 9. How this was made

Inventory and data dump, then three directions by three agents in parallel (A Traces, B Relations,
C Reads), each with This shot / Session and Replay / Analyse / Print, mocked over the live panel with the
typeset harness and fed a real 15-shot session fixture. Mark asked mid-round for the group fit. Round 1
drew every curve in ink because the brief over-read "colour means out"; Mark caught it ("color has been lost
for the curves"), and round 1b restored `Theme.metricColor`, made warn a mark plus a word, and added
signature renders because the thumbnail grid did not let him tell the designs apart. Lesson for the
next panel: brief what the shipped panel already does that Mark asked for, and show signatures at his
window size, not only a grid.
