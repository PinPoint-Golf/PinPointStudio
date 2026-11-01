# PpSelectTile

A selectable tile: a card or row the user picks one of (a layout card, a preset, a club, an input device, an athlete).

Source: `src/Gui/components/PpSelectTile.qml`.

**Use** when the choice needs more than a word — a title and a line, or custom content. **Don't** use for one-word choices (`PpChoiceChip`). It sizes to nothing of its own: set its height (and width).

The consumer supplies the size, `selected`, `title` + `text` or its own content, and handles `clicked()`.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `selected` | bool | `false` | picked |
| `style` | string | `""` | `""`, `card`, `tint` |
| `title` | string | `""` | body2 12 px, `colorText` (`colorAccent` when selected) |
| `text` | string | `""` | micro data 10 px, `colorText3`, 2 px under the title |
| `hoverScale` / `moves` / `held` | alias | 1.02 (card: 1.0) | PpPressable |
| `hovered` | bool (ro) | | |
| signal `clicked()` | | | |

**Styles** (rest → hover; selected = `colorAccentLight` fill inside `colorAccent` border; hover border → `colorAccentMid`):

- `""` — clear, `colorBorderMid` → faint `colorBg2` fading in. Radius `Theme.radius`.
- `card` — `colorSurface`, `colorBorderStrong` → `colorBg2` at 50 %; no hover grow.
- `tint` — `colorSurface` → the accent wash laid over the surface (opaque), mid border, radius `Theme.radiusLg`; selected looks hovered, with the accent border (athlete cards).

Text sits 12 px in from each side, vertically centred, elided. Colour and border fade over 120 ms.
