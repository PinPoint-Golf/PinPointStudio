# PinPoint Studio: Variability marks

**Design specification** · Status: approved for implementation · 7 Nov 2026
**Audience:** the PinPoint Studio design system (source of truth), and implementers
**Design canvas:** "Consistency Indicator" (claude.ai artifact, private). This document is self-contained; the canvas is illustrative only.

## 1. Purpose and principles

Golfers are put off by σ, box plots and statistical vocabulary. The variability marks make spread readable at a glance, with plain words, wherever PinPoint reports how much something varies.

The marks follow seven principles.

1. **A word always goes with the mark.** Colour and shape are never the only channel.
2. **The mark shows the grade, not raw SD.** Spread is always judged against a scale that matters for that measure, so equal σ can earn different grades.
3. **Consistency is precision, not accuracy.** It measures spread about the golfer's own mean. Repeating the same wrong position is *Grooved*; diagnostics separately says it's wrong.
4. **Measurement quality is a different question.** "Can we trust the number?" gets its own mark and its own words, and is never confused with golfer behaviour.
5. **Typicality is not a judgement.** An unusual swing may be the best swing. Whether a swing is good is the corridor's job.
6. **Certainty is not variability.** How sure we are of a finding uses the existing strength mark.
7. **It is one visual family.** Every variability visual shares the same geometry language, tones and words, so the app reads as one system.

## 2. The family at a glance

| Element | Question it answers | Component · variant | Forms | Vocabulary | Colour |
|---|---|---|---|---|---|
| **Dispersion** | Does the golfer repeat? (scalar measures) | `PpVariabilityMark` · `Dispersion` | Icon, Cell | Consistency | Status by grade |
| **Overlay** | Does the golfer repeat? (time-series measures) | `PpVariabilityMark` · `Overlay` | Icon, Cell | Consistency | Status by grade |
| **Needle** | Can we trust this reading? | `PpVariabilityMark` · `Needle` | Icon, Cell | Measurement | Status by grade |
| **Spread funnel** | Is the golfer getting more consistent over time? | `PpVariabilityFunnel` | Compact, Wide | Consistency + Trend | Tone of the current grade |
| **Your group** | How did this swing compare with my usual? (scalar) | `PpVariabilityMark` · `Typicality` | Icon, Cell | Typicality | Accent |
| **Trace on envelope** | How did this swing compare with my usual? (time series) | `PpVariabilityMark` · `TypicalityTrace` | Cell | Typicality | Accent |

The badge, `PpVariabilityBadge`, pairs any mark with its word (§5). Recommended defaults:
- Dispersion for icons and the golfer layer;
- Overlay Cell for technical-layer tables of time-series measures;
- Needle only for measurement (§9.4).

## 3. Grades and tone

| Grade | Consistency | Measurement | Tone |
|---|---|---|---|
| 5 | Grooved | Locked | `colorGood` |
| 4 | Repeatable | Steady | `colorGood` |
| 3 | Settling | Fair | `colorAttention` |
| 2 | Variable | Unsteady | `colorWarn` |
| 1 | Scattered | Noisy | `colorWarn` |
| 0 | Not enough swings | Checking | `colorText3` |

- **Grade 5 is the least variable;** grade 1 the most. Grade 0 means no grade: too little data, or measurement-limited (§8.6).
- **Typicality** uses `colorAccent` and is never graded.
- **Neutral elements** (rings, baselines, rulers, target lines) use `colorBorderStrong` or `colorBorderMid`, as specified per element.
- **Translucent washes** are tone at a given alpha over `colorSurface`, multiplied on screen by `variabilityWashAlpha` (1 in light themes, 3 in dark, clamped to 1). They are the group ellipse, the Needle wedges, the funnel band fill, the typicality inner zone and the trace envelope. Strokes and the 70–85% dots are not washes. No shadows, blur effects or gradients. (Mark, 7 Nov 2026: in dark themes the 12/16/24% Needle wedges could not be told apart.)

