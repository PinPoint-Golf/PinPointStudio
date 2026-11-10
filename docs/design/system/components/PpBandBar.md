# PpBandBar

Counts by category as one stacked horizontal bar: each category's share of the total in its tone, 1 px apart, rounded ends, square joins.

Source: `src/Gui/components/PpBandBar.qml`.

**Use** on an area row for its habit counts, and for a club's shot mix (Home's Tendencies rows, the session panel's `sdClubMix`, the report's Tendencies page, where a typeset caller sets `height` in `u(n)` and passes its roles' tones). The bar says how many in proportion only — the caller prints the count beside it ("3 habits"), so colour is never the only channel.

The consumer supplies `segments` in draw order, typically via `Theme.bandTone`: habit → `colorWarn` (the fault coral, never the alarm red), occasional → `colorAttention`, clean → `colorGood`, value → `gradientCool` (a shot-mix part that is neither praise nor fault: Fade, Draw, Low, Mid, High; Mark, 10 Nov 2026), unclear → `colorMeterUnlit`.

A club's shot mix maps each part's `kind` the same way everywhere (Home, the session panel, the report): good → clean, value → value, main → habit, other → occasional. A typeset caller reads the same tones from its roles: `good`, `cool`, `warn`, `attention`.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `segments` | var | `[]` | `[{ count, tone }]`; zero/missing/negative counts draw nothing |
| `gap` | int | `1` | px between segments |
| `minSegment` | int | `sp(2)` = 2 px | a count that is there is never rounded away |

**Geometry.** Implicit 160 × `bandBarH` (6 px); radius = height / 2 on the outer ends only. Widths: each drawn segment gets the floor, the rest shared by count with cumulative rounding. Nothing to count → a plain `colorMeterUnlit` track.
