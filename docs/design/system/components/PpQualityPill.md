# PpQualityPill

A shot-quality score pill, its fill graded by quartile (red → rust → amber → green) via `Theme.qualityColor`.

Source: `src/Gui/components/PpQualityPill.qml`.

**Not used while the shot score is retired (#186).** Until the shot score is built, the shot card and the shot action bar draw no pill, the carousel filter has no quality-band option (it filtered on the same number), the folded-strip shot picker's readout draws no score, and the sessions drawer has no "avg q" column. The score is still computed and stored. The component stays in the design system; retiring it is a separate Design Lead decision.¹

**Use** (when a score is shown again) the small pill on a shot card and the large "NN /100" at the head of the review panel. Set `mark` where nothing beside the pill says the band in words, so it reads without its colour.

The consumer supplies `score` (0–100).

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `score` | int | `0` | the shot's quality |
| `large` | bool | `false` | 24 px high, heading-size number + "/100" |
| `mark` | bool | `false` | lead with the band's PpBadge: a tick (≥ 75) or a target (below) |

**Fill:** `qualityColor(score)` at 50 % alpha — `colorError` < 25, `colorWarn` < 50, `colorAttention` < 75, else `colorGood`. **Ink:** `colorOnAccentBright` (white in light, the page ground in dark), full opacity.

**Geometry.** Small: 17 px high, content + 14 px, capsule, micro data 10 px. Large: 24 px high, content + 22 px, number at `fontSzHeading` 16 px, "/100" micro at 80 % opacity, nudged 2 px down. Mark badge 13 / 18 px, plain-less (its own 12 % wash), 2 px gap.

Bands for filter chips: 0–24, 25–49, 50–74, 75–100 (`qualityBands`).

---

¹ The Design Lead, 24 Nov 2026 (platform refactor judgement calls PR-32 and PR-37). **Still showing the score's tint and mark:** the folded-strip shot picker's chips, which have no neutral style to fall back to; un-tinting them waits for the Design Lead. The wrist score itself still shows, named as such and only where one exists, in Charts' wrist section and the report's wrist page.
