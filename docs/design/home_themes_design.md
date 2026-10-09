# Home screen themes — design

Status: prototype (tools/themes/), not in the app. 9 Oct 2026.

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
8. **Name.** Each theme is named from its two strongest members, using the golfer phrase table
   (`tools/themes/golfer_phrases.json`, keyed by measure and direction). The app version carries
   these phrases in the pack.

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

## Before it goes into the app

- **Phrases live in the pack.** Golfer phrases become pack content per condition and per
  measure-and-direction, linted like the rest of the pack.
- **Where it runs.** Port it as a pure reduction alongside work-ons (`work_ons.h`), re-derived on
  the same catch-up and stamped with a rule version.
- **Minimum data.** Below about 60 swings across 3 sessions, layer 2 says "not yet". Layer 1 works
  from one session.
- **Trends.** A theme's trend needs a camera-drift-free session score. Until then it is worded
  tentatively, or left out.
- **Drill sessions.** These vary faults on purpose, and are the fastest way to more themes and to
  testing the model's edges.

## Tooling

`tools/themes/`, run with `/opt/homebrew/bin/python3 -I -B`. scipy is not importable under `-I`,
so everything is numpy.

| File | Does |
|---|---|
| `theme_data.py` | The swing × measure table from the ledgers, and the session centring |
| `theme_pca.py` | Themes, with stability, clustering cross-check and naming (`--out` dir → `themes.json`, loadings, stability, session scores) |
| `theme_network.py` | Pairwise links and the model comparison (`--out` dir → `links.csv`, `model_comparison.csv`) |
| `theme_summary.py` | The two-layer plain-language summary (`--themes <themes.json>`) |
| `golfer_phrases.json` | Prototype golfer wording |
