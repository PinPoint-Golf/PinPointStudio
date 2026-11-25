# PpButton

THE app's button: every button look is a `style` and every size a `size`, so a restyle is this one file.

Source: `src/Gui/components/PpButton.qml`.

## When to use
Any pressable command. Pick the style by what the button means, not how loud you want it: `primary` for the one main action of a sheet, `attention` for the thing the user should do next, `destructive` for removal, the default outline for everything else. Don't hand-build a button lookalike — add a style here instead.

## Consumer supplies
`label` (and/or `glyph` / `icon`), an `onClicked` handler, and optionally `style`, `size`, `tone`, `active`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| label | string | "" | The words. |
| glyph | string | "" | Leading glyph in the symbol font (e.g. "▶"). |
| icon | Component | null | Leading drawn icon where the symbol font has none; receives `contentColor`. |
| iconSize | real | 15 | Icon square side. |
| primary / attention / destructive | bool | false | Shorthands for those styles when `style` is "". |
| style | string | "" | outline · primary · attention · destructive · accent · quiet · faint · wash · washPlain · subtle · ghost · tone · toneSoft · tonal · bar · glyph · scrim · menu · labPrimary · labStop · labOutline · labText |
| size | string | "normal" | normal 34·24·body · compact 28·20·body2 · micro 26·20·micro · mini 18·16·10px · icon 34×34 · iconSmall 20×18 · lab |
| tone | string | "" | Theme tone name (accent, good, watch, warn, error) for tone/toneSoft/tonal/wash/bar. |
| active | bool | false | Held lit: open disclosure, running calibration, menu up. |
| held | bool | false | Grown without hover (PpPressable.held). |
| mono | bool | false | Label in the data face. |
| hint | string | "" | Dim micro data words after the label (a shortcut). |
| chevron | bool | false | A ▾ after the label: opens a menu. |
| sizeLabel | string | "" | Measure width from this text so a changing label keeps its width. |
| tooltip | string | "" | Tooltip after 400 ms. |
| horizontalPadding | real | by size | Total horizontal padding (24 normal, 20 compact/micro, 16 mini/bar). |
| signal clicked() | | | |

## Tokens
Radius `Theme.radius` (iconSmall: radius − 1). Fills/borders/ink: `colorAccent`, `colorAttention`, `colorWarn`, `colorWarnLight`, `colorAccentLight`, `colorAccentMid`, `colorBg2`, `colorBg3`, `colorBorderMid`, `colorBorderStrong`, `colorText`, `colorText2`, `colorText3`, `colorOnAccent`, `colorOnAccentBright`, `colorOnScrim`. Label: `Theme.fontBody` at 13 px (compact 12, micro 10 data face); quiet/wash/bar labels at `Theme.fontBodyWeight`, others 400.

## States
- **outline** (default): transparent → `colorBg2` on hover, 1 px `colorBorderStrong`, ink `colorText`.
- **primary**: `colorAccent` fill (Qt.lighter 1.08 on hover), no border, ink `colorOnAccentBright`. **attention**: the same in `colorAttention`.
- **destructive**: `colorWarnLight` wash, `colorWarn` border and ink.
- **quiet**: clear → `colorBg2`; border `colorBorderStrong` → `colorAccentMid`; ink `colorText2`; active = `colorAccentLight` + `colorAccent` border/ink.
- **subtle**: `colorBg2` → `colorBg3`, `colorBorderMid`, ink `colorText2` (the neutral Cancel).
- Hover grows to 1.02, press dips to 0.97 (PpPressable, `Theme.durationFast`). Colour fades 120 ms.
- Disabled: opacity 0.4 (quiet/faint 0.5, bar 0.35; wash and lab looks stay opaque and grey their ink).

## Do / don't
- Rest fills are transparent but RGB-matched to the hover fill, so the fade only ramps alpha — no colour flash.
- A caller whose copy sat at another height states `implicitHeight` / `horizontalPadding`; don't add a size for it.
