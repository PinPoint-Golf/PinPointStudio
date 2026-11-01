# PpInlinePicker

A choice that opens IN PLACE, never a pop-up: a quiet question, one row per choice with the current one marked, then the clearing row. It pushes what is below it down.

Source: `src/Gui/typeset/PpInlinePicker.qml`.

## When to use
A short pick that belongs to a typeset register and opens under the control that asked for it — the declared-miss picker under the tab row, the Charts panel's choices (a preset, the metrics to trace, the window, what to split the session by or set against). It closes when the register sets `open` false (a pick, the chip again, or Esc). Nothing here is coloured: it is a control, not a finding.

Not for: a dropdown in the app's chrome (use `PpComboBox`), a row menu (`PpMenuItem` in a popup), or a picker among tiles (`PpSelectTile` / `PpChipGroup`).

## Consumer supplies
`roles`, `items`, `current`, `open`, and `onPicked(id)` that writes the choice back and closes it.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The register's PpTypeset. |
| brush | var | null | |
| medium | string | `roles.medium` | |
| interactive | bool | `medium === "screen"` | Rows press only while true. |
| open | bool | true | Shown while true. |
| title | string | "What is the bad shot?" | The quiet question. |
| items | var | [] | `[{ id, label, note }]`. |
| current | string | "" | The chosen id ("" = none). |
| clearLabel | string | "No declared miss" | The clearing row's words. |
| itemObjectName | string | "sdMissCandidate" | objectName of each row (carries `missId`). |
| clearObjectName | string | "sdMissClear" | objectName of the clearing row. |
| signal picked(string id) | | | "" for the clearing row. |

objectNames: `sdMissPicker` (the column; callers may override), `sdMissPickerLabel` (the question), the rows and clearing row as above.

## Typeset tokens
`roles.fLabel` + `roles.ink3` (question), `roles.fBody` / `fBodyM` (a row; the current one medium), `roles.ink` (label), `roles.ink3` (note, unpicked ○), `roles.fSmall` (●/○), `roles.rowHover` (hover), `roles.hw` / `roles.hair` (rule under each row), `roles.fSmallL` + `roles.link` (clearing row while something is chosen; `fSmall` + `ink` when nothing is), `roles.u(2 / 20 / 24 / 26)`, `roles.g(4 / 6 / 8)`.

## States
- Each row: ● in ink when current, ○ in ink3 otherwise; hover wash `rowHover`; a hairline under it.
- Clearing row: a link while something is chosen; plain ink when nothing is.
- Not interactive (print, an auto-closing cast): drawn, never pressed.

## Usage
```qml
PpInlinePicker {
    open: root.missOpen; width: under.width
    roles: ro; brush: br; interactive: root.interactive
    items: ss.missCandidates().map(m => ({ id: m.id, label: m.name || m.id }))
    current: ss.declaredMiss
    onPicked: id => { root.declareMiss(id); root.missOpen = false }
}
```

## Used by
`diagnostics/PpDiagAnalyse.qml` and `diagnostics/PpDiagReplay.qml` (the declared-miss picker), `charts/PpChartsRegister.qml`, `charts/PpChartsSessionView.qml` and `charts/PpChartsShotView.qml` (Charts: presets, metrics, window, split, against).
