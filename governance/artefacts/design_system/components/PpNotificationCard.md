# PpNotificationCard

The notice card both notification surfaces draw: an elevated fill with a hairline in the notice's tone, a glyph, the words, and actions after hairlines.

Source: `src/Gui/components/PpNotificationCard.qml`.

It only draws and reports — who shows it, for how long and what an action does stay with the host. Use it through PpNotificationHost (the window's notification centre) or PpToast (a panel's own toast); draw it directly only when building a new host.

The consumer supplies the glyph, title (and optional detail), the tone colour, and which actions to show; it wires the signals.

## Props

| name | type | default | meaning |
|---|---|---|---|
| `style` | string | `""` | `""` the centre's (glyph set apart, optional detail line, ≥ 40 px, grows) · `toast` (glyph tight against one line, always 40 px) |
| `glyph` | string | `""` | symbol glyph, `fontSymbol`, 13 px |
| `glyphColor` | color | `colorText2` | the glyph's ink |
| `tone` | color | `colorBorderStrong` | the card's 1 px hairline |
| `title` | string | `""` | body (13 px) in `colorText` |
| `detail` | string | `""` | body2 (12 px) in `colorText2`, middle-elided at 420 px |
| `copyText` | string | `""` | non-empty: a ⧉ in `colorAccent` copies it; flashes ✓ in `colorGood` for 1200 ms |
| `actionText` | string | `""` | an action in the data face, 12 px, `trackingLabel`, `colorAccent` |
| `dismissible` | bool | `false` | a ✕ in `colorText2` at the end |
| signals | | | `copied()`, `actionClicked()`, `dismissClicked()` |

## Geometry

Fill `colorBg3`, radius `Theme.radiusLg`, 1 px border in `tone`. Width = content + 30 px. Row gap 14 px (toast: glyph↔words 9 px). Each action follows a 1 × 18 px `colorBorderStrong` hairline. Title↔detail gap 2 px; centre style pads 8 px top/bottom.

## Tokens

`colorBg3`, `colorBorderStrong`, `colorText`, `colorText2`, `colorAccent`, `colorGood`, plus the tone (`colorWarn`, `colorError`); `Theme.radiusLg`, `Theme.fontBody`, `Theme.fontData`, `Theme.trackingLabel`.
