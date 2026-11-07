# Variability marks

How much something varies, or whether a reading can be trusted, as a shape and a word. Components: [PpVariabilityMark](components/PpVariabilityMark.md), [PpVariabilityBadge](components/PpVariabilityBadge.md) (the mark and its word: what a screen uses) and [PpVariabilityFunnel](components/PpVariabilityFunnel.md). Their words are under [Words](#words), below.

This page is the family guide: when a mark is allowed, how to classify an existing display, the rules, and every word. Each component's own page has its properties, states and layout. The design rationale is `docs/design/variability-mark/spec.md`; decisions and open items are `docs/design/variability-mark/decisions.md`.

## Principles

1. A word always goes with the mark. Colour and shape are never the only channel.
2. The mark shows a grade, never raw SD. Spread is judged against a scale that matters for the measure (its corridor), so equal σ can earn different grades. No scale, no mark.
3. Consistency is precision, not accuracy: spread about the golfer's own mean. Repeating the same wrong position is Grooved; diagnostics says separately that it's wrong.
4. Measurement quality is a different question ("can we trust the number?"), with its own mark, the Needle, and its own words.
5. Typicality is not a judgement. An unusual swing may be the best one; whether a swing is good is the corridor's job.
6. Certainty is not variability. How sure a finding is uses `PpStrengthMark`.
7. One visual family: one geometry language, one set of tones and words.

## The family

| element | answers | component · variant | forms | words | colour |
|---|---|---|---|---|---|
| Dispersion | Does the golfer repeat? (scalar measures) | `PpVariabilityMark` · `Dispersion` | Icon, Cell | Consistency | status by grade |
| Overlay | Does the golfer repeat? (time-series measures) | `PpVariabilityMark` · `Overlay` | Icon, Cell | Consistency | status by grade |
| Needle | Can we trust this reading? | `PpVariabilityMark` · `Needle` | Icon, Cell | Measurement | status by grade |
| Spread funnel | Is the golfer getting more consistent over time? | `PpVariabilityFunnel` | compact, wide | Consistency + Trend | the current grade's tone |
| Your group | How did this swing compare with my usual? (scalar) | `PpVariabilityMark` · `Typicality` | Icon, Cell | Typicality | accent |
| Trace on envelope | How did this swing compare with my usual? (time series) | `PpVariabilityMark` · `TypicalityTrace` | Cell | Typicality | accent |

- Grades: 5 the least variable, 1 the most; 0 no grade (too few swings). Tones: 5–4 `colorGood`, 3 `colorAttention`, 2–1 `colorWarn`, 0 `colorText3`. Typicality is never graded and wears `colorAccent`.
- Washes: the group ellipse, the Needle's wedges, the funnel's band fill, the typicality inner zone and the trace's envelope are tone at a low alpha. On screen each × `Theme.variabilityWashAlpha`, clamped to 1: 1 in light themes, 3 in dark (Mark, 7 Nov 2026: the dark-theme Needle wedges could not be told apart). Strokes, the band's edge, the dots, the ghosts and the dashed outer zone are not washes. Paper never reads it.
- Defaults: Dispersion for icons and the golfer layer; Overlay Cell for technical-layer tables of time-series measures; the Needle only for measurement.
- Ungraded (no scale, no uncertainty): no mark at all, never a raw SD in its place.

## Where a consistency mark is allowed

All three must hold: the spread is repeatability (the same measure, across swings, in comparable conditions); the spread isn't expected (never across clubs, shot types or contexts, never movement within one swing); and a grade exists.

## Classifying a display

| class | when | treatment |
|---|---|---|
| Replace | Golfer layer, eligible | Remove the σ or spread-only box plot. Show the badge, its line as the tooltip. |
| Pair | Technical layer, eligible | Keep the σ and any box plot. Add the badge with its basis string. |
| Measurement | The figure describes the measurement, not the golfer | The Needle with measurement words. |
| Typicality | A per-swing "vs average" or outlier display | Your group, or trace on envelope. |
| Funnel | A multi-session spread or trend display | The spread funnel. |
| Leave | Not eligible, or ungraded | Unchanged. Record the reason. |

## Rules

- Technical-layer box plots stay: they carry location, skew and outliers. A golfer-layer box plot that shows position against the corridor is **leave** until its replacement is decided.
- The Needle is for validated pipeline accuracy, triangulation, camera calibration, IMU noise and drift, clock sync, pose and tracking jitter, and a consistency grade limited by its source. Never for golfer variability, detector confidence, dropouts, corrected bias or launch-monitor and vendor figures. On the golfer layer it goes only on capture, setup and device surfaces; never on Home or diagnosis cards, except as the "Checking" substitution. Never put a Needle and a consistency mark on one value without naming which is which.
- A measurement-limited grade (the source's Needle is Unsteady or Noisy) shows "Checking" on the golfer layer and the grade with "· measurement-limited" on the technical layer. `PpVariabilityBadge` does it.
- Intent: grade within each shot shape or type, never across. With no intent, outcome measures get no grade; body-movement measures stay eligible.
- A work-on is achieved only when its mean is in the corridor **and** its consistency is Repeatable or better over at least 8 swings (`Variability.nMin`). Show both.
- The strength mark sits with the finding; the variability mark sits with the measure.
- Print and greyscale: the shape and the word carry the grade without colour. On paper the marks take the page's `roles` and lift their fills by `variabilityPrintAlphaScale`, never by `variabilityWashAlpha`.
- Once cohort norms exist, never mix cohort and universal scales in one column without saying so in its header.
- Launch-monitor ball flight: keep the dispersion plot and add a per-club Dispersion badge ("7-iron · Settling"), per club and per intent; a pooled view isn't graded. No Needle for launch-monitor error.
- Host marks in tables with a real model (`ListModel` or C++), never an integer model.

## Words

Every variability word, line and accessible name lives in one of four singletons in `src/Gui/components/`, through `qsTr()` with a translator comment naming the vocabulary. No screen, data pack or presenter spells one. Arrow glyphs and the " · " separator stay outside the translatable strings.

### Consistency

`ConsistencyVocabulary`: Dispersion, Overlay, the funnel. How the golfer repeats.

| grade | word | golfer-layer line |
|---|---|---|
| 5 | Grooved | You repeat this almost exactly, swing after swing. |
| 4 | Repeatable | This stays much the same from one swing to the next. |
| 3 | Settling | This is starting to repeat, with some swings still drifting. |
| 2 | Variable | This changes noticeably from swing to swing. |
| 1 | Scattered | This is different almost every swing. |
| 0 | Not enough swings | Hit a few more and we'll show how consistent this is. |

### Measurement

`MeasurementVocabulary`: the Needle only. How far a reading can be trusted. The words describe the measurement, never the golfer, so always name the reading: "Pelvis IMU · Steady".

| grade | word | line |
|---|---|---|
| 5 | Locked | This reading is rock steady. Even small changes are real. |
| 4 | Steady | This reading is reliable. Small noise won't change what it tells you. |
| 3 | Fair | This reading is usable. Treat small changes with caution. |
| 2 | Unsteady | This reading is noisy. Only large changes mean anything. |
| 1 | Noisy | This reading is too noisy to rely on. Check the setup. |
| 0 | Checking | Not enough data yet to judge this reading. |

### Trend

`TrendVocabulary`: the funnel. The trend word always follows the grade word, never replaces it: "Repeatable · ↑ Tightening".

| token | label | golfer-layer line |
|---|---|---|
| `tightening` | ↑ Tightening | This is becoming more repeatable session by session. |
| `loosening` | ↓ Loosening | This is repeating less than it was. |
| `holding` | → Holding | No real change in how consistently you do this. |

`insufficient` or "" shows nothing. While the latest unbroken run has fewer than 3 sessions the funnel says "Trend after 3 sessions" (`tooFewSessions`; the count is `Variability.trendMinSessions`).

### Typicality

`TypicalityVocabulary`: the two typicality variants. This swing against the golfer's usual. Never praise or fault: an unusual swing may be the best one.

| band | word | golfer-layer line |
|---|---|---|
| `usual` (\|z\| ≤ 1) | Your usual | This swing looked like most of your swings. |
| `aLittleOff` (1 < \|z\| ≤ 2) | A little off | This swing was a little different from your usual. |
| `unusual` (\|z\| > 2) | Unusual | This swing was quite different from your usual, for better or worse. |

- Zone labels: "most swings" (±1 SD, `zoneMost`) and "nearly all" (±2 SD, `zoneNearlyAll`), in lower case. Never "normal" or "abnormal".
- Accessible names: "Consistency: Repeatable (4 of 5)", "Measurement: Steady (4 of 5)", "Consistency: Not enough swings (0 of 5)"; typicality has no grade, "Typicality: Your usual". The funnel: "Pelvis rotation at top: Repeatable, tightening over 6 sessions".

### Phrasing

- Golfer layer: the word, and its line as the tooltip. No figures: no σ, ρ, CV or counts. No causal claims.
- Technical layer: the word, then " · " and the basis string in the data face (`fontSzDataSm`, `colorText2`). Every basis string is built in C++ from computed values (`src/Metrics/variability/`), never hand-written, and shows only what was computed. An empty one shows nothing.
- Keep existing headings. "Consistency" stays a heading; the grade words and lines under it are what change.

### Basis strings

As the grader builds them:

| result | basis string | built by |
|---|---|---|
| Scalar, against the norm corridor | "σ 2.1° · 0.42 of corridor · n 24" | `VariabilityResult::basisString` |
| Scalar, against the measure's `variabilityScale` | "σ 2.1° · 0.42 of scale · n 24" | the same |
| Scalar, CV fallback (a ratio measure with no corridor or scale) | "σ 4.1 km/h · CV 0.04 · n 24" | the same |
| Trace, timing sets the grade | "σ peak 18 ms · n 24" | the same |
| Trace, amplitude sets the grade | the scalar form, with the RMS of SD(t) and ρₐ: "σ 3.2° · 0.48 of scale · n 24". "of corridor" against a corridor; the 10 %-of-peak fallback reads "of scale". | the same |
| Ordinal (an order or checkpoint) | "Same order in 21 of 24 swings" | the same |
| Measurement-limited | the consistency string, then " · measurement-limited" | the same |
| Needle (measurement) | none: the string is empty | the same |
| Grade 0, or ungraded | none | the same |
| Trend | "ρ 0.62 → 0.38 over 6 sessions"; empty with no trend | `TrendResult::basisString` |
| Typicality, scalar | "this swing 5.2 cm · usual 4.8 cm" | `ScalarTypicality::basisString` |
| Typicality, trace | "peak on time", "peak 25 ms early", "peak 45 ms late, higher", "… lower" | `TraceTypicality::basisString` |
| Ball flight (launch monitor, per club) | "offline ±6 yd · carry ±5 yd · 12 shots" | `ballFlightBasisString` |

- A fallback scale is never called a corridor.
- σ takes the measure's own decimals; ρ and CV two places; milliseconds none. "°", "%" and ":" sit against the number ("2.1°"); any other unit follows a space ("4.8 cm").
- "Peak on time" means within the golfer's usual spread of time-to-peak (|z| ≤ 1), or a deviation that rounds to 0 ms. "higher" or "lower" appears only past |z| 1 of the window's peaks.

## Not repeated here

The geometry tables (spec §4 and §6: coordinates, per-grade arrays, alphas) live in `docs/design/variability-mark/spec.md` and are built in `src/Gui/variability/variability_geometry.cpp`. The grade basis and its provisional constants (spec §8) are in `src/Metrics/variability/`. Decisions, gaps the build filled and open items: `docs/design/variability-mark/decisions.md`.
