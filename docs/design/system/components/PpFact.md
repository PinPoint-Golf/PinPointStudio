# PpFact

A label and its value in an opened row: a micro label in a fixed column, then the value's words (marks such as pips may sit before them).

Source: `src/Gui/components/PpFact.qml`.

## Consumer supplies
`label`, `text`, optionally marks as children (placed in a row before the words). Empty, it takes no room.

## Props
| name | type | default | meaning |
|---|---|---|---|
| label | string | "" | PpMicro label (spell capitals via Theme.caps). |
| text | string | "" | The value's words. |
| labelWidth | int | sp(84) | The label column. |
| extra (default) | list | — | Marks before the words, gap(8) apart; words follow at gap(12). |

## Tokens & values
Label: PpMicro (data face 10 px, `trackingMicro`, `colorText3`), on the words' first baseline (or the marks' centre). Words: body face 12 px, `Theme.fontBodyWeight`, `colorText2`, line-height 1.35, wrapping.