## 4. The mark: `PpVariabilityMark`

### 4.1 Properties

| Property | Type | Default | Notes |
|---|---|---|---|
| `grade` | int | 0 | 0–5. Out-of-range values are clamped, with a console warning. Ignored by the typicality variants. |
| `variant` | enum | `Dispersion` | `Dispersion` \| `Overlay` \| `Needle` \| `Typicality` \| `TypicalityTrace` |
| `form` | enum | `Icon` | `Icon` (square) \| `Cell` (landscape, 80 : 24) |
| `size` | real | `Theme.sp(variabilityIconSize)` | Icon edge length, or Cell height (Cell width = height × 80/24) |
| `tone` | color | from grade | Overridable. Typicality defaults to `colorAccent`. |
| `label` | string | from vocabulary | The word, used for the accessible name. The mark never draws text. |
| `vocabulary` | object | by variant | Consistency (Dispersion, Overlay), Measurement (Needle), Typicality (typicality variants) |
| `values` | object | none | Per-instance values for the typicality variants (§6.2). Named `values`, not `data`, which is Qt's built-in children list (Mark, 7 Nov 2026). |
| `showCorridor` | bool | false | Typicality only. Draws the corridor bracket (§6.2). |

### 4.2 Behaviour

- **Accessibility:** `Accessible.role: Graphic`. `Accessible.name` is "<domain>: <word> (<grade> of 5)", for example "Consistency: Repeatable (4 of 5)".
- **Motion:** the geometry never animates between grades. Colour changes follow `--pp-duration-slow`, and are 0 under reduced motion.
- **Strokes** are constant in pixels at every size. Geometry is scaled; strokes are not.

### 4.3 Geometry conventions

- Design units: Icon 24 × 24, Cell 80 × 24.
- `g = grade − 1` indexes the per-grade arrays.
- Screen coordinates: y points down; angles are in degrees.
- `exp` is the natural exponential.

### 4.4 Dispersion

**Icon**
- **Ring:** centre (12, 12), r 10.5, 1 px, `colorBorderStrong`.
- **Dots:** 3, at −30°, 90° and 210° from the centre.
  - Distance from the centre: `spread[g]`, with `spread = [7.2, 5.4, 3.6, 1.8, 0]`.
  - Radius 2.6 (3.0 at `size` ≤ 12 px).
  - Fill: tone at 85%.
- At grade 5 the three dots coincide and read as one dot.

**Cell**
- **Target line:** x 40, from y 2 to y 22, 1 px, `colorBorderMid`.
- **Group ellipse:** centre (40, 12), `rx = sx[g] + 4`, `ry = sy[g] + 3.5`. Fill: tone at 12%.
- **Dots:** 7, r 2.2, tone at 85%, at `(40 + sx[g]·ox[k], 12 + sy[g]·oy[k])`:
  - `sx = [30, 21, 13, 6.5, 1.8]`
  - `sy = [7.5, 5.5, 3.8, 2.2, 0.6]`
  - `ox = [−0.95, −0.55, −0.15, 0.1, 0.45, 0.75, 1]`
  - `oy = [0.3, −0.9, 0.7, −0.2, 1, −0.6, 0.1]`

### 4.5 Overlay

**Icon**
- **Baseline:** y 21.5, from x 1 to x 23, 1 px, `colorBorderStrong`.
- **Traces:** 3, sampled at x = 1…23 in steps of 0.5:
  - `y = 21.5 − h·exp(−((x − c)/3.8)²)`
  - `c = 12 + sp[g]·shift[k]`, with `shift = [−1, 0.2, 1]`
  - `h = 15·(1 + hv[g]·hk[k])`, with `hk = [−1, 1, 0.1]`
  - `sp = [3.2, 2.2, 1.3, 0.6, 0]`
  - `hv = [0.3, 0.2, 0.12, 0.05, 0]`
- **Stroke:** 1.25 px at `size` ≤ 16 px, else 1.5 px. Round cap and join. Tone at 70%.

