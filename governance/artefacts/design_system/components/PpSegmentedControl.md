# PpSegmentedControl

THE app's segmented selector: a row of joined choices, one active.

Source: `src/Gui/components/PpSegmentedControl.qml`.

## When to use
Choosing one of 2–5 mutually exclusive values in place. Segments brighten on hover and never scale (a scale breaks the seam). For a free set of filters use PpChoiceChip / PpChipGroup.

## Consumer supplies
`options` (words, or `{label, value, disabled}`), `selected`, and `onActivated(value)` that writes the value back.

## Props
| name | type | default | meaning |
|---|---|---|---|
| options | var | [] | Words or `{label, value, disabled}` objects. |
| selected | string | "" | The active value. |
| solid | bool | true | Default style: active solid accent (else raised bg3, accent ink). |
| style | string | "" | "" · compact · speed · unit |
| disabledOptions | var | [] | Values dimmed (0.35) and inert. |
| signal activated(value) | | | |

## Styles (exact)
- **""**: recessed capsule track filling its width, 30 px, `colorBg` fill, 1 px `colorBorderMid`, 3 px inset, 2 px gaps, equal segments, body2 (12 px). Active solid: `colorAccent` with `colorOnAccentBright` ink, weight 500; inactive ink `colorText2` at `Theme.fontBodyWeight`; hover `colorBg3`.
- **compact**: the same track at 108 × 24, glyph segments in the symbol face, active raised `colorBg3` with `colorAccent` ink.
- **speed**: 26 px outline capsule hugging 20 px segments, micro data words (10 px, tracking-data) in `colorText3`; active on `colorAccent` at 16 % with accent ink.
- **unit**: separate 36 × 34 cells on `colorBg2`, outer corners `Theme.radius`, each with a 1 px `colorBorderStrong` border; active `colorAccentLight` inside a `colorAccent` border; micro data words.
