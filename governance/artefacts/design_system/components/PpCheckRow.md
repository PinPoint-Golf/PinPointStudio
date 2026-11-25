# PpCheckRow

A square checkbox and its words, the whole row pressable.

Source: `src/Gui/components/PpCheckRow.qml`.

## When to use
Opt-in rows: the export sheet's camera and JSON rows, "Show archived (N)". Press-dip only, no hover-grow (a growing full-width row would nudge its neighbours). Anything declared inside follows the label on the same line, 10 px on.

## Props
| name | type | default | meaning |
|---|---|---|---|
| label | string | "" | The words (13 px body, `colorText`). |
| checked | bool | false | The value. |
| trailing | default children | | Items after the label. |
| signal toggled() | | | |

## Tokens / states
Row 26 px tall, 10 px gaps. Box 16 × 16, radius 4, 1 px border: off transparent + `colorBorderStrong`; on `colorAccent` fill and border with a ✓ (11 px) in `colorOnAccent`.
