# PpChipGroup

A row of `PpChoiceChip`s built from a list, one picked (or several with `multiSelect`).

Source: `src/Gui/components/PpChipGroup.qml`.

**Use** when the options are data (a list of words or objects). **Don't** use when each chip does its own thing — put `PpChoiceChip`s in your own Repeater.

The consumer supplies `options`, the current `selected` (or `selectedValues`), and handles `selectionChanged(value)`.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `options` | var | `[]` | words, or `{label, value, count, tint, disabled}` (value defaults to label) |
| `selected` | string | `""` | the picked value (single-select) |
| `multiSelect` | bool | `false` | each chip a toggle |
| `selectedValues` | var | `[]` | the values on, in multi-select |
| `style` | string | `""` | any PpChoiceChip style |
| `disabledOptions` | var | `[]` | values shown dimmed + blocked |
| `spacing` | real | `gap(6)` = 6 px | between chips |
| `wrap` | bool | `false` | take the given width and wrap (opt-in) |
| signal `selectionChanged(string value)` | | | a chip tapped (in multi-select: the one to add or drop) |
| signal `blockedClicked(string value)` | | | a disabled chip tapped |

Tokens: as `PpChoiceChip`.

- One line, sized to its chips, unless `wrap` — wrapping is opt-in because a Flow sized by a RowLayout stays wrapped after a text-size change.
- A disabled option is still clickable so the screen can say why it cannot be taken.
