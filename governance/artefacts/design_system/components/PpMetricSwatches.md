# PpMetricSwatches

The metric palette as a row of round swatches — one per colour NAME, drawn in the current theme's value for it; picking one emits its name.

Source: `src/Gui/components/PpMetricSwatches.qml`.

**Use** where a metric's colour is chosen (the inspector) or a name is retuned (Settings → Appearance). What the picked name is written to is the caller's business.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `selected` | string | `""` | the name ringed as current |
| `swatchSize` | real | `sp(20)` = 20 px | diameter |
| signal `picked(string name)` | | | |

Swatches come from `Theme.metricColorNames` in catalogue order (crimson, coral, ochre, gold, moss, mint, teal, sky, cornflower, lavender, violet, orchid → tokens `metricCrimson` … `metricOrchid`), via `paletteColor`, so a user's retune shows. 6 px apart (`gap(6)`), wrapping.

**Selected:** a 2 px `colorSurface` border inside, then a 1.5 px `colorText` ring 2 px outside — the ring reads on every swatch rather than vanishing into a dark one. Hover grows 12 %; a tooltip names it (capitalised) after 300 ms.
