# PpMoreButton

THE ⋯ tile that opens a row's menu: a square `radius` tile with a ⋯ in the data face, washed while hovered or open.

Source: `src/Gui/components/PpMoreButton.qml`.

## When to use
The trailing menu of a table or drawer row (Sessions rows). Set `open` from the menu and toggle it in `onClicked`; a Popup may take `face` as its parent so it moves with the tile.

## Props
| name | type | default | meaning |
|---|---|---|---|
| open | bool | false | Its menu is up: stay washed and grown. |
| revealed | bool | true | false hides it at rest (show on row hover); shown while hovered or open. |
| tile | real | 24 | The square tile's side. |
| wash | color | colorBg2 | Tile fill while hovered or open. |
| ink / litInk | color | colorText3 / colorText | ⋯ at rest / lit. |
| face | Item (ro) | | The tile, for a Popup parent. |
| signal clicked() | | | |

## Tokens
`Theme.radius`, `colorBg2`, `colorText3`, `colorText`; ⋯ in `Theme.fontData` 13 px. Grows via PpPressable (held while open); fades in over `Theme.durationFast`.