**Cell**
- **Baseline:** y 21.5, from x 2 to x 78, 1 px.
- **Traces:** 5, sampled at x = 2…78 in steps of 1:
  - `y = 21.5 − h·exp(−((x − c)/9)²) − 1.5·exp(−((x − 14)/8)²)`
  - `c = 40 + tsp[g]·tshift[j]`, with `tshift = [−1, 0.55, 0.1, 1, −0.6]`
  - `h = 17·(1 + thv[g]·th[j])`, with `th = [−1, 0.6, 0.05, −0.5, 1]`
  - `tsp = [9, 6.2, 3.8, 1.8, 0.4]`
  - `thv = [0.3, 0.21, 0.13, 0.06, 0.015]`
- **Stroke:** 1.3 px, round cap and join, tone at 60%.

### 4.6 Needle (measurement quality)

The "blur" is three stacked translucent shapes, never a blur filter.

**Icon**
- **Gauge arc:** the upper half-circle, centre (12, 20), r 10, from (2, 20) to (22, 20). 1 px, `colorBorderStrong`.
- **Wedges:** 3 sectors on the pivot (12, 20), radius 11, pointing up.
  - Half-angles: `a`, `0.62a` and `0.3a`, with `a = [34, 22, 13, 6, 0]°`.
  - Fill: tone at 12 / 16 / 24% (14 / 18 / 26% at `size` ≤ 16 px).
- **Needle:** from (12, 20) to (12, 8.5). 1.5 px (1.25 px at `size` ≤ 16 px), round cap, full tone.
- **Pivot dot:** at (12, 20), r 1.8 (2.2 at `size` ≤ 16 px).
- At grade 5 only the needle remains.

**Cell**
- **Ruler:** y 5, from x 4 to x 76, `colorBorderMid`.
- **Ticks:** every 8 units from x 8 to x 72, `colorBorderStrong`.
  - Major ticks (y 2–5) at x 8, 24, 40, 56 and 72.
  - Minor ticks (y 3.5–5) between them.
- **Wedges:** 3 triangles, each with its apex at (40, 21) and its base from (40 ± w, 5).
  - Half-widths: `w`, `0.62w` and `0.3w`, with `w = [24, 15, 9, 4, 0]`.
  - Fill: tone at 12 / 16 / 24%.
- **Needle:** x 40, from y 4 to y 21, 1.5 px.
- **Pivot dot:** at (40, 21), r 2.

### 4.7 Grade 0

- Dispersion: the ring (Icon) or target line (Cell) only.
- Overlay: the baseline only.
- Needle: the arc or ruler only.
- Word: "Not enough swings" (consistency) or "Checking" (measurement), in `colorText3`.

## 5. The badge: `PpVariabilityBadge`

The mark plus its word, laid out for each form.

- **Icon badge:**
  - Mark at `variabilityIconSize` (20; 16 in dense lists).
  - Gap of 6, then the word in body 12 in the tone.
- **Cell badge:**
  - Mark at `variabilityCellW` × `variabilityCellH`.
  - Gap of 10, then the word in body2 in the tone.
- **Technical layer:** the basis string (§7.5) follows the word, in data-sm in `colorText2`, separated by " · ".
- **Tooltip:** on the golfer layer, the word's line from §7.

## 6. Companion visuals

### 6.1 Spread funnel: `PpVariabilityFunnel`

The group's spread drawn as a band along time. It narrows into a funnel as the golfer tightens, and opens out as they loosen.

**Sizes:** compact 120 × 32 (tables, work-on rows) or wide (the Trend tab). Minimum width 96.

**Geometry:**
- **Centre line:** at H/2, 1 px dashed, `colorBorderStrong`.
- **Sessions:** evenly spaced with 6 px end padding, oldest on the left, at most 12 (older ones roll off).
- **Half-width at each session:** `clamp(ρ / 1.25, 0.08, 1) × (H/2 − 2)`. It is driven by ρ (§8), so it shows movement within a grade.
- **Band:** a polygon through the upper and lower half-widths, with straight segments.
  - Fill: the **current** grade's tone at 16%.
  - Edge: 1 px, tone at 55%.
