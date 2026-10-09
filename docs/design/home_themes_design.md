# Home screen themes — design

Status: in the app (home screen, YOUR SWING), 9 Oct 2026. The prototype in tools/themes/ is the
reference the C++ is tested against.

## Purpose

The home screen tells a golfer, in their own words, what their swing is doing. It does not show
metrics, condition names, corridors or the causal model. Those stay one tap down, for the golfer
who wants them and for their coach.

The themes are found per golfer, from that golfer's own swings. They are not a general rule
applied to everyone. Where the golfer's swings disagree with the authored model, that is reported
as a finding about the golfer, or about the model, which may be wrong.

## Two layers

Faults that move together are not the same thing as faults that are present. A fault seen on every
swing at a steady size never varies with anything, so it never forms a theme, however big it is.
The summary therefore has two layers, kept separate.

**1. What we see most** — the faults present on most swings.
- Source: each session's ledger (`diagnostics.json`), fired / assessed per condition.
- Rules:
  - A session counts for a fault when it judged it on at least 8 swings.
  - The fault is present when the latest such session saw it on at least half of them.
  - Ranked by a recency-weighted share: each older session counts 0.75 of the next.
- Trend: "easing" / "growing" when the last two sessions differ from the earlier ones by more
  than 15 points.
- At most five lines. Each is said as a golfer would say it ("you stand up through the ball"),
  with how often ("on almost every swing") and in how many sessions.

**2. What goes together** — groups of measures that rise and fall together across the golfer's
swings.

Told only as firmly as their stability allows:

| Tier | Rule (Tucker congruence) | Wording |
|---|---|---|
| firm | bootstrap p05 ≥ 0.84 and leave-one-session-out min ≥ 0.85 | plain statement |
| probably | bootstrap median ≥ 0.75 and leave-one-session-out median ≥ 0.9 | "Probably:" |
| possibly | bootstrap median ≥ 0.7 and leave-one-session-out median ≥ 0.85 | "Possibly:" |
| — | anything less | not shown |

Wording rules:
- Co-movement is said as co-movement: "On swings where your hips slide toward the target, your
  lead knee goes with them". Never "because" or "causes".
- Each theme line names the phase where it starts ("It starts in the backswing"), from its
  earliest member. That is the "where to start" a golfer can act on.
- Each line ends with its trend: "may be easing", "may be growing" or "no clear change yet".
  Session means carry camera drift, so trends are worded tentatively.
- Thin evidence is said as thin. "Nothing yet — your faults don't rise and fall together clearly
  enough to say" is a valid summary.

Narrative beyond these templates is deferred.

## Method (layer 2)

All of it is numpy only and hand-written, so it ports to C++ directly.

1. **Table.** One row per swing, one column per measure. The values come from the ledger rows:
   since schema 4, every measure behind a row, on clean swings as well as fired ones.
   - Drop measures read on less than 60% of swings.
   - Drop swings reading less than half of the well-covered measures. A capture hole otherwise
     forms a component of its own.
2. **Orient.** Each measure is oriented so that + means more of the fault: a ceiling corridor
   means high is the fault, a floor means low is, and a one-tailed signal names its tail.
   Two-sided measures keep their raw sign.
3. **Centre per session.** Subtract each session's median and scale by the pooled
   within-session spread (MAD × 1.4826). Then winsorise at ±4. This removes camera, club and
   day-to-day drift.
4. **Impute.** Missing values are filled by EM-PCA. Check against zero-fill, and distrust a theme
   that does not survive it.
5. **Find the themes.** PCA, keeping the number of components that parallel analysis supports
   (95th percentile against column-shuffled data). Then varimax rotation.
6. **Cross-check.** Average-linkage clustering on 1 − |Spearman ρ|. A theme both methods find is
   the trustworthy kind.
7. **Stability.**
   - Bootstrap of 500 resamples, swings resampled within each session.
   - Leave one session out.
   - Collapse measure families (the same metric read at different phases, which load together
     trivially).
   - Run with other sessions added.
   - Components are matched by the Hungarian method on congruence.
8. **Name.** Each theme is named from its two strongest members, in the golfer words the pack
   carries for each measure and direction (`golferHigh` / `golferLow`). A second member of a
   family already named (the same quantity at another moment) is skipped, so a theme never says
   one thing twice.

## Coach layer: model comparison

`theme_network.py` builds the pairwise picture underneath:
- session-centred Spearman correlations, with FDR control at q 0.05;
- Ledoit–Wolf shrinkage partial correlations;
- within-session bootstrap sign agreement ≥ 90%, and the sign held when any one session is left
  out.

Each authored edge is then classified, measure pair by measure pair:
- found;
- found with the opposite sign;
- confidently absent (CI inside ±0.3);
- unclear;
- structural (the same metric at two phases).

Partial correlations alone cannot test an edge, because conditioning on everything removes
mediated paths. Marginal correlations are used too.

