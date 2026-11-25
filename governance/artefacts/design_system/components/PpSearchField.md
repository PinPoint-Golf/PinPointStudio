# PpSearchField

The app's search field: a PpTextField with a search's chrome — a box, or a ⌕ glyph over a rule.

Source: `src/Gui/components/PpSearchField.qml`.

## When to use
Settings (line), the athlete picker (compact), Sessions, the swing model's search and type-ahead pickers. What typing does (filtering, Esc, arrows) stays with the caller.

## Props
| name | type | default | meaning |
|---|---|---|---|
| style | string | "box" | "box": PpTextField's box at its `size` · "line": no box, ⌕ then the text, a 1 px rule 6 px above the bottom. |
| (PpTextField) | | | `size`, `placeholderText`, `text`… |

## Tokens / states
Line: ⌕ in the body face at 16 px, `colorText3` (`colorText2` focused); text 12 px; rule `colorBorderMid` (`colorAccent` focused), both easing over `Theme.durationFast`; glyph-to-text gap 6. Box: as PpTextField.
