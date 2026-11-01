# PpMeter

How often, as ten segments: a share of 0..1 lights round(share × 10) of them in the tone.

Source: `src/Gui/components/PpMeter.qml`.

**Use** in a table row to show how often a pattern appears (the how-often meter). The caller prints the figure beside it when exactness matters; the segment count carries the reading, so colour is never the only channel.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `share` | real | `0` | 0..1 |
| `tone` | color | `colorAccent` | lit segments |
| `filled` | int (ro) | | clamp(round(share × 10), 0, 10) |

**Geometry** (all theme tokens, scaled with `sp()` — a segment is part of the mark, so its gap is `sp`, not `gap`): segment `meterSegW` 7 × `meterSegH` 6 px, `meterSegGap` 2 px apart, corner `meterSegRadius` 1 px; unlit `colorMeterUnlit`.
