# PinPoint Studio design system

PinPoint Studio is a desktop instrument for golf swing analysis: quiet chrome, flat surfaces, hairlines, and one accent, so the golfer's video, figures and verdicts read first.

**Read this before writing any QML.** It is the rule book; the values live in one place:

- `src/Gui/theme/Theme.qml` — the source of truth. Every colour, font, size and space a component uses is a `Theme.*` property.
- [`tokens.json`](tokens.json) — every Theme value with a usage note, per theme. **Generated** from Theme.qml by `tools/theme/make_design_system.py`; never edit it by hand (`--check` fails when it is stale).
- [`components/`](components/README.md) — one page per shared `Pp*` control: when to use it, its properties, the tokens it reads, its states.
- The visual reference (swatches, type specimens, rendered component previews across the themes) is the PinPoint Studio design system artifact on claude.ai. Where it and this folder differ, this folder and Theme.qml win.
- `pinpoint_qml_design_system.md` (one level up) is the design history and reasoning — the coaching-card look (§13), themes as data (§14), the typeset system (§15). Its older value tables are superseded by Theme.qml and `tokens.json`.

## The non-negotiables

- Never hard-code a colour, font family, font size or space. Use `Theme.color*`, `Theme.font*`, `Theme.fontSz*`, `Theme.sp(n)` for the size of a thing and `Theme.gap(n)` for the space between things.
- Never branch a component on `Theme.aesthetic`. If two aesthetics need different behaviour, that is a new optional key in the theme table (`_v(name, default)`), not an `if`.
- Use the shared component from `src/Gui/components/` (or `src/Gui/typeset/`) rather than redrawing it. A restyle is one file. A screen never draws its own button, chip, field, heading, notice, badge, popup surface, switch, row or card from Rectangle + Text + MouseArea, and never copies a component's file to tweak it.
- **Any change to the design system needs Mark's approval before it is built** — a new component, a new `style` / `size` / `kind` variant, a changed look or behaviour, a new or changed Theme token, or retiring a component. Propose the need, the screens affected and a before/after picture, then wait. This binds Claude sessions and agents too: if no component fits, stop and ask; never build a local copy.
- **The rules are tested.** `design_system_lint_test` (ctest) reads every screen's QML and fails on a hand-drawn control (D1), a raw text input (D2), a popup that draws its own surface (D3), a literal colour (D4) or space that ignores Density (D5). Deliberate exceptions live in `src/Gui/tests/design_system_allowlist.txt`, each with its reason; adding one is a design-system decision. `design_system_tokens_check` fails when `tokens.json` is stale.
- Colour is never the only channel: every tone comes with a shape, a word, or both.
- Flat: no drop shadows, blur or decorative images; the only gradient is the display title's brand sweep.
- Weight carries hierarchy, not size. Body emphasis is `Font.Medium` at most.

## Themes

- Seven aesthetics × light and dark = 14 themes, selected by `Theme.themeIndex` (2 × place in `themeOrder`, + 1 for dark). Instrument (index 0) is the default.
  - **Instrument** — warm paper and amber; Georgia (Gelasio where absent) · DM Mono · the serif at Bold.
  - **Editorial** — ivory and ink blue; Instrument Sans Light · JetBrains Mono · Source Serif 4 italic.
  - **Studio** — cool greys and electric blue; Hanken Grotesk Light · Geist Mono.
  - **Vector** — square corners, signal orange; Space Grotesk Light · Space Mono.
  - **Terrain**, **Links**, **Folio** — the same token set. Folio also changes spacing (`spacingScale` 1.15), heading case (sentence case), card rules (none), chart furniture and titles (flat ink); components get all of that through the tokens, never by testing the theme.
- `tokens.json` currently documents the four original aesthetics in detail; Terrain, Links and Folio values are in Theme.qml.
- A new theme is one `_themes` entry plus its name in `themeOrder`; run `tools/theme/metric_palette.py` for its metric colours and `make_design_system.py` for the docs.

## Content

