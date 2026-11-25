# PpZonePlot

The common ground every value panel stands on: the corridor wash, the zone edges (fault in warn, pass dashed, the rest grid hairlines) and the baseline, all through one PpFigureScale — with any break in the scale marked, never hidden.

Source: `src/Gui/typeset/PpZonePlot.qml`.

## When to use
Figure-internal: under the marks of a panel in a figure row (PpRunChart, PpBoxStrip, PpHistoryStrip), so the panels of a row share one ground. Not for Charts-panel plots (their ground is drawn in their own blocks), and not as a background anywhere else (`PpTopoBackground` is the app's decorative ground).

## Consumer supplies
`roles`, `scale` (the row's PpFigureScale), and its size (normally `anchors.fill` over the plot).

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The panel's PpTypeset. |
| scale | var | null | The shared PpFigureScale. |

## Typeset tokens
`roles.wash` (corridor), `roles.fault` (fault edges), `roles.grid` + `roles.gridOpacity` (other edges), `roles.passEdge` + `roles.r.passDash` + `roles.dashPath()` (pass edges, dashed), `roles.hw` (edge and baseline width), `roles.baseline` + `roles.baselineOpacity` (baseline, `u(4)` under the plot), `roles.u(3)` (break slashes).

## States
- **Linear scale:** one wash, edges at full weight.
- **Broken (fault-forward):** the wash is drawn piece by piece (`scale.washPieces`) so the break stays clear; edges that fall in a compressed band are drawn there at 60 % opacity; PpBreakSlash marks each break at both ends of the panel.

## Usage
```qml
PpZonePlot { anchors.fill: plot; roles: ro; scale: sc }
```

## Used by
Session diagnostics: `diagnostics/PpRunChart.qml`, `PpBoxStrip.qml`, `PpHistoryStrip.qml`.
