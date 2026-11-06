# PpChMatrix

A table of short cells under one heavy rule, set like an annual report: named rows, a figure or a word in each cell, and at most a one-hue wash behind it. One component with two forms: the family (a lower triangle of r, Charts Relations) and the rectangle (named columns across the full width, Consistency).

Source: `src/Gui/typeset/PpChMatrix.qml`.

## When to use
A small grid of readings that is read cell by cell, on screen or on paper, where each cell is a word, a figure or a mark. Any PpTypeset feeds it: a register's PpChRoles, a PpDiagRoles, or a print PpTypeset with `fixedAesthetic`.

Not for: a table of long rows with a column per field (`PpTableRow` in the app's chrome, or a block's own ledger), or a heat map over a plot: the heat is for table cells only, and a chart's only other wash is the corridor.

## Consumer supplies
`roles` and a `width`, then one of:
- **The family:** `matrix` (the presenter's `{ readings: [{ text }], cells: [[{ xId, yId, defined, rText, clear, on }]] }`) and `onPairActivated(xId, yId)`.
- **The rectangle:** `columns`, `rows`, `onCellActivated(rowId, column)`; `keyText` when the cells have short forms.

Setting `columns` chooses the rectangle. Every word comes from the presenter; the component writes none.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | null | The register's PpTypeset. Nothing draws until it is set. |
| matrix | var | null | The family's data. Ignored once `columns` is set. |
| columns | var | [] | The rectangle's heads: `[string]` or `[{ text, short }]`. `short` is the head in the short form; missing, `text` stays. |
| rows | var | [] | `[{ id, name, cells }]`; each row has one cell per column. A cell with no `text` is blank. |
| wash | bool | true | Draw the cells' heat. False sets the same table with no wash. |
| keyText | string | "" | What the short forms mean, in the presenter's words. Shown under the table, in the short form only. |
| interactive | bool | true | False: drawn, never pressed (print). |
| atom | bool | true | A pager unit. |
| signal pairActivated(string xId, string yId) | | | The family: a defined cell was pressed. |
| signal cellActivated(string rowId, int column) | | | The rectangle: a cell with `tap` was pressed. `column` is 0-based. |

**A cell of `rows`:**

| key | type | meaning |
|---|---|---|
| text | string | The word or figure. |
| short | string | The same thing in the short form, a mark such as `·` `◐` `●` `⚠`. |
| strong | bool | Ink; otherwise ink3. |
| heavy | bool | The word in `fSmallM`, full form only. |
| heat | int | 0 none, 1 some, 2 loose. |
| soft | bool | Lighter heat, whatever the level (the span after the ball). Needs `heat` > 0. |
| on | bool | Picked: an ink outline. |
| mark | string | One glyph after the word in ink, such as `◆` "this shot left your pattern here". It stays inside the cell: in a tight cell it closes up to the right edge and the word moves left only if it must. Say what it means in `keyText` or a note. |
| tap | bool | Pressable (while `interactive`). |

Read-only: `family` (the family form), `short` (the short form is on: a screen can hang its own words on it), `n` (the family's readings that fit), `nCols`, `em`, `nameMax`, `wordW`, `indent`, `nameW`, `cellW`, `rowH`, `tableW`, `all`, `heads`, `body`, `keyKinds`.

objectNames: `chMatrix`, `chMatrixCell` (each cell; `xId`, `yId` in the family).

## Typeset tokens
`roles.r.figureRuleTop` + `roles.rule` (the head rule), `roles.hw` + `roles.hair` (row hairlines), `roles.fFigLabel` + `roles.ink3` (heads and row numbers), `roles.fSmall` + `roles.ink` (row names), `roles.fFigS` (figures, short forms, the mark), `roles.fSmall` / `fSmallM` (words), `roles.ink` / `ink3` (strong or not), `roles.ink` (the mark: its shape carries the meaning), `roles.heat(level, soft)` (the wash), `roles.pickWash` (the family's pick), `roles.ink` at `r.figureRuleTop` (the rectangle's outline), `roles.em`, `roles.u(22 / 26 / 6)`, `roles.g(3 / 4 / 5 / 6)`, `roles.r.ch.matrixCellEm`. Theme, through `heat()`: `colorHeatSome`, `colorHeatLoose`, `colorHeatAfter`.

## States
- **The family** fits as many readings as its columns allow and leaves the rest to the "Moves with" ledger; under three readings it hides. It is as wide as its columns, not its `width`.
- **The rectangle** fills `width`. Row names run to 8.4 em and elide.
- **Short form:** when the words no longer fit (a cell needs 4.3 em) every cell takes its `short` on the same wash, the heads take theirs, row names give way to 6.2 em and `keyText` appears. No column is ever dropped: the cells narrow.
- **Picked:** the family keeps the pick wash; a cell of the rectangle takes an ink outline, because the heat has taken over the old highlight.
- **Colour is never the only channel:** the word, or in the short form the mark and its key, stays in every cell with heat. The heat says how much, never good or bad, so it is `colorAttention` and never warn.
- **Print:** the heat is `roles.attention` scaled by `tintK`; with `fixedAesthetic` it reads the fixed aesthetic's table, so a printed page does not follow the app's theme.

## Usage
```qml
PpChMatrix {
    width: parent.width; roles: ro
    columns: table.columns; rows: table.rows; keyText: table.keyMarks
    onCellActivated: (rowId, column) => root.open(rowId, column)
}
```

## Used by
`charts/PpChartsRelationsView.qml` (the family), `diagnostics/PpConsistencyView.qml` and `report/PpConsistencyPage.qml` (the rectangle).
