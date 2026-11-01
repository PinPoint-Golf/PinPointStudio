# PpQualityPill

A shot-quality score pill, its fill graded by quartile (red → rust → amber → green) via `Theme.qualityColor`.

Source: `src/Gui/components/PpQualityPill.qml`.

**Use** the small pill on a shot card and the large "NN /100" at the head of the review panel. Set `mark` where nothing beside the pill says the band in words, so it reads without its colour.

The consumer supplies `score` (0–100).

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `score` | int | `0` | the shot's quality |
| `large` | bool | `false` | 24 px high, heading-size number + "/100" |
| `mark` | bool | `false` | lead with the band's PpBadge: a tick (≥ 75) or a target (below) |

**Fill:** `qualityColor(score)` at 50 % alpha — `colorError` < 25, `colorWarn` < 50, `colorAttention` < 75, else `colorGood`. **Ink:** `colorOnAccentBright` (white in light, the page ground in dark), full opacity.

**Geometry.** Small: 17 px high, content + 14 px, capsule, micro data 10 px. Large: 24 px high, content + 22 px, number at `fontSzHeading` 16 px, "/100" micro at 80 % opacity, nudged 2 px down. Mark badge 13 / 18 px, plain-less (its own 12 % wash), 2 px gap.

Bands for filter chips: 0–24, 25–49, 50–74, 75–100 (`qualityBands`).
