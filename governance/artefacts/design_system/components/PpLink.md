# PpLink

A text link in the accent: words that go somewhere.

Source: `src/Gui/components/PpLink.qml`.

## When to use
Navigation-like actions inside copy or a row of roots (the file picker's Home / Desktop…). Use PpButton for commands.

## Props
| name | type | default | meaning |
|---|---|---|---|
| text | string | | The words (it is a Text). |
| interactive | bool | true | false: greys to `colorText3` and takes no press. |
| current | bool | false | Where you already are: `colorText`, demi-bold (600), still pressable. |
| hoverTint | bool | true | Brightens (Qt.lighter 1.15) under the pointer. |
| pressMargin | int | 0 | Grows the press target past the words. |
| inkColor | color (ro) | | The colour now — for a glyph drawn beside it. |
| signal clicked(mouse) | | | |

## Tokens
`colorAccent`, `colorText`, `colorText3`; `Theme.fontBody` at 12 px (fontSzBody2), weight 400 (600 current).
