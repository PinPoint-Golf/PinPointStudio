# PpRowAction

A row's hover action: a small chip of micro caps words that appears on the hovered row and does its one obvious thing.

Source: `src/Gui/components/PpRowAction.qml`.

## When to use
"Graph" on the swing model table, "Review" on a Sessions row. The caller places it and binds `visible` to the row's hover/focus; it takes no press while hidden. Put the keyboard shortcut in `tooltip`, so the mouse path teaches the key.

## Props
| name | type | default | meaning |
|---|---|---|---|
| text | string | "" | The words (PpMicro kind "bodyWeight": body face, theme caps). |
| tooltip | string | "" | Shown after 400 ms. |
| hovered | bool (ro) | | Pointer on it. |
| signal clicked() | | | |

## Tokens / states
Height 20, width = text + 14, `Theme.radius`, 1 px border. Rest: `colorBg3` fill, `colorBorderMid` border, `colorText2` ink. Hover: `colorAccentLight`, `colorAccent` border and ink (fill fades over `Theme.durationFast`). No hover-grow; press dips.