- **Session markers:** dots r 1.6 in `colorText2` on the centre line. The current session is r 2.2 in tone.
- **Breaks:** a change of context, basis or scale stops the band. It restarts after a 4 px gap marked by a short dashed `colorBorderStrong` tick.
- **Fewer than 3 sessions** in the latest unbroken run: no band. Show the badge plus "Trend after 3 sessions" in `colorText3`.

**Label and accessibility:**
- **Label:** the current grade word, then the trend word: "Repeatable · ↑ Tightening".
- **Technical layer:** add the basis, for example "ρ 0.62 → 0.38 over 6 sessions".
- **Accessible name:** for example "Pelvis rotation at top: Repeatable, tightening over 6 sessions".

### 6.2 Typicality: this swing compared with your usual

**Rules for both variants:**
- **Accent, never status colours.** Typicality is not graded (§8.7).
- **`showCorridor: true`** draws the measure's corridor as a 1.5 px `colorGood` bracket beneath the visual. Use it wherever the golfer needs to know whether the swing was *good*. It keeps "usual" and "good" visibly separate.

**Your group** (`Typicality`, scalar measures)

- **Cell:** 1 SD = 14 units on x, centred at x 40.
  - **Outer "nearly all" zone (±2 SD):** ellipse rx 28, ry 9, 1 px dashed `colorBorderStrong`.
  - **Inner "most swings" zone (±1 SD):** rx 14, ry 5, `colorText` at 7%.
  - **Ghost dots:** up to 12 recent swings at `x = 40 + zᵢ × 14`, r 1.5, `colorText3` at 70%. Vertical position is a deterministic jitter (from the swing id, ±4 units) and **carries no meaning**.
  - **This swing:** a ring, r 3, 1.75 px accent stroke, `colorSurface` fill, plus an accent centre dot r 1.1.
  - **When |z| > 2.6:** clamp the ring at x 4 or x 76 and add an outward chevron.
- **Icon:**
  - **Outer dashed circle:** r 10.5 (2 SD).
  - **Inner disc:** r 5.25 (1 SD), `colorText` at 7%.
  - **This swing:** an accent ring, r 2.6, at `x = 12 + clamp(z, −2.2, 2.2) × 5.25`, y 12.

**Trace on envelope** (`TypicalityTrace`, time-series measures; Cell only)

- **Baseline:** y 21.5.
- **Envelope:** mean(t) ± SD(t) over the golfer's window, time-normalised (§8.3). Filled `colorText` at 9%.
- **Mean:** 1 px dashed, `colorText3`.
- **This swing:** 1.75 px accent, round cap and join, on the same normalised time.
- **No icon form.** In icon contexts, use the Your group icon with the trace's RMS z.

## 7. Wording

All strings live in vocabulary singletons, never in screens or data packs, and go through `qsTr()` with a translator comment naming the vocabulary. Arrow glyphs stay outside the translatable strings.

### 7.1 Consistency (`ConsistencyVocabulary`)

| Grade | Word | Golfer-layer line |
|---|---|---|
| 5 | **Grooved** | You repeat this almost exactly, swing after swing. |
| 4 | **Repeatable** | This stays much the same from one swing to the next. |
| 3 | **Settling** | This is starting to repeat, with some swings still drifting. |
| 2 | **Variable** | This changes noticeably from swing to swing. |
| 1 | **Scattered** | This is different almost every swing. |
| 0 | **Not enough swings** | Hit a few more and we'll show how consistent this is. |

### 7.2 Measurement (`MeasurementVocabulary`; Needle only)