This layer is for the golfer and coach, not the home screen.

## First run: Mark, 135 swings, 5 sessions

The output, as the prototype writes it:

```
What we see most
  • You stand up through the ball — on almost every swing (4 of 4 sessions)
  • Your trail leg straightens going back — on almost every swing (4 of 4 sessions)
  • You lean toward the target at the top — on almost every swing (4 of 4 sessions)
  • Your hands come over the top on the way down — on almost every swing (4 of 4 sessions)
  • Your hips haven't turned enough by impact — on most swings (4 of 4 sessions; growing)

What goes together in your swing
  • On swings where your hips slide toward the target, your lead knee goes with them.
    It starts in the backswing. (may be easing)
  • Probably: On swings where your lead arm folds after impact, you turn your chest further into
    the finish. It starts around impact. (may be growing)
  • Probably: On swings where your head comes up going back, it stays up coming down.
    It starts in the backswing. (no clear change yet)
  • Possibly: On swings where you bend over more at address, your hips turn further going back.
    It starts at address. (no clear change yet)
```

What it established for the design:

**One firm theme from 135 swings.** It is lateral slide: pelvis sway back, down and at impact,
plus lead-knee drift at the top and at impact.
- Bootstrap congruence median 0.93, 5th percentile 0.85.
- Leave-one-session-out min 0.92.
- Clustering recovers the same five measures (Jaccard 1.0).
- Persists with nine older sessions added.

Expect few firm themes per golfer. That is honest, not a failure.

**The largest component is unstable but coherent.** It explains 13% of variance (bootstrap median
0.71, leave-one-session-out median 0.89). Its members are:
- more forward bend at address;
- a deeper hip turn at the top;
- the trail knee losing flex;
- the pelvis moving toward the ball going back;
- a less open pelvis at impact;
- standing up.

This is the golfer's own reading of his swing: rotation lost through a slide that costs posture
from the start. It is shown as "possibly", and is worth watching as sessions accumulate.
- Several of its measures are read "from P1", so they share the address reading, which partly
  couples them mechanically.

**Early extension does not form a theme.** It is present on nearly every swing, at a steady size.
Its two measures (pelvis toward the ball, spine-angle loss) trade off slightly (ρ −0.28). That is
why layer 1 exists.

**The authored model is mostly not visible swing to swing.** Of 100 authored measure pairs both
read:
- 3 found direct and 1 found indirect;
- 4 found with the opposite sign;
- 63 confidently absent;
- 15 unclear;
- 11 structural.

Over the top ↔ early extension (pelvis) leans positive (ρ +0.24) but does not pass FDR.