- Second person, plain words, sentence case: "Your hips open early on the downswing." "Seen in 7 of your last 10 swings."
- Two layers. **The golfer layer** (home screen): no figures, no jargon, no claims about cause — "Picked first: …", never "because". **The technical layer** (Swing diagnostics, session screens): technical names and counts are allowed — "124 of 135 swings", "Δ plane".
- Write headings in sentence case in the source and let the theme set the case: `Theme.caps(qsTr("Your focus"))` or `font.capitalization: Theme.capsFont`. Acronyms stay capitals in the source (IMU, DTL, GRF, LM, P1…P10). `.arg()` goes after `caps()`.
- Units are never capitalised: `Theme.capsUnit("Loft", "°")` → "LOFT (°)".
- Links say where they go: "Swing diagnostics →", "← Home".
- A verdict has words as well as a colour: `Theme.verdictWords()` — "in range", "watch", "outside"; trends "↑ growing", "↓ easing".
- No emoji.

### Variability words

Every variability word, line and accessible name lives in one of four singletons in `src/Gui/components/`, through `qsTr()` with a translator comment naming the vocabulary. No screen, data pack or presenter spells one. Arrow glyphs and the " · " separator stay outside the translatable strings.

**Consistency** (`ConsistencyVocabulary`: Dispersion, Overlay, the funnel). How the golfer repeats.

| grade | word | golfer-layer line |
|---|---|---|
| 5 | Grooved | You repeat this almost exactly, swing after swing. |
| 4 | Repeatable | This stays much the same from one swing to the next. |
| 3 | Settling | This is starting to repeat, with some swings still drifting. |
| 2 | Variable | This changes noticeably from swing to swing. |
| 1 | Scattered | This is different almost every swing. |
| 0 | Not enough swings | Hit a few more and we'll show how consistent this is. |

**Measurement** (`MeasurementVocabulary`: the Needle only). How far a reading can be trusted. The words describe the measurement, never the golfer, so always name the reading: "Pelvis IMU · Steady".

| grade | word | line |
|---|---|---|
| 5 | Locked | This reading is rock steady. Even small changes are real. |
| 4 | Steady | This reading is reliable. Small noise won't change what it tells you. |
| 3 | Fair | This reading is usable. Treat small changes with caution. |
| 2 | Unsteady | This reading is noisy. Only large changes mean anything. |
| 1 | Noisy | This reading is too noisy to rely on. Check the setup. |
| 0 | Checking | Not enough data yet to judge this reading. |

**Trend** (`TrendVocabulary`: the funnel). The trend word always follows the grade word, never replaces it: "Repeatable · ↑ Tightening".

| token | label | golfer-layer line |
|---|---|---|
| `tightening` | ↑ Tightening | This is becoming more repeatable session by session. |
| `loosening` | ↓ Loosening | This is repeating less than it was. |
| `holding` | → Holding | No real change in how consistently you do this. |

`insufficient` or "" shows nothing. While the latest unbroken run has fewer than 3 sessions the funnel says "Trend after 3 sessions" (`tooFewSessions`; the count is `Variability.trendMinSessions`).

**Typicality** (`TypicalityVocabulary`: the two typicality variants). This swing against the golfer's usual. Never praise or fault: an unusual swing may be the best one.

| band | word | golfer-layer line |
|---|---|---|
| `usual` (\|z\| ≤ 1) | Your usual | This swing looked like most of your swings. |
| `aLittleOff` (1 < \|z\| ≤ 2) | A little off | This swing was a little different from your usual. |
| `unusual` (\|z\| > 2) | Unusual | This swing was quite different from your usual, for better or worse. |

- Zone labels: "most swings" (±1 SD, `zoneMost`) and "nearly all" (±2 SD, `zoneNearlyAll`), in lower case. Never "normal" or "abnormal".
- Accessible names: "Consistency: Repeatable (4 of 5)", "Measurement: Steady (4 of 5)", "Consistency: Not enough swings (0 of 5)"; typicality has no grade, "Typicality: Your usual". The funnel: "Pelvis rotation at top: Repeatable, tightening over 6 sessions".

**Phrasing.**
- Golfer layer: the word, and its line as the tooltip. No figures: no σ, ρ, CV or counts. No causal claims.
- Technical layer: the word, then " · " and the basis string in the data face (`fontSzDataSm`, `colorText2`). Every basis string is built in C++ from computed values (`src/Metrics/variability/`), never hand-written, and shows only what was computed. An empty one shows nothing.
- Keep existing headings. "Consistency" stays a heading; the grade words and lines under it are what change.

**Basis strings**, as the grader builds them:

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

## Colour