| Grade | Word | Line |
|---|---|---|
| 5 | **Locked** | This reading is rock steady. Even small changes are real. |
| 4 | **Steady** | This reading is reliable. Small noise won't change what it tells you. |
| 3 | **Fair** | This reading is usable. Treat small changes with caution. |
| 2 | **Unsteady** | This reading is noisy. Only large changes mean anything. |
| 1 | **Noisy** | This reading is too noisy to rely on. Check the setup. |
| 0 | **Checking** | Not enough data yet to judge this reading. |

These words describe the measurement, never the golfer. Always name the reading: "Pelvis IMU · Steady".

### 7.3 Trend (`TrendVocabulary`)

| Word | When | Golfer-layer line |
|---|---|---|
| **↑ Tightening** | Spread is falling by more than the noise | This is becoming more repeatable session by session. |
| **↓ Loosening** | Spread is rising by more than the noise | This is repeating less than it was. |
| **→ Holding** | No change beyond the noise | No real change in how consistently you do this. |

The trend word always follows the grade word, never replaces it.

### 7.4 Typicality (`TypicalityVocabulary`)

| Word | When | Golfer-layer line |
|---|---|---|
| **Your usual** | \|z\| ≤ 1 | This swing looked like most of your swings. |
| **A little off** | 1 < \|z\| ≤ 2 | This swing was a little different from your usual. |
| **Unusual** | \|z\| > 2 | This swing was quite different from your usual, for better or worse. |

- **Never imply praise or fault.**
- **Zone labels:** "most swings" (±1 SD) and "nearly all" (±2 SD). Never "normal" or "abnormal".

### 7.5 Phrasing rules

- **Voice:** second person, sentence case, plain words. Headings are set in capitals by the theme, not in the source.
- **Golfer layer:**
  - The word and its line only.
  - No figures, no σ, ρ or CV.
  - No causal claims.
- **Technical layer:** the word, then a basis string in the data face. Every basis string is built from computed values and never hand-written; show only what was actually computed.

| Context | Basis string |
|---|---|
| Scalar consistency | "σ 2.1° · 0.42 of corridor · n 24" |
| Timing-limited trace | "σ peak 18 ms · n 24" |
| Ordinal | "Same order in 21 of 24 swings" |
| Measurement-limited | the consistency string, then "· measurement-limited" |
| Trend | "ρ 0.62 → 0.38 over 6 sessions" |
| Typicality, scalar | "this swing 5.2 cm · usual 4.8 cm" |
| Typicality, trace | "peak on time", "peak 25 ms early", "peak 45 ms late, higher" |
| Ball flight | "offline ±6 yd · carry ±5 yd · 12 shots" |

- **Keep existing headings.** "Consistency" as a heading stays; the grade words and descriptions are what change.

## 8. Grade basis

### 8.1 Principle

By the identity RMSE² = bias² + SD² (population SD):
- **Consistency** grades the **SD** term only. Bias belongs to diagnostics.
- **Measurement quality** grades **total** uncertainty, bias included, because there the reference is the truth.

### 8.2 Scalar measures

1. **Window:** the session. For Home and trends, use the last `N_WINDOW` swings. Exclude swings already flagged as outliers or mishits.
2. **Minimum n:** fewer than `N_MIN` swings gives grade 0.
3. **Spread:** the sample SD. For `scaleType: circular`, use the circular SD `√(−2 ln R̄)`.
4. **Scale:** take the first available.
   1. The norm corridor half-width for the context: `(hi − lo)/2`. Norms store μ, σLo and σHi, so the corridor is the in-range band (|z| ≤ 1) and its half-width is `(σLo + σHi)/2`, the σ the Consistency matrix and Posture repeatability already use (Mark, 7 Nov 2026).
   2. `variabilityScale`.
   3. CV, only for `scaleType: ratio` with strictly positive values.
   4. Otherwise the measure is **ungraded**: no mark, never raw SD.
5. **Ratio:** `ρ = SD / scale`, graded on the bands below.

| Grade | ρ |
|---|---|
| 5 | ρ ≤ 0.25 |
| 4 | 0.25 < ρ ≤ 0.50 |
| 3 | 0.50 < ρ ≤ 0.75 |
| 2 | 0.75 < ρ ≤ 1.00 |
| 1 | ρ > 1.00 |

