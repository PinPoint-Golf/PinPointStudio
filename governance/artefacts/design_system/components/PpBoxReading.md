# PpBoxReading

A stood-up box plot's full reading while the box is examined: the upper whisker end, Q3, the median, Q1 and the lower whisker end, each a small figure at its own height with a hairline tick from the box edge, and the count line ("15 shots · 1 outlier") under them.

Source: `src/Gui/typeset/PpBoxReading.qml`.

## When to use
You normally don't place it yourself: PpBoxPlot draws it for a VERTICAL box (`vertical: true`) when the box is examined, and exposes it as `reading`. Session diagnostics' Spread box and across-sessions strip, and the Charts boxes stood up at the P-positions of an opened reading, show it this way. Callers steer it through PpBoxPlot's `footer` and `reading*` properties.

Not for: a box lying along its axis — PpBoxPlot lays those values on the plot itself in two lanes. Not a tooltip or popup (`PpPopoverCard` is the app's popup surface).

## Consumer supplies
(Through PpBoxPlot.) `items` with each value's true height and its formatted label, the box's edges, and the room it may use.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | null | The panel's PpTypeset. No roles, nothing drawn. |
| items | var | [] | `[{ y, text, strong, f }]` in any order: a value's TRUE height (this item's coordinates), its label, `strong` for the median (set in ink), `f` the caller's own value riding along. |
| footer | string | "" | The count line. |
| footLimit | real | 1e6 | How far down the reading may reach before its count line goes above it instead. |
| edgeL / edgeR | real | 0 | The box's left and right edges. |
| roomLeft / roomRight | real | ∓1e6 | How far the reading may reach on each side. |
| gutterX / gutterYs | real / var | 1e6 / [] | Where the zone-label gutter starts and the heights of its labels, to keep clear of. |
| forceSide | string | "" | "" (decide) · "right" · "left". |
| paper | color | `roles.paper` | The backing colour. |

Read-only: `ok`, `side`, `onRight`, `blockW` (its width from the box edge), `x0`, `x1` (its extent), `placed`, `footAbove`.

objectNames: `sdBoxReading` (the item), `sdBoxReadingLabel` (each value; carries `f`), `sdBoxReadingCount` (the count line).

## Typeset tokens
`roles.fFigLabel` (values), `roles.fLabel` (count line), `roles.ink` (median), `roles.ink2` (other values), `roles.ink3` (ticks, count line), `roles.hw` (tick width), `roles.paper` at 92 % (backing), `roles.u(1 / 2 / 4 / 6 / 8)` (line spacing, tick, gap).

## States
- **Side:** right by default; left when the right lacks the room or would run into a zone label in the right gutter at the reading's heights; `forceSide` overrides.
- **Crowding:** labels closer than a line are nudged apart as a group centred on their true heights; each tick bends from the TRUE height to its label, so nothing points at a value it does not hold.
- **Count line:** under the labels, or above them when it would leave the plot (`footLimit`).

## Usage
```qml
PpBoxPlot {
    vertical: true; roles: ro; map: v => sc.y(v); stats: box; texts: boxTexts
    footer: qsTr("%1 shots").arg(n)
    readingRoomRight: width; readingGutterX: gutterX; readingGutterYs: zoneLabelYs
    examined: hovered
}
```

## Used by
Instantiated only inside `typeset/PpBoxPlot.qml`; reached through `diagnostics/PpBoxStrip.qml`, `diagnostics/PpHistoryStrip.qml` and the Charts box cells stood up at P-positions.
