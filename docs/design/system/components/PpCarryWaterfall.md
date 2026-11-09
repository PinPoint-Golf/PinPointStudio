# PpCarryWaterfall

Where the yards went on one shot: the golfer's usual carry, each part taken off it in causal order, the part nobody can account for when it is worth saying, then what the ball carried.

Source: `src/Gui/diagnostics/PpCarryWaterfall.qml`.

## When to use
This shot in Session diagnostics Analyse, on a shot labelled short that has a usual to be short of (`shotOutcome(shotId).carry`). Bars are drawn in the register's inks and never in warn: colour in this panel means "outside its corridor", and a yard lost is not that.

Not for: a distribution across shots (`PpBoxPlot`), or the one-line summary in the Replay glance and on paper (`carry.line`, set as text).

## Consumer supplies
`roles`, `carry`, and a width.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpDiagRoles {}` | The register's one PpDiagRoles. |
| brush | var | null | The register's PpBrush. |
| medium | string | `roles.medium` | |
| carry | var | `({})` | `{ usual, actual, provenance, residual, residualShown, scaled, parts: [{ id, name, loss, detail }] }`. Hidden while `usual` or `actual` is missing. |
| atom | bool | true | One pager unit. |

Read-only: `has`, `rows` (`[{ name, detail, from, to, value, kind }]`, kind "usual" · "part" · "residual" · "actual"), `labelW`, `valueW`, `gapW`, `trackW`, `pxPerYard`, `keyKinds` (empty).

objectNames: `sdCarryWaterfall`, `sdCarryWaterfallScaled` (the "scaled to fit" line).

## Typeset tokens
`PpSectionHead` for "Where the yards went" and the provenance sub-line. `roles.fSmall` (a part's name), `roles.fSmallM` (usual, actual, residual), `roles.fMicro` + `roles.ink3` (a part's detail), `roles.fFig` (the yards), `roles.stripH` + `roles.r.barRadius` (bar), `roles.ink3` (usual, and the residual's hairline) · `roles.ink2` (actual) · `roles.ink` (a part). Space: `roles.g(5)`, `g(10)`.

## States
- **No carry block:** not drawn.
- **Parts:** each bar runs from where the last one ended, on one yardage axis. A part with `modelled` set, or id `launch_spin`, says "(estimate)".
- **Residual:** an outlined bar, "Not accounted for", when `residualShown` is true.
- **Scaled:** "The parts were scaled to fit the shortfall." when a part was cut.

## Usage
```qml
PpCarryWaterfall {
    width: column.width
    roles: ro; brush: br
    carry: root.outcome.carry || ({})
}
```

## Used by
Session diagnostics: `diagnostics/PpDiagAnalyse.qml`.
