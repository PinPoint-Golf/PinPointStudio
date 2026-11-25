# PpChoiceChip

THE app's choice chip: one or two words the user picks, alone or one of a row — every chip look the app draws is a `style` of this one component.

Source: `src/Gui/components/PpChoiceChip.qml`.

**Use** for picking one of a few options (PpChipGroup lays a row out from a list), filters, tags, toggles and small tabs. **Don't** use for stating facts (`PpChip`) or for actions (`PpButton`).

The consumer supplies `label`, `selected`, a `style`, and handles `clicked()`.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `label` | string | `""` | the words |
| `count` | string | `""` | dim words after the label, micro data face, 5 px gap (filter's "what this would leave") |
| `selected` | bool | `false` | picked |
| `pending` | bool | `false` | solid styles: picked, not live yet (accent wash + accent ink) |
| `style` | string | `""` | one of the looks below |
| `tint` | color | `colorAccent` | tag / toggle hue |
| `dimmed` | bool | `false` | quietened (opacity 0.4; solid 0.5; capsule/filter 0.45), still tappable |
| `blocked` | bool | `false` | taps kept (to say why), no grow, no pointing hand; `""` loses its hover and selected look |
| `horizontalPadding` | real | per style | total horizontal padding |
| `hoverScale` / `pressScale` / `moves` | alias | 1.02 hover | PpPressable motion |
| `hovered` | bool (ro) | | |
| signal `clicked()` | | | |

**Styles** (height · total padding · face; rest → hover; selected):

| style | geometry | look |
|---|---|---|
| `""` | 28 · 24 · body 13 | clear → `colorBg2`, `colorBorderStrong` → `colorAccentMid`; selected `colorAccentLight` fill, `colorAccent` border + ink |
| `mono` / `monoNeutral` | 24 · 20 · micro data 10 | transparent → accent wash at 40 % (monoNeutral: `colorBg2` at 60 %) |
| `body` | 30 · 28 · body2 12 | as mono, body face |
| `solid` / `solidLift` / `solidFlat` | 24 · 16 · body2 | `colorSurface`, `colorBorderMid` → accent wash 60 %; selected SOLID `colorAccent` with `colorBg` ink |
| `capsule` | 26 · 20 · body2 | capsule; transparent → `colorBg2`, strong border, no fades |
| `pill` / `pillFilled` | 24/22 · 18 · micro body | capsule, mid border, no hover look (pillFilled on `colorBg2`) |
| `filter` | 22 · 16 · label 11 | capsule; clear → `colorBg2`, mid border → accent; `count` after |
| `tag` | 18 · 10 · 9 px data, micro tracking | capsule in `tint`: hover tint 8 %, selected tint 15 % fill, 40 % border, tint ink; sits still |
| `toggle` | 18 · 16 · 9 px data | clear → `colorBg2`; selected tint 15 % fill, 40 % border |
| `tab` | 30 · 26 · body2 | no border; `colorBg2` when selected/hovered; 2 px `colorAccent` underline when selected |

Radius: `Theme.radius` (Theme.radius), capsule styles height/2. Colour and border fade over `durationFast` (120 ms), except capsule/pill. Hover grows 2 %.

- A restyle is this one file: add a look as a style, never a hand-built copy.
- Unselected `""` uses the theme's body weight; selected uses Normal (400).