The anchor: at ρ = 1, ±1 SD spans the whole corridor, so even a perfectly centred golfer has about a third of swings outside it.

### 8.3 Time-series measures

1. **Time-normalise** between event landmarks (P-positions, else address, top and impact). The default window is top → impact, resampled to 101 points.
2. **Amplitude:** the RMS of SD(t), divided by a scale. The scale is the corridor half-width of the associated scalar, else `variabilityScale`, else `TRACE_AMP_FRACTION` × the mean peak. This gives ρₐ.
3. **Timing:** the SD of time-to-peak in real milliseconds, divided by the timing corridor half-width, else `TIMING_SCALE_MS`. This gives ρₜ.
4. **Grade:** the worse of ρₐ and ρₜ. Averaging would hide timing scatter, and kinematic-sequence consistency is largely about timing.
5. **CMC** may be reported on the technical layer, but never drives the grade.

### 8.4 Ordinal and categorical measures

Examples: kinematic-sequence order, P-position checkpoints, classified outcomes.

1. **Modal share** `s`: the share of swings matching the most common category or ordering.
2. **Chance-corrected:** `s′ = (s − 1/k)/(1 − 1/k)`, where k = 2 for a binary checkpoint and k = m! for an order of m segments.
3. **Bands on s′:**

| Grade | s′ |
|---|---|
| 5 | ≥ 0.85 |
| 4 | ≥ 0.65 |
| 3 | ≥ 0.45 |
| 2 | ≥ 0.25 |
| 1 | < 0.25 |

A repeated *wrong* pattern is still Grooved.

### 8.5 Measurement quality

**Uncertainty:** take the first available.
1. Validation RMSE against ground truth.
2. Live residual RMSE (reprojection, calibration, smoother innovation, clock sync).
3. Stationary noise SD.

**Tolerance:** `measurementTolerance`, else `MEAS_TOL_FRACTION` × the corridor half-width, else ungraded.

**Grade:** `ρm = uncertainty / tolerance`, on the §8.2 bands.

### 8.6 Coupling between measurement and consistency

Observed spread includes measurement noise: SD²obs = SD²golfer + σ²noise.

- **When σnoise is known,** grade consistency on `√max(0, SD²obs − σ²noise)`.
- **When the source's Needle grade is 1–2:**
  - Technical layer: append "· measurement-limited".
  - Golfer layer: show the grade-0 mark with the word "Checking".

### 8.7 Typicality

> **Amended 7 Nov (decisions.md §2, R22):** z is the robust z, `(x − median)/(IQR/1.349)`, for the scalar, the trace's z(t), the ghosts and the corridor; the basis reads "· median". Circular angles keep the circular mean and SD.

- **Scalar:** `z = (x − x̄)/SD` over the window, with n ≥ `N_MIN`.
- **Trace:** the RMS of z(t), plus the timing and amplitude deviations used for the basis string.
- **Never graded.** Words come from the |z| thresholds (§7.4).

### 8.8 Trend

- **"↑ Tightening" / "↓ Loosening":** only when the change in ρ over the last `TREND_MIN_SESSIONS` or more sessions exceeds `TREND_SE_MULT` × its standard error. The change is the least-squares slope × span, and the SE of ρ is about `ρ/√(2(n−1))`.
- **Otherwise:** "→ Holding".
- **Like-for-like only:** same context, basis and scale. Any change is a break, and the trend uses the latest unbroken run.

### 8.9 Hysteresis

- **Live sessions:** a grade changes only when ρ (or s′) crosses a band edge by `HYSTERESIS`.
- **Frozen session assessments:** use the plain bands.

### 8.10 Constants

All are provisional and to be calibrated against logged sessions before being finalised.

