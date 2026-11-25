# PpInset

Something set into a card: a faintly darker panel with a hairline for the part a reader acts on (a drill, what to try); with `bar`, a short tone bar down its left edge marks it as the card's own.

Source: `src/Gui/components/PpInset.qml`.

## Consumer supplies
Content as children (a column), `tone` when `bar` is on.

## Props
| name | type | default | meaning |
|---|---|---|---|
| tone | color | `colorAccent` | The bar's colour. |
| bar | bool | false | 3 px tone bar on the left, inset 14 px top and bottom, radius 1.5. |
| padX | int | gap(14) | Horizontal padding. |
| padY | int | gap(12) | Vertical padding. |
| spacing | int | gap(4) | Between children. |
Read-only `contentHeight`.

## Tokens & values
Fill `colorText` at 4 % (3.5 % dark), 1 px `colorBorder`, corner `Theme.radius`. The bar is a mark, not chrome — it stays in every theme.
