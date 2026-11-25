# PpExpandRow

A PpTableRow with a drawn chevron that turns 90° when the row opens, revealing a detail block in place under it.

Source: `src/Gui/components/PpExpandRow.qml`.

## When to use
- Rows that open in place (§13.7): areas, faults, work-ons. A row with no `detail` never opens (no chevron, no press).

## Consumer supplies
Header cells (default property, as PpTableRow), a `detail` Component, an `accessibleName`; bind `expanded` and set `autoToggle: false` for one-open-at-a-time.

## Props
| name | type | default | meaning |
|---|---|---|---|
| expanded | bool | false | Open or closed. |
| autoToggle | bool | true | Flip `expanded` itself before `toggled()`. |
| detail | Component | null | Loaded under the header, shown while expanded. |
| detailIndent / detailRightInset | real | 0 | Narrow the detail. |
| chevronSide | string | "left" | "left" (in the row padding) or "right" (trailing column). |
| showChevron | bool | true | Hide for a row that cannot open. |
| chevronCentreY | real | -1 | Centre on a header line of its own. |
| accessibleName, pressObjectName | string | "" | A11y name; probe name. |
| rowHeight | real | rowH / rowH2 | Header height. |
Aliases to the header: `secondLine`, `padH`, `columnOffset`, `centreCells`, `topRule`, `hoverBleed`, `card`, `layout`. Signal `toggled()`.

## Tokens & values
Chevron 10 px, two round-capped strokes ≥ 1.2 px: `colorText3` closed, `colorText2` open, rotated 90°. Focus ring: 2 px `colorAccent`, `Theme.radius`, inset 1 px. Keyboard: Tab focuses; Space / Return / Enter toggle. Accessible.Button, expanded exposed as checked.
