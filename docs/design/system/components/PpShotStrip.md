# PpShotStrip

The session strip, one drawing used everywhere: a cell per shot in the order struck, with three distinct marks — a warn bar (fired), an ink3 tick (clean), a stub (not measured) — and the picked shot as a pick column.

Source: `src/Gui/typeset/PpShotStrip.qml`.

## When to use
Wherever a typeset block shows a condition's shots in order: ledger rows, folded lines, Replay rows, each row of the barcode, and the page key's strip swatch. It works at any session length: given `availableWidth` the pitch narrows to fit, down to `roles.stripMinPitch`, then the strip wraps onto further lines — it never clips and never drops a shot.

Not for: a row of session pips in the app's chrome (`PpPips`), a points chart (`PpMark` in a plot), or a how-often meter (`PpMeter`).

## Consumer supplies
`roles`, `ticks`, and for a live strip `brush`; optionally `availableWidth`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | var | — | The panel's PpTypeset. |
| brush | var | null | The register's PpBrush. |
| medium | string | `roles.medium` | |
| ticks | var | [] | `[{ state, shotId, selected }]`; state "fired" · "clean" · anything else = not measured. shotId defaults to index + 1. |
| pitch | real | `roles.stripPitch` | The pitch it WANTS. |
| barH | real | `roles.stripH` | |
| barW | real | `roles.stripBar` | |
| availableWidth | real | 0 | > 0: fit (narrow, then wrap) inside this width. |
| rowGap | real | `roles.u(4)` | Between wrapped lines. |
| pickBand | bool | true | false when the parent draws a continuous column. |
| bandPad | real | 3 | The pick column's overhang above and below. |
| interactive | bool | `medium === "screen"` | |
| signal shotActivated(string swingDir, int shotId) | | | swingDir from `brush.dirOf()`. |
| signal shotHovered(int shotId) | | | -1 when the pointer leaves. |

Read-only: `count`, `fit` (`roles.stripFit(…)`), `cellPitch` (the pitch actually drawn — read it to align a column), `rows`, `perRow`. Functions: `cellX(i)`, `cellY(i)`.

objectNames: `sdShotStrip`, `sdShotCell` (each cell; carries `shotId`, `cellState`, `picked`, `hovered`), `sdShotStub`.

## Typeset tokens
`roles.warn` + `roles.r.barRadius` (fired bar), `roles.ink3` + `roles.hw` (clean tick, 60 % of the height), PpStub (not measured), `roles.pickWash` / `roles.hoverWash` (pick and hover columns), `roles.dim` + `roles.normal` (recede under a highlight), `roles.stripPitch`, `stripH`, `stripBar`, `stripMinPitch`, `u(4)`.

## States
- **Fired:** a warn bar the full height. **Clean:** an ink3 hairline tick, shorter than the bar. **Not measured:** a short flat ink3 stub on the baseline — present, never a gap.
- **Picked:** a faint ink column behind the cell (`pickWash`); **hovered:** a fainter one (`hoverWash`). Without a brush, `selected` marks the pick.
- Under a shot highlight the other shots recede to `roles.dim`.

## Usage
```qml
PpShotStrip {
    roles: ro; brush: br
    ticks: row.run                               // [{ state, shotId }]
    availableWidth: stripColumn.width
    onShotActivated: (dir, id) => root.openShot(dir, id)
}
```

## Used by
Session diagnostics: `diagnostics/PpLedgerTable.qml`, `PpBarcode.qml`, `PpSparkRow.qml`. Inside `typeset/PpPageKey.qml` (the strip swatch).