Swing-to-swing co-movement is not the only evidence a causal edge can have. A habit can be caused
without varying. Declared-focus sessions (the panel's "moved together" grade) and drill sessions
are what will test the edges.

## In the app

Built 9 Oct 2026. What was planned under "Before it goes into the app" and how it landed:

- **Phrases live in the pack.** `core.json` carries `golfer` on every detectable fault and setup
  condition ("you stand up through the ball") and `golferHigh` / `golferLow` on every measure that
  produces readings. Measure phrases are COMPARATIVE ("your hips slide further toward the target
  coming down"), because a theme compares a golfer's swings with each other, and each stands
  alone. The pack validator warns `golferPhraseMissing` and `golferPhraseWording` (causal words,
  jargon such as pelvis / thorax / P-numbers, digits and units); `core_pack_test` holds the shipped
  pack to zero of both. Warnings, not errors, so an older user pack still loads.
- **Where it runs.** `src/Analysis/swing_themes.h` is the pure reduction (both layers, and the
  sentences); `src/Diagnostics/swing_themes_pack.h` marshals orientation, swing position and phrases
  from the pack. `WorkOnsController` runs it once its catch-up has every session's record current,
  off the GUI thread with the bootstrap replicates spread over a thread pool, and never while a
  session is live. The result is `<athlete>/swing_themes.json`, fingerprinted by
  `kThemeRuleVersion`, the content stamp and every session's work-ons fingerprint, so it is
  recomputed only when a session changes. The words are made when the list is published, so a
  phrase edit needs no recompute. Cost on the library (133 swings × 46 measures, k = 9): about
  2 s optimised, 13 s in the Mac Debug build.
- **Minimum data.** Under 60 swings or 3 sessions (after dropping sparse swings), layer 2 says
  "Not yet — it takes about 60 swings over 3 sessions to see what goes together (so far: N swings
  over M sessions)."
- **Trends.** Still uncentred session scores, so still worded "may be". A trend is said only when
  the slope is over 0.1 within-session SD per session AND at least 2 standard errors from zero,
  the error taken from the sessions' own scatter about the line. Session means on the library
  move ±0.8 SD from session to session — drift, far above their sampling error — and the slope
  gate alone read that drift as a trend (the slide theme: slope −0.21, t −1.0, now "no clear change
  yet"; arm-and-finish +0.30, t 3.3, still "may be growing").
- **Display.** "YOUR SWING" is drawn to be looked at before
  it is read (Mark rejected a first version of bulleted sentences as "really boring"):
  - two cards side by side — **What you do well** (green: a tick, the strength, how often it is in
    the ideal range, one pip per session) and **What needs work** (amber: the fault, a ten-step
    "how often" meter, a growing / easing chip, one pip per session that saw it), five each at most;
  - **What goes together** below, one row per theme (four at most, firm first): a signal-strength
    mark for CLEAR / LIKELY / POSSIBLE in place of the "Probably:" prefix, the two halves joined by
    a connector ("When your hips slide …" / "your lead knee caves in …"), and a six-stop swing
    timeline (Address · Back · Top · Down · Impact · Finish) marking where it starts.
  Superseded the same day by the focus-led layout below; the cards and the goes-together rows
  carried over.
- **The home screen is a coaching page; the rest is Swing diagnostics.** Two polished lists of the
  same faults ("what needs work" and WORK ONS) read as a contradiction, and five faults on "almost
  every swing" give a golfer no order. Mark's call, 9 Oct: one focus to the fore, the technical layer
  one tap down. Home, top to bottom: YOUR SWING → **Your focus** → What you do well (3) | Next on your
  list (4) → "Swing diagnostics →". The Swing diagnostics screen holds FAULTS (the work-ons card,
  with its counts, corridors, drills and session links) and What goes together.
- **The focus (rule v1).** The needs-work faults are grouped by the first drill the pack authors
  for them (a fault with none is a group of its own), and the groups ranked: earliest in the swing
  first (a condition's swing position is the earliest of its measures'), then a group with a drill,
  then the one covering more faults, then the higher share. Starting at the earliest point is a
  coaching convention, said as one ("Picked first: it comes earliest in your swing and covers two of
  the things on your list.") — an earlier fault changes the positions later ones are read from; no
  causal claim is made. The card shows the drill as its title, what to aim for (`golferWell`), how
  often each covered fault happens now (meter + pips), why it matters (`golferWhy`, a new pack field:
  what the fault costs the shots, in golfer words) and the drill's instruction. "Next on your list"
  is the rest in the same order. On Mark's data: Trail hip load and hold (trail knee straightens +
  sway, both in the backswing), then over the top, reverse spine at the top, early extension, hips
  at impact. A related theme is NOT attached to the focus yet: the firm slide theme shares sway's
  measure but moves the opposite way.
- **What you do well (rule v1).** A golfer is told what is RIGHT too, and only what is true:
  - a strength is a fault condition whose readings sit INSIDE THE IDEAL CORRIDOR, by value, on at
    least 85 % of swings (recency-weighted over sessions that judged it on 8+ swings; 80 % in the
    latest; 30+ swings over 2+ sessions). "Never fired" is not enough: Mark's lag sits below the
    ideal band on most swings yet casting never fires, because the fault line is further out;
  - never a conjunction (detection All) — the absence of "A and B" says nothing about A or B;
  - prominence Occasional or above, ranked by prominence, then share;
  - never the same quantity as anything in "what needs work" or as any member of a theme the page
    shows (family = metricKey, with a signed series the same family as its magnitude — otherwise
    "your hips keep turning through impact" sat beside "your hips haven't turned enough by impact");
  - worded from the pack's `golferWell` (what a golfer in the ideal range does, never "don't").
- **Parity with the prototype.** Both draw their random numbers from mt19937_64 with one seed per
  replicate (`tools/themes/theme_rng.py` is bit-identical to `DetRng`), so
  `swing_themes_golden_test` holds the C++ to the Python on a copy of Mark's five library ledgers
  (`src/Analysis/tests/data/swing_themes/`): every number to 1e-9 in practice, tiers, members and
  sentences exactly. Regenerate the fixture (its README) whenever the pack's orientation, reducers
  or phrases change.
- **Drill sessions.** Still the fastest way to more themes and to testing the model's edges.

## Tooling

`tools/themes/`, run with `/opt/homebrew/bin/python3 -I -B`. scipy is not importable under `-I`,
so everything is numpy.

| File | Does |
|---|---|
| `theme_data.py` | The swing × measure table from the ledgers, and the session centring |
| `theme_pca.py` | Themes, with stability, clustering cross-check and naming (`--out` dir → `themes.json`, loadings, stability, session scores) |
| `theme_network.py` | Pairwise links and the model comparison (`--out` dir → `links.csv`, `model_comparison.csv`) |
| `theme_summary.py` | The two-layer plain-language summary (`--themes <themes.json>`) |
| `theme_rng.py` | mt19937_64, bit-identical to the app's `DetRng`, and the per-replicate seeds |
| `make_golden.py` | The C++ golden fixture: `strip` copies ledgers, `expected` writes the reference |
