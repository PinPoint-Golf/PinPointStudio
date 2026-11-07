# PpTypeset

The per-panel helper of the typeset system (design system §15.3): it turns Theme's typeset data into what a block draws with — the scale, the type roles, the inks, the marks, the size classes, the flows and tiers, and the small generic text and figure helpers.

Source: `src/Gui/typeset/PpTypeset.qml`.

## When to use
Any panel or register drawn with the typeset blocks (Session diagnostics, Charts, Trust, the launch monitor board, the session report, Home's tab bar). Make ONE per panel and pass it to every block as `roles`; the blocks read their fonts, inks and sizes from it, never from Theme directly.

It is a `QtObject`, **not a singleton**: the medium belongs to the instance, so one window can show a panel on screen and lay out its printed report at the same time.

Not for: a panel's own words or column tables — those live in a subclass. **PpDiagRoles** (Session diagnostics) and **PpChRoles** (Charts) are `PpTypeset { … }` subclasses that add their tables through `extraFlows` / `extraTiers` / `extraElements` and add their own functions. Nothing of one panel belongs in PpTypeset. Not for the app's chrome either (cards, buttons, pills): those are `src/Gui/components/` and read Theme.

## Consumer supplies
`medium` ("screen" or "print"), and for a document that leaves the studio optionally `fixedAesthetic`. A subclass supplies its `extraFlows`, `extraTiers`, `extraElements`.

- **Never give it the id `roles`.** In `roles: roles` the block's own property shadows the id and the block silently uses its default.
- **Declare it LAST in the file** (children are destroyed in creation order).

## The scale: size, space, medium

| function | screen | print | what it is for |
|---|---|---|---|
| `u(n)` | `Theme.sp(n)` (= round(n × `fontScale`)) | round(n × `printScale` × 2) / 2 — half-pixel steps; `printScale` 1.18 (body 11 → 13 px ≈ 10 pt), never the user's `fontScale` | **SIZE** of a thing: type, marks, strokes, chrome |
| `g(n)` | `Theme.gap(n)` (= round(n × `fontScale` × `spacingScale` × `densityScale`)) — follows the theme's spacing and the user's **Density** setting | exactly `u(n)` — a printed page never depends on a screen preference | **SPACE** between things: spacing, margins, padding |
| `gutter(w)` | `g(r.gutter[sizeClass(w)])` (fallback `g(14)`) | the same through print `g` | space between columns; it is a `g`, so it is space |

`n` is a design unit. The typeset type sizes are design units too (body 11, small 9.5, label 9, micro 8.5, stand 12.5, title 16, feature 21 by default); every role's pixel size is `Math.round(u(size))`.

`medium`: `"screen"` (default) or `"print"`; `isPrint` is the read-only test. In print, besides `u`/`g`: `paper` is `#FFFFFF`, the hairline width `hw` is `p.hair` (0.5) instead of 1, washes are scaled by `tintK` = `p.tintScale` (0.7), and the picked ring `ringD` uses `p.ring` (12). `p` (= `Theme.typesetPrint`) also carries the A4 page geometry PpPager uses: `marginX` 60, `headY` 30, `top` 76, `bottom` 92 (A4 at 96 dpi logical), and the Charts keys that differ on paper (`ch: { boxFill, kindCurveEm }`).

## Size classes and tiers

`em` = the body role's pixel size (`fBody.pixelSize`). A width is classed by how many ems it holds:

| class | width | rank | gutter (design units) |
|---|---|---|---|
| `wide` | ≥ 90 em | 4 | 20 |
| `medium` | ≥ 60 em | 3 | 20 |
| `compact` | ≥ 42 em | 2 | 14 |
| `narrow` | < 42 em | 1 | 12 |

- `sizeClass(w)`, `rank(cls)`, `atLeast(w, cls)`.
- **Tiers** — the responsive rule as data: 1 always · 2 compact+ · 3 medium+ · 4 wide only. `shows(tier, w)` takes a number or an element key. Below its tier an element moves to its `else` (fold, don't drop). Shared elements (`tiers.elements`):

  | element | tier | else |
  |---|---|---|
  | `tabCounts` | 2 | each view's head states its count |
  | `breadcrumb` | 2 | the head names the subject; Back stays |
  | `reviewNote` | 3 | a note at the foot of the view |
  | `rail` | 4 | head above; legend, bookends, footnotes below |
  | `margin` | 4 | notes under the figure, in prose columns |
  | `longStandfirst` | 3 | short form; the rest opens a later section |

  `railFolds(w)` / `marginFolds(w)` name the two fold decisions.
- **Flows** — minimum measures in em: `prose` 26, `measure` 44, `index` 20, `contents` 22, `tile` 22, `keyFigure` 10, `chart` 30, `proseTwo` 62, `rail` 18, `margin` 18, `sparkline` 7 (plus a subclass's `extraFlows`). `columns(w, minEm, gap)` = ⌊(w + gap) / (minEm·em + gap)⌋, at least 1, gap defaulting to `gutter(w)`. `wrapColumns(n, fit)` wraps n cells as one row, else two, else four. `measureW(w)` = min(w, 44 em). `castZoom(w, h, rowsEm)` scales a second-screen cast between 1 and 1.6.

## Props (data)
| name | type | default | meaning |
|---|---|---|---|
| medium | string | "screen" | "screen" · "print" |
| isPrint | bool (ro) | | `medium === "print"` |
| fixedAesthetic | string | "" | Name an aesthetic (e.g. "folio") to draw in its LIGHT inks and its type, read from Theme's theme table — never the live tokens, and without touching `Theme.themeIndex`. "" follows the app. |
| r | var (ro) | | The merged typeset table: `Theme.typeset`, or the fixed aesthetic's. |
| p | var (ro) | | `Theme.typesetPrint`. |
| extraFlows / extraTiers / extraElements | var | {} | A subclass's vocabulary, merged over the generic tables. |
| flows / tiers | var (ro) | | The merged tables. |

## Type roles (fonts, read-only)
Families, sizes and weights come from `r` (a theme's `typeset` block overrides `_typesetDefault`).

| role | family | size | weight / extras |
|---|---|---|---|
| `fFeature`, `fPull` | display | feature | display |
| `fTitle` | display | title | display |
| `fStand` | stand | stand | text |
| `fBody` / `fBodyM` / `fBodyL` | text | body | text / strong / text + link underline |
| `fSmall` / `fSmallM` / `fSmallI` / `fSmallL` | text | small | text / strong / italic if `italicNotes` / link underline |
| `fMicro` | text | micro | text |
| `fLabel` / `fLabelL` | label | label | `labelTracking`, all caps if `labelCaps` (L: + link underline) |
| `fKicker` | label | small | `kickerWeight`, label tracking and caps |
| `fSection` | text | small if `sectionCaps`, else body | `sectionWeight`, `sectionTracking`, caps if `sectionCaps` |
| `fFig` / `fFigS` / `fFigLabel` / `fFigL` | figure | body / small / label / title | text |
| `fTab` | label if `labelCaps`, else text | label or body | label tracking and caps when `labelCaps` |
| `fLabelUnit` | label | label | label tracking, case as written |

Units are never capitalised: a label that carries a unit is set in `fLabelUnit` with its text from `labelUnit(words, unit)` — `labelUnit("Peak", "°/s")` → "PEAK °/s" in Vector, "Peak °/s" in Folio. `labelCase(s)` applies the label role's case.

## Inks and marks (read-only)
- **Ink:** `ink`, `ink2`, `ink3` (Theme `colorText`/`2`/`3`, or the fixed aesthetic's light values), `warn` ("outside its corridor", and nothing else), `attention` (Theme `colorAttention`, or the fixed aesthetic's: a table cell's heat, never a fault), `good` (Theme `colorGood`, or the fixed aesthetic's: a variability mark's grades 4–5 on paper), `accent`, `paper` (`colorBg`; white in print), `link` (accent when `r.linkAccent`, else ink2), `rule` / `hair` (ink at `r.ruleAlpha` / `r.hairAlpha`), `hw` (hairline width), `tintK`.
- **Chart furniture:** `grid`, `gridOpacity`, `baseline`, `baselineOpacity` (via `Theme.gridColor` / `gridOpacity` / `baselineColor` / `baselineOpacity`), `wash` (THE chart wash: the corridor, ink at `r.zoneTint × tintK`), `heat(level, soft)` (a table cell's heat, the one exception: `attention` at the strength of `colorHeatSome` for level 1, `colorHeatLoose` for 2, `colorHeatAfter` when `soft`, × `tintK`), `fault` (warn at `r.faultAlpha`), `passEdge` (ink3 at `r.passAlpha`, dashed).
- **Focus:** `pickWash` (ink at 0.10 dark / 0.07 light), `hoverWash` (0.065 / 0.045), `rowHover` (0.05 / 0.03), `dim` 0.28 (what recedes under a highlight); `fast` / `normal` = `Theme.durationFast` / `durationNormal`.
- **Marks** (one shape per theme): `square` (`r.markShape === "square"`), `markFired`, `markClean`, `ringD`, `stripPitch`, `stripBar` (≥ 2), `stripH`, `stateColor(s)` (fired → warn, else ink3).
- **Strip at any session length:** `stripMinPitch` (max(2.5, u(3))), `stripMaxShots` 22, `stripWidthFor(n)`, `stripFit(n, avail, pitch, barW, barH, rowGap)` → `{ pitch, perRow, rows, barW, width, height }` (narrow the pitch, then wrap; never clip, never drop a shot), `stubH(barH)`, `shotLabelStep(pitchPx, labelPx)`, `axisLabelShown(i, n, step, keep)`.
- **Link grades:** `linkGrades` (movedTogether, conditionallyDependent, coherent, presentTogether, unanchored — glyph, word, stroke, width, arrow, opacity, tone), `linkGradeOrder`, `linkStyle(l)`, `dashFor(stroke)`, `dashPath(ctx, x0, x1, y, pat)` (a dashed rule drawn dash by dash so each stays crisp).

## Text and figure helpers
- **Case and escaping:** `cap1`, `lower1`, `sentence`, `esc`, `hex`.
- **Figures** — the QML twin of `Core/figure_format.h`, implemented once in `typeset/figure_format.js` (imported as `Figures`): `decimalsFor(unit)`, `figure(v, dec, trimZeros)`, `figureSigned(v, dec)`, `withUnit(f, unit)`, `figureUnit(v, unit)`, `shortUnit(unit)`, `num(v)` (one decimal, zeros trimmed), `list(xs)` ("a, b and c"). Missing is "-", never 0; the minus is U+2212; never "−0". A file with no PpTypeset to hand imports `figure_format.js` directly.
- **Words:** `word(n)` (zero to twenty in words), `countWord(n)` (zero is "none").
- **Lookups:** `byId`, `byKey`, `listAt`, `fin(v)` (a drawable number), `withHLines(lo, hi, ls)`.
- **Inline HTML:** `figHtml(s, c)` (the figure face inside a sentence), `strongHtml(s)`, `toneHtml(s, c)`.
- **Shots:** `shotNo(readout)` (the ordinal, shotIndex + 1, never the id), `shotCountOf`, `shotOfText` ("shot N of M"), `indexOfShot(ticks, id)`, `sessionMeta(source, readout, longMonth)` → `{ golfer, date, club, shot }` for running heads.
- **Pulse:** `takePulse(brush, cue, token)` hands a register's after-shot cue to its PpBrush once per token.

## Tokens
Reads `Theme.typeset` (`_typesetDefault` merged with the aesthetic's `typeset` block — Vector and Folio carry one), `Theme.typesetPrint`, `Theme._themes` (fixed aesthetic only), `Theme.sp`, `Theme.gap`, `Theme.colorText`/`2`/`3`, `colorWarn`, `colorAttention`, `colorHeatSome`, `colorHeatLoose`, `colorHeatAfter`, `colorAccent`, `colorBg`, `Theme.dark`, `gridColor`, `gridOpacity`, `baselineColor`, `baselineOpacity`, `durationFast`, `durationNormal`.

## Variants
- **Default aesthetics:** round marks (fired 7, clean 5, hollow clean, ring 14), labels mixed case, links in accent.
- **Vector:** square marks (6 / 4, clean filled, ring 12, bar radius 0), labels and section heads in tracked capitals in Space Mono, links ink with an underline.
- **Folio:** display and standfirst in the serif, the direction sentence italic, lighter rules and wash.
- **Print:** see the scale table; the session report and print pages also set `fixedAesthetic: "folio"`.

## Usage
```qml
// a register: one helper, passed to every block
PpLedgerTable { roles: ts; … }
PpSectionHead { roles: ts; number: "1"; title: qsTr("Session patterns") }
PpTypeset { id: ts; medium: root.medium }        // last in the file; never id: roles
```

## Used by
Instantiated directly in `report/PpSessionReport.qml` (print, Folio), `session/PpLmGraphicsBody.qml`, `trust/PpTrustPanel.qml`, `home/HmHomeTabs.qml`, `cameras/PpCameraFrame.qml`, `review/PpDataViewer.qml`, `shot/PpSwingEditPanel.qml`, `swing3d/SwingViz3DView.qml`, and as a default in `diagnostics/PpDiagReviewFoot.qml` and the typeset blocks. Subclassed by `charts/PpChRoles.qml` (Charts register; the print report pages Delivery, Posture, Wrist, Work-on) and `diagnostics/PpDiagRoles.qml` (Session diagnostics Analyse, Replay and Print).