- Ground: `colorBg` (page), `colorBg2`, `colorBg3` (recesses, strips, hover), `colorSurface` (cards and panels). Chrome recedes: `colorRail`, `colorToolbar`.
- Ink: `colorText` (headlines, values), `colorText2` (prose, supporting copy), `colorText3` (captions, micro labels, asides only — it is about 2–3:1 in most themes, so never body copy).
- Hairlines: `colorBorder` (card edge), `colorBorderMid` (controls, unlit marks), `colorBorderStrong` (an unselected control), `colorHairline` (between table rows). Borders are 1 px at translucent ink.
- One accent: `colorAccent` (links, focus, selected borders, the current tab, primary buttons), `colorAccentLight` (selected fill), `colorAccentMid` (hovered border). Ink on an accent fill: `colorOnAccentBright` (buttons, segments) or `colorOnAccent` (checks, filters).
- Status, by meaning, never by panel: `colorGood` (in range, connected, easing), `colorAttention` (do this next, watch), `colorWarn` (a fault or habit — the coral, not an alarm), `colorError` (failure only), each with a `*Light` wash. Map words to colours with `Theme.toneColor()` / `toneLight()`; scores with `qualityColor()` + `qualityMark()`; corridors with `corridorFill()` / `corridorTone()`. A table cell's heat is `colorHeatSome`, `colorHeatLoose` and `colorHeatAfter` (`colorAttention` at three strengths, through `roles.heat()`): how much, never good or bad, with its word in the cell.
- A card has ONE tone; its rule, heading, badges, meter and pips all wear it. A relation (neither praise nor fault) uses `gradientCool`.
- A catalogue metric always draws in its own identity colour: `Theme.metricColor(key)`. Other series: `Theme.chartSeriesColor(i)`. IMU sensors A–D: `colorImuA`–`colorImuD`.
- Over video: the pose palette (`poseBone`, `poseInk`, `poseSpine*`) is fixed in every theme; text on the picture is `colorOnScrim` on `colorScrim`.
- Weak pairs kept from the source (see each token's usage note): `colorText3` everywhere but Instrument Dark; `colorAttention` in Vector Light and Studio Light; `colorWarn` in Studio Light.

## Type

- Three families per aesthetic: `Theme.fontBody`, `Theme.fontData` (every figure), `Theme.fontDisplay` (titles only). Set body weight with `Theme.fontBodyWeight`, never `Font.Light` directly.
- Sizes: `fontSzHero` (home title only), `fontSzDisplay`, `fontSzHeading` 16, `fontSzBody` 13, `fontSzBody2` 12, `fontSzLabel` 11, `fontSzMicro` 10, `fontSzDataLg` 26, `fontSzData` 20, `fontSzDataSm` 13 — all × the user's `fontScale`.
- Tracking: `trackingMicro` on micro labels, `trackingLabel` on labels, `trackingData` on figures.
- One display headline per screen (the hero card). Prose at line height 1.45, at most `sp(720)` wide; item headlines at 1.3.

## Space and density

- `Theme.sp(n)` is the size of a thing (type, marks, strokes, chrome). `Theme.gap(n)` is the space between things (padding, margins, spacing) and follows the theme's `spacingScale` and the user's **Density** setting.
- Density (Settings → Appearance → Density; `Theme.density`, persisted as `ui/density`):

  | | Compact | Default | Spacious |
  |---|---|---|---|
  | `gap(n)` × `densityScale` | 0.75 | 1 | 1.25 |
  | `rowH` | 40 | 48 | 56 |
  | `rowH2` | 48 | 56 | 64 |
  | `itemRowH` | 32 | 40 | 48 |
  | `tableHeaderH` | 32 | 38 | 44 |
  | `rowPadH` | 14 | 18 | 22 |
  | `itemIndent` | 58 | 62 | 66 |

  Default is the app as it always was. Anything that is space must use `gap()` (or a row token) or it will not respond to density; anything that is size must use `sp()` so it does not.
- The typeset layer (`PpTypeset` and its roles objects) has the same pair: `u(n)` is size and `g(n)` is space. On screen `g(n)` is `Theme.gap(n)`; in print it equals `u(n)`, so a printed page never depends on a screen setting. Never put `Theme.gap` on a path that prints.
- Space that does not follow Density is an error: spacing, margins and layout padding from `sp()`, `u()` or a literal of 4 px or more. Negative margins that enlarge a hit area, and 1–3 px optical nudges, are size and pass.
- At Default: cards sit `gap(16)` apart, two side by side down to `sp(640)`; a card pads `gap(20)`; items inside sit `gap(18)` apart.
- A centred column is `Theme.contentWidth(parent.width)`; every side panel is `Theme.sidenavWidth`.
- Corners: `Theme.radius` for controls, `Theme.radiusLg` for cards, tables and popovers (Vector is square).

## Cards and marks

- A card (`PpCard`): `colorSurface` in a 1 px `colorBorderMid` hairline, a `Theme.cardRule` tone rule across its top following the corners. At most one hero card per screen: `heroRule`, a tone wash and tone hairline when `Theme.heroTinted`.
- Badge (`PpBadge`): a tinted circle with a tick (good), target (to work on), down arrow (easing) or dashed ring (unconfirmed).
- How-often meter (`PpMeter`): ten segments, filled in the tone, unlit in `colorMeterUnlit`, always followed by words.
- Session pips (`PpPips`): filled = seen, a `colorBorderStrong` ring = not seen, oldest first.
- Band bar (`PpBandBar`), strength mark (`PpStrengthMark`: CLEAR, LIKELY, POSSIBLE), quality pill (`PpQualityPill`), swing timeline (`PpSwingTimeline`: ADDRESS, BACK, TOP, DOWN, IMPACT, FINISH).

### Variability marks

How much something varies, or whether a reading can be trusted, as a shape and a word. Components: [PpVariabilityMark](components/PpVariabilityMark.md), [PpVariabilityBadge](components/PpVariabilityBadge.md) (the mark and its word: what a screen uses) and [PpVariabilityFunnel](components/PpVariabilityFunnel.md). Their words are under Content, "Variability words".

**Principles.**
1. A word always goes with the mark. Colour and shape are never the only channel.
2. The mark shows a grade, never raw SD. Spread is judged against a scale that matters for the measure (its corridor), so equal σ can earn different grades. No scale, no mark.
3. Consistency is precision, not accuracy: spread about the golfer's own mean. Repeating the same wrong position is Grooved; diagnostics says separately that it's wrong.
4. Measurement quality is a different question ("can we trust the number?"), with its own mark, the Needle, and its own words.
5. Typicality is not a judgement. An unusual swing may be the best one; whether a swing is good is the corridor's job.
6. Certainty is not variability. How sure a finding is uses `PpStrengthMark`.
7. One visual family: one geometry language, one set of tones and words.

**The family.**

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

**Where a consistency mark is allowed.** All three must hold: the spread is repeatability (the same measure, across swings, in comparable conditions); the spread isn't expected (never across clubs, shot types or contexts, never movement within one swing); and a grade exists.

**Classifying a display.**

| class | when | treatment |
|---|---|---|
| Replace | Golfer layer, eligible | Remove the σ or spread-only box plot. Show the badge, its line as the tooltip. |
| Pair | Technical layer, eligible | Keep the σ and any box plot. Add the badge with its basis string. |
| Measurement | The figure describes the measurement, not the golfer | The Needle with measurement words. |
| Typicality | A per-swing "vs average" or outlier display | Your group, or trace on envelope. |
| Funnel | A multi-session spread or trend display | The spread funnel. |
| Leave | Not eligible, or ungraded | Unchanged. Record the reason. |

**Rules.**
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

**Not repeated here.** The geometry tables (spec §4 and §6: coordinates, per-grade arrays, alphas) live in `docs/design/variability-mark/spec.md` and are built in `src/Gui/variability/variability_geometry.cpp`. The grade basis and its provisional constants (spec §8) are in `src/Metrics/variability/`. Decisions, gaps the build filled and open items: `docs/design/variability-mark/decisions.md`.

## Motion

- Screens cross-fade only (`Theme.durationNormal`); hover eases opacity or alpha (`durationFast`); colour changes animate over `durationSlow`. All are 0 under `Theme.reduceMotion`.
- Never animate a layout's x, y, width or height. A small press scale is allowed on buttons only (`PpPressable`).

## Iconography

- No icon font. Rail and home-tile icons are Unicode symbol glyphs (⌂ ▶ ◑ ⚙ ⌖ ⇅ ✦) set in `Theme.fontSymbol` and sized with `Theme.symbolScale(glyph)` so their ink heights match.
- Marks (tick, target, arrows, chevrons) are drawn as geometry in the tone colour, round caps, about `sp(1.6)` strokes.
- The app icon (`src/Resources/icons/pinpointstudio.*`) is a fixed full-colour mark; never recolour it to a theme.
