# PpBoxPlot

THE box plot: every box in the app draws through this one item — the middle half, the median, whiskers with caps, the caller's shot marks, an optional corridor, and its values laid on the plot while it is examined.

Source: `src/Gui/typeset/PpBoxPlot.qml`.

## When to use
Any box of a session's spread: the Charts rows, runs, traces, kinds, scatter margins and Wrist rows (through `PpChBoxCell`), Session diagnostics' Spread box (`PpBoxStrip`) and across-sessions boxes (`PpHistoryStrip`), and the page key's box swatch (`PpPageKey`), so the key cannot drift from the figures. Never draw a box from Rectangles in a panel.

It works out no number. The statistics come from the one C++ implementation, `src/Gui/typeset/box_stats.h` (type-7 quantiles, Tukey whiskers within 1.5 × IQR), published by the models; the labels come from whoever owns their formatting.

Not for: one shot's marks without a box (use `PpMark`), or a session strip (use `PpShotStrip`).

## Consumer supplies
`roles` (a PpTypeset — in practice PpChRoles or PpDiagRoles), `map(v)` → px, `stats`, and optionally `marks`, `brush`, `corridor` and `texts` with `valuesOnHover` / `examined`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | null | The panel's PpTypeset. No roles, no box. |
| vertical | bool | false | The axis runs down the item (the high values on top). |
| map | function | v → v | v (caller's units) → px along the axis; drawing snaps to half pixels. |
| cross | real | centre | The box's centre line across the axis, px. |
| stats | var | null | `{ lo, q1, med, q3, hi }`; absent or non-finite = no box. |
| bodyW | real | 9 | The box's thickness. |
| capW | real | round(bodyW / 2) | The whisker caps' length. |
| medianOver | real | 2 | How far the median rule reaches past the box each side. |
| style | var | {} | `{ print, ink, medianInk, fill }`, each optional. `print` sets paper weights. |
| lw / medW / ink / medianInk / fill | | from `style` | The same one at a time (set directly, they win). |
| marks | var | [] | `[{ at, shotId, fired, role, ringed?, size?, ringSize?, step?, interactive?, emitsHover?, … }]` — shots drawn as PpMark, side-stepped `step × stepPx` across the axis. Keep it independent of the hover. |
| echo | var | null | The hovered shot's echo mark (so the hover never rebuilds `marks`). |
| brush | var | null | The register's PpBrush. |
| stepPx | real | u(7) | One side-step across the axis. |
| corridor | var | null | `{ known, lowOpen, highOpen, signalLo, signalHi, faultLo, faultHi }` — across only. |
| texts | var | null | `{ lo, q1, med, q3, hi [, signalLo, signalHi, faultLo, faultHi] }`, formatted. |
| valuesOnHover | bool | false | Examine on its own hover. |
| examined | bool | false | Examine regardless (a caller that owns the hover, or a render). |
| footer | string | "" | The vertical reading's count line ("15 shots · 1 outlier"). |
| readingRoomLeft / readingRoomRight / readingGutterX / readingGutterYs / readingBottom | | unbounded | Room for the vertical reading (passed to PpBoxReading). |
| namePrefix | string | "chBox" | objectName prefix. |
| signal markActivated(var mark) | | | A mark was clicked. |
| signal markHovered(var mark, bool on) | | | A mark's hover changed (unless its `emitsHover` is false). |

Read-only: `hasBox`, `showingValues`, `reading` (alias of the vertical PpBoxReading), `valueLabels`.

objectNames: `<prefix>Body`, `<prefix>Median`, `<prefix>WhiskerHi`, `<prefix>WhiskerLo`, `<prefix>Mark`, `<prefix>Value`, `<prefix>Corridor`.

## Typeset tokens
`roles.ink2` (outline and whiskers), `roles.ink` (median), `roles.ink3` at 10 % (fill), `roles.hw` (print line weight), `roles.wash` (corridor), `roles.fault` (fault ticks), `roles.warn` / `ink3` / `ink` (value labels by tone), `roles.fFigLabel` (value labels), `roles.knock` when the roles define it, else `roles.paper` (label backing), `roles.markFired` / `markClean` / `ringD` (mark sizes), `roles.u(7)`.

## States
- **Screen:** outline and whiskers 1 px, median 1.5 px. **Print** (`style.print`): outline and whiskers `roles.hw`, median 1 px.
- **Corridor** (across only, when `corridor.known`): the wash between the signal edges (an open side runs to the end), a fault tick at each closed fault line.
- **Examined, across:** values in two lanes — median, whisker ends and fault lines above; quartiles and corridor edges below; a label that would collide with a more important one in its lane is left out.
- **Examined, stood up:** PpBoxReading beside the box, ticks from each true height, with the count line.
- **Marks:** a mark is ringed when `ringed` says so, else when the brush has it picked.

## Usage
```qml
PpBoxPlot {
    anchors.fill: parent
    roles: ro; brush: br
    map: function (v) { return (v - lo) / (hi - lo) * width }
    stats: row.box                       // { lo, q1, med, q3, hi } from the model
    texts: row.boxTexts
    valuesOnHover: true
    onMarkActivated: m => root.openShot(m.shotId)
}
```

## Used by
Instantiated in `charts/PpChBoxCell.qml` — which the Charts panel places in `PpChProfile`, `PpChRunBox`, `PpChReadingTrace`, `PpChKindTable`, `PpChScatter`, `PpChPositionProfile`, `PpChPostureAddress`, `PpChDeliverySession` and the report's `PpReportProfile` and `PpDeliveryPage` — `diagnostics/PpBoxStrip.qml` (Session diagnostics Spread box), `diagnostics/PpHistoryStrip.qml` (across-sessions boxes) and `typeset/PpPageKey.qml` (the key's box swatch, `namePrefix: "sdKeyBox"`).
