# PpTableRow

A fixed-height table row (§2.5 visual language) laid out on its table card's column template.

Source: `src/Gui/components/PpTableRow.qml`.

## When to use
- Rows inside `PpCard { table: true }`; or alone with `topRule` and an explicit `layout`.

## Consumer supplies
Cells as direct children, in column order (cell i takes column i + `columnOffset`); bind a cell's width to `columnWidth(i)` to fill/elide, and right-align text in a right column.

## Props
| name | type | default | meaning |
|---|---|---|---|
| secondLine | bool | false | Height `rowH2` (56) instead of `rowH` (48). |
| padH | int | sp(rowPadH) | 18 px each side. |
| columnOffset | int | 0 | First column cell 0 takes. |
| centreCells | bool | true | Centre cells vertically. |
| topRule | bool | false | A `colorHairline` at the top, for a row outside a table card. |
| hoverEnabled | bool | true | Show the hover wash. |
| highlighted | bool | false | Hold the wash on (PpExpandRow drives it). |
| hoverBleed | int | 0 | How far the wash reaches past the sides. |
| hoverColor | color | `colorBg3` in a table card, else `colorBg2` | The wash. |
| card | var | nearest table card | Template source. |
| layout | var | card.columnLayout | Resolved `[{x,width,align}]`. |
| roundTop / roundBottom | bool | auto | Round the wash to the card corner (radius − 1). |
Read-only `hovered`, `lit`, `cellBox`; functions `columnX(i)`, `columnWidth(i)`.

## Do / don't
- Don't lay cells out with a Repeater directly in the row — wrap repeated cells in one Item.
