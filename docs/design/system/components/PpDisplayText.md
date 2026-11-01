# PpDisplayText

A large display title filled with the brand's warm→cool gradient sweep, falling back to flat ink when gradient titles are off.

Source: `src/Gui/components/PpDisplayText.qml`.

## When to use
- Page and session titles, the home hero ("PinPoint Studio"). Not for body headings — those are `heading` (16 px) in the body face.

## Props
| name | type | default | meaning |
|---|---|---|---|
| text | string | — | The title. |
| pixelSize | int | fontSzDisplay | `Theme.fontSzDisplay` (26 / 34 / 22 / 24 by aesthetic); hero = 2 ×. |
| fontFamily | string | fontDisplay | `Theme.fontDisplay`. |
| fontWeight | int | fontDisplayWeight | `Theme.fontDisplayWeight`. |
| italic | bool | fontDisplayItalic | Editorial only. |
| letterSpacing | real | 0 | |
| flatColor | color | `colorText` | Ink when not gradient. |
| horizontalAlignment, wrapMode, lineHeight, elide, maximumLineCount | — | — | Text pass-throughs. |

## Tokens & values
Gradient: horizontal (≈ the site's 96°), `gradientWarm` 0 → `gradientWarmLit` 0.55 → `gradientCool` 1, masked to the glyphs. Shown while `Theme.gradientTitlesActive` (user setting on, reduce motion off, theme allows it).
