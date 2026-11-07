# PinPoint Studio design system

PinPoint Studio is a desktop instrument for golf swing analysis: quiet chrome, flat surfaces, hairlines, and one accent, so the golfer's video, figures and verdicts read first.

**Read this before writing any QML.** It is the rule book; the values live in one place:

- `src/Gui/theme/Theme.qml` — the source of truth. Every colour, font, size and space a component uses is a `Theme.*` property.
- [`tokens.json`](tokens.json) — every Theme value with a usage note, per theme. **Generated** from Theme.qml by `tools/theme/make_design_system.py`; never edit it by hand (`--check` fails when it is stale).
- [`components/`](components/README.md) — one page per shared `Pp*` control: when to use it, its properties, the tokens it reads, its states.
- The design system is this folder and Theme.qml; nothing is synced to a claude.ai artifact (Mark, 7 Nov 2026). The visual reference is the component pages and the previews they show (the variability marks' in `docs/design/variability-mark/previews/`).
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
- Variability words (Grooved … Scattered, Locked … Noisy, the trend and typicality words) live in four vocabulary singletons; no screen spells one. The golfer layer shows the word with its line as the tooltip, never a figure; the technical layer adds the grader's basis string. Every word, line and basis string: [variability.md](variability.md#words).

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
- Variability marks (`PpVariabilityMark`, `PpVariabilityBadge`, `PpVariabilityFunnel`): how much something varies, or how far a reading can be trusted, as a grade in shape and word — Dispersion and Overlay for consistency, the Needle for measurement, the funnel for the trend, two ungraded typicality variants. When one is allowed, how to classify a display, and the rules: [variability.md](variability.md).

## Motion

- Screens cross-fade only (`Theme.durationNormal`); hover eases opacity or alpha (`durationFast`); colour changes animate over `durationSlow`. All are 0 under `Theme.reduceMotion`.
- Never animate a layout's x, y, width or height. A small press scale is allowed on buttons only (`PpPressable`).

## Iconography

- No icon font. Rail and home-tile icons are Unicode symbol glyphs (⌂ ▶ ◑ ⚙ ⌖ ⇅ ✦) set in `Theme.fontSymbol` and sized with `Theme.symbolScale(glyph)` so their ink heights match.
- Marks (tick, target, arrows, chevrons) are drawn as geometry in the tone colour, round caps, about `sp(1.6)` strokes.
- The app icon (`src/Resources/icons/pinpointstudio.*`) is a fixed full-colour mark; never recolour it to a theme.
