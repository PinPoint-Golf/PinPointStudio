# PpNotice

A notice: words in a tone, on the tone's wash inside a hairline in the tone.

Source: `src/Gui/components/PpNotice.qml`.

Use it for a standing message attached to a place on screen — "the raw frames are big", "everything's ready", "release on open canvas". Not for transient news (use PpToast / PpNotificationHost) and not for a question (PpConfirm). Anything declared inside it sits on top, so wrapping a PpPressable makes the whole notice a way in.

The consumer supplies the `tone`, the words, the `style`, and — for `strip` and `para` — the width (those two claim none of their own).

## Props

| name | type | default | meaning |
|---|---|---|---|
| `tone` | string | `"warn"` | Theme tone name: good, watch/attention, warn, fault/error, accent, dim |
| `text` | string | `""` | the words |
| `style` | string | `"strip"` | `warning` · `strip` · `para` · `bubble` · `bubbleSlim` · `chip` |
| `title` | string | `Theme.caps("Warning")` | `warning` only: the eyebrow over the words |
| `maxWidth` | real | ∞ | `bubble` only: never wider than this |

## Styles (exact values)

All: fill `toneLight(tone)` (e.g. `colorWarnLight`), radius `Theme.radius`, 1 px border in the tone.

| style | look | where |
|---|---|---|
| `warning` | ⚠ (14 px) then a PpMicro caps title in the tone over body2 words in `colorText2`; border tone at 40 %; padding 11 px, gap 10 px, 3 px between title and words | Home (resources) |
| `strip` | mono micro (10 px, `Theme.fontData`) words in the tone, full width; padding 12 px sides, 10 px top/bottom | Settings › Storage |
| `para` | body2 words (12 px, body weight) in the tone, 1.5 line height, 12 px all round | setup Ready |
| `bubble` | body2 words in the tone, centred, sized to them up to `maxWidth`; 12 px sides, 8 px top/bottom | model graph (drag) |
| `bubbleSlim` | bubble, 2 px shorter (7 px top/bottom) | model graph (link hint) |
| `chip` | 26 px tall, micro body-face words in the tone, **no hairline**, 9 px sides | model editor (validation) |

## Tokens

`colorGood/Warn/Error/Attention/Accent` + their `…Light` washes, `colorText2`, `colorText3`; `Theme.radius`, `Theme.fontBody`, `Theme.fontData`, `Theme.fontBodyWeight`.

## Do / don't

- Do pick the tone by the kind of news; Theme picks the hue.
- Don't use it for a destructive question — that is PpConfirm with the error tone.
- The styles are kept as they are in use until the restyle (#131) merges them; don't invent a seventh.