| Constant | Value | Used in |
|---|---|---|
| `N_MIN` | 8 swings | all grades, typicality |
| `N_WINDOW` | 20 swings | Home and trend windows |
| Scalar and measurement bands | 0.25 / 0.50 / 0.75 / 1.00 | §8.2, §8.5 |
| Ordinal bands | 0.85 / 0.65 / 0.45 / 0.25 | §8.4 |
| `TRACE_AMP_FRACTION` | 0.10 | §8.3 |
| `TIMING_SCALE_MS` | 20 ms | §8.3 |
| `MEAS_TOL_FRACTION` | 0.25 | §8.5 |
| `HYSTERESIS` | 0.05 | §8.9 |
| `TREND_MIN_SESSIONS` | 3 | §8.8 |
| `TREND_SE_MULT` | 2 | §8.8 |
| Typicality thresholds | \|z\| 1, 2 | §7.4 |

`MetricDescriptor` gains three optional fields: `scaleType` (`interval` | `ratio` | `circular` | `ordinal` | `categorical`), `variabilityScale` and `measurementTolerance`.

## 9. Usage rules

### 9.1 Where a consistency mark is allowed (all three must hold)

1. **The spread is repeatability:** the same measure, across swings, in comparable conditions.
2. **The spread isn't expected.** Never use the mark for spread across clubs, shot types or contexts, or for within-swing variation (movement, not repetition).
3. **A grade exists** (§8). Never draw a mark from raw SD.

### 9.2 Classifying existing displays

| Class | When | Treatment |
|---|---|---|
| **Replace** | Golfer layer, eligible | Remove σ or the box plot. Show the badge, with the line as its tooltip. |
| **Pair** | Technical layer, eligible | Keep σ and any box plot. Add the badge with its basis string. |
| **Measurement** | The figure describes the measurement, not the golfer | Needle with measurement words (§9.4) |
| **Typicality** | A per-swing "vs average" or outlier display | Your group, or trace on envelope (§6.2) |
| **Funnel** | A multi-session spread or trend display | Spread funnel (§6.1) |
| **Leave** | Not eligible, or ungraded | Unchanged. Record the reason. |

### 9.3 Consistency rules

- **One meaning everywhere:** the mark always encodes the §8 grade.
- **Technical-layer box plots stay.** They carry location, skew and outliers, so they are paired with the mark, never replaced.
- **Golfer-layer box plots:** replace one that only conveys spread. One that shows position against the corridor is **leave** until the box-plot replacement is decided (§12, open items).

### 9.4 The Needle

**The Needle answers "can we trust this number?"** Dispersion and Overlay answer "does the golfer repeat?" Never put both on one value without naming which is which.

| Use for | Basis | Typical surface |
|---|---|---|
| Pipeline validated accuracy (shaft, ball, pose) | Validation RMSE | Metric info, technical catalogue |
| Triangulation quality | Reprojection RMSE | Rig and calibration screens, capture summary |
| Camera calibration | Calibration residuals | Calibration wizard result |
| IMU noise and drift | Stationary SD or ESKF innovation RMS | Sensor panel, per-sensor status |
| Clock sync (BLE, camera, phone) | Sync residual SD | Device and sync status |
| Pose and tracking jitter | Static-frame SD or smoother innovation | Capture quality settings |
| A consistency grade limited by its source | Source ρm | "Measurement-limited" (§8.6) |

**Never use the Needle for:**
- golfer variability;
- classifier or detector confidence (a likelihood, not a spread);
- dropouts or missing data (status components);
- already-corrected bias (show the residual instead);
- launch-monitor or vendor figures we don't measure ourselves.

**Placement:** on the golfer layer, only on capture, setup and device surfaces, where the golfer can act. Never on Home or diagnosis cards, except through the "Checking" substitution.

### 9.5 Further rules

