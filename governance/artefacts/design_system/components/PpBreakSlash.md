# PpBreakSlash

The conventional pair of slashes that marks a broken value axis — an empty end of the scale collapsed into a thin band — drawn once for every figure that breaks its scale.

Source: `src/Gui/typeset/PpBreakSlash.qml`.

## When to use
Figure-internal. Wherever a fault-forward scale (PpFigureScale) is `broken`, mark each break with this rather than hiding it: PpZonePlot (both ends of a panel), PpSparkline (left end only) and PpSmallMultiple. Never draw your own break marks.

Not for: a divider between content (use `PpDivider`) or any rule outside a figure.

## Consumer supplies
A size (normally `anchors.fill` over the plot), `roles`, the breaks' centre lines `ys`, and when it shows (`visible`).

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | var | null | The panel's PpTypeset. No roles, nothing drawn. |
| ys | var | [] | The breaks' centre lines, px in this item (PpFigureScale.breakYs). |
| k | real | 3 | One slash: k wide, 2k tall. Callers pass `roles.u(3)`. |
| leftOnly | bool | false | Only at the left end (a sparkline). |

## Typeset tokens
`roles.ink3` (stroke), `roles.hw` (line width).

## States
At each y in `ys`, a double slash at the left end and, unless `leftOnly`, at the right end; repaints when ys, k, size or the ink change.

## Usage
```qml
PpBreakSlash {
    anchors.fill: parent
    visible: scale.broken
    roles: ro; k: ro.u(3)
    ys: scale.broken ? scale.breakYs : []
}
```

## Used by
`typeset/PpZonePlot.qml` (so every PpRunChart, PpBoxStrip and PpHistoryStrip), `diagnostics/PpSparkline.qml`, `diagnostics/PpSmallMultiple.qml` — Session diagnostics.
