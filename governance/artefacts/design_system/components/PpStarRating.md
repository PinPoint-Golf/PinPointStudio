# PpStarRating

A row of ★ stars: read-only on shot cards, interactive in the review panel, and the threshold picker in the filter.

Source: `src/Gui/components/PpStarRating.qml`.

The consumer supplies `value` and, when interactive, writes `rated(newValue)` back to its model (no state is kept here). Tapping the star equal to the current value clears it (emits `rated(0)`).

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `value` | int | `0` | stars lit |
| `max` | int | `5` | stars drawn |
| `interactive` | bool | `false` | tappable, pointing hand, hit target 2 px beyond each star |
| `starSize` | int | `fontSzMicro` (10 px) | glyph size |
| `onColor` | color | `colorAttention` | lit star |
| `offColor` | color | `colorText3` | unlit star |
| signal `rated(int newValue)` | | | |

Stars are 2 px apart (`gap(2)`), set in `Theme.fontSymbol` (Segoe UI Symbol / Apple Symbols) so they never render as colour emoji.