1. **Intended variability.** Grade within each intent (shot shape or shot type), never across intents. In varied practice where shots carry no intent, outcome measures get no grade. Body-movement measures that should repeat remain eligible.
2. **Work-on completion.** A work-on is achieved only when its mean is in the corridor **and** its consistency is Repeatable or better over at least `N_MIN` swings. Show both on the work-on.
3. **Certainty.** Finding and causal-link certainty uses the strength mark (CLEAR, LIKELY, POSSIBLE). The variability mark sits with the measure; the strength mark sits with the finding.
4. **Print and greyscale.** Shape and word must carry the grade without colour. Print styles multiply translucent fills by `variabilityPrintAlphaScale`. In print, the mark, badge and funnel take an optional `roles` (PpTypeset) and use its inks, sizes and type, so a printed page never depends on a screen setting (Mark, 7 Nov 2026).
5. **Roster.** Once cohort norms exist, each golfer is graded on their own cohort corridor. Never mix cohort and universal scales in one column without saying so in the header.

### 9.6 Launch-monitor ball flight

- **Keep the existing dispersion plot.** Add a per-club Dispersion badge as its summary ("7-iron · Settling").
- **Grouping:** per club and per intent. A pooled view isn't graded.
- **Scale:** per-club offline and carry tolerances (`variabilityScale`), so a wide driver group and a tight wedge group are graded fairly.
- **The plot's ellipses** use the golfer's measured SD, never a vendor value, in Dispersion tones.
- **No Needle** for launch-monitor error.

## 10. Tokens

**Colour use:** §3 and per-element, using existing tokens only.

**New tokens:**

| Token | Value | Use |
|---|---|---|
| `variabilityIconSize` | 20 | Default icon edge |
| `variabilityCellW` / `variabilityCellH` | 80 / 24 | Cell size in a 40 px row |
| `variabilityFunnelH` | 32 | Compact funnel height |
| `variabilityPrintAlphaScale` | 1.5 | Print uplift for translucent fills |
| `variabilityHysteresis` | 0.05 | Live-session grade hysteresis |
| `variabilityWashAlpha` | 1 light, 3 dark | Screen multiplier on every translucent wash (§3). Added 7 Nov 2026 after Mark found the dark-theme Needle wedges indistinct. `variabilityWashAlphaStrong` was not needed. Print uses `roles` and `variabilityPrintAlphaScale` instead. |

## 11. Rendering guidance

- **Renderer:** Qt Quick Shapes with `CurveRenderer`.
  - Primitives: `PathPolyline` for traces, `PathAngleArc` for rings, dots and ellipses, plain paths for wedges.
  - Not to be used: Canvas, SVG assets, QPainter items, MSAA, blur effects.
- **Pixel geometry:** compute in pixels from the design units. Never `scale` the item, so strokes stay constant.
- **Geometry providers:** abstract interface `IVariabilityGeometry`.
  - `CanonicalVariabilityGeometry`: grade-only, cached and shared; the §4 tables.
  - `DataVariabilityGeometry`: per-instance data, for the funnel and typicality.
- **Delegates:** with delegate reuse in tables, the mark must stay smooth at 500 or more rows.

## 12. Decisions and open items

**Decided:**

| Decision | Chosen | Rejected, and why |
|---|---|---|
| Consistency mark | Dispersion (scalar) and Overlay (time series) | Pendulum: closest to the Wi-Fi idiom, but less golf, less kinematic |
| Measurement mark | Needle with wedge | Focus (camera-specific); Error bar (too technical) |
| Trend | Spread funnel | Mark strip (no within-grade movement); grade steps (generic, technical) |
| Typicality | Your group plus trace on envelope | Band and pin. Its corridor bracket was adopted as `showCorridor`. |
| Ball flight | Keep the existing launch-monitor plot and add a badge | New shot-pattern, strip or zone visuals: unnecessary, since real data is already plotted |
| Rendering | Shapes with `CurveRenderer` | Canvas, SVG, QPainter, custom scene-graph nodes |

**Open:**
- **Golfer-layer box-plot replacement:** a data-driven Dispersion cell with a target line, bias offset and `showCorridor`. Not before the bands are calibrated.
- **Measurement-quality surfaces** that show no figure today (§9.4 table): add them after the audit.
- **Cohort-specific scales:** wait for the cohort norms.
