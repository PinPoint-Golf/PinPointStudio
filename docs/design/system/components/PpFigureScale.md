# PpFigureScale

The value axis the panels of one figure row share: it maps the model's normalised position f (0..1 along `axisLo..axisHi`) to a pixel y, names the zone edges once, and — fault-forward — spends its height where the shots are.

Source: `src/Gui/typeset/PpFigureScale.qml`.

## When to use
Inside a PpFigureRow, so PpRunChart, PpBoxStrip and PpHistoryStrip share ONE y scale ("one figure grammar"), and in PpSparkline. Panels only ever call `y(f)`, `region(f)` and read `corridor` / `washPieces` / `edges` / `bandLo` / `bandHi`.

It draws nothing (a `QtObject`); PpZonePlot draws the ground from it and PpBreakSlash its breaks. Not for: a Charts-panel axis or any plot whose values are not the model's normalised band positions.

## Consumer supplies
`bands`, `plotTop`, `plotHeight`, and for fault-forward the shots' `values` plus `axisLo` / `axisHi` to label a band in true units.

## Props
| name | type | default | meaning |
|---|---|---|---|
| bands | var | [] | The model's bands (`f0`, `f1`, `lo`, `grade`, `edge`). |
| faultLines | var | [] | |
| yTicks | var | [] | |
| plotTop | real | 0 | |
| plotHeight | real | 100 | |
| values | var | [] | f of every shot the panels plot. |
| faultForward | bool | true | Keep a linear window over the shots; false = linear. |
| bandPx | real | 16 | Height of a compressed band (callers pass `roles.u(16)`). |
| breakPx | real | 7 | The drawn gap between a band and the window. |
| minOmit | real | 0.12 | Collapse an end only when it would waste at least this share. |
| clean | bool | false | No shot fired: keep every pass edge in the window (the margin is the story). |
| axisLo / axisHi | real | NaN | The values at f = 0 and f = 1. |

Read-only: `edges` (`{ f, value, kind }`, kind fault · pass · aim · grid), `corridor` (`{ lo, hi }` over ideal + good + open, or null), `washPieces`, `passFs`, `faultFs`, `win` (`{ lo, hi, bandLo, bandHi }`), `bandLo`, `bandHi`, `broken`, `winTop`, `winBottom`, `loBandTop`, `hiBandBottom`, `breakYs`.

Functions: `y(f)`, `region(f)` (0 low band · 1 window · 2 high band), `valueOf(f)`, `tickVisible(f)`, `edgesIn(r)`.

## Tokens
None — it is geometry. Sizes come from the caller (`roles.u(…)`).

## States
- **Linear:** no `values`, or `faultForward: false`.
- **Fault-forward:** a linear window over the shots ∪ the pass edge they must get back to, padded (8 %, at least 0.03); an end outside it collapses into a compressed band behind a break. A clean set keeps the whole corridor. Anything in a band is still drawn there — compressed, never hidden; ticks show only where the scale is true.

## Usage
```qml
PpFigureScale {
    id: sc
    bands: s.bands || []; yTicks: s.yTicks || []
    plotTop: topY; plotHeight: fH
    values: shotFs; axisLo: s.axisLo; axisHi: s.axisHi
    bandPx: roles.u(16); breakPx: roles.u(8); clean: !isFault
}
```

## Used by
`diagnostics/PpFigureRow.qml` (shared by its PpRunChart, PpBoxStrip, PpHistoryStrip) and `diagnostics/PpSparkline.qml` — Session diagnostics.
