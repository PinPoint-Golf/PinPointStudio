# PpStrengthMark

How firmly something holds, as a signal-strength mark: 3 / 2 / 1 bars for firm / probably / possibly, with the word under it.

Source: `src/Gui/components/PpStrengthMark.qml`.

**Use** beside a finding to say how sure the evidence is. The bar count carries it, so colour is never the only channel.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `tier` | string | `"firm"` | `firm` (3 bars, "Clear"), `probably` (2, "Likely"), anything else (1, "Possible") |
| `tone` | color | `gradientCool` | lit bars |
| `bars` | int (ro) | | 3 / 2 / 1 |

**Geometry.** Three 5 px-wide slots, 18 px tall, 3 px apart, bars bottom-aligned at 8 / 13 / 18 px high, radius 1.5; unlit bars `colorBorderStrong`. The word is a PpMicro (data face 10 px) in `colorText2`, set in capitals by the theme (`Theme.caps`), tracking 0.3 (`trackingData`), 6 px below.
