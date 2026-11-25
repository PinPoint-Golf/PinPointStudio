# PpStatusPill

A status pill: a state said in a word or two ("Connected", "Complete", "✓  Enabled"), in its tone on the tone's wash with a hairline in the tone.

Source: `src/Gui/components/PpStatusPill.qml`.

**Use** for the state of a device, a step or a setting. **Don't** use for a judged reading (`PpBadge`, `PpQualityPill`) or a choice (`PpChoiceChip`).

The consumer supplies `text` and a Theme tone name (or its own `fg`/`bg`/`edge`).

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | `""` | the words |
| `tone` | string | `""` | `good`/`ok`, `watch`/`attention`, `warn`, `fault`/`error`, `accent`; anything else = quiet pill |
| `style` | string | `"box"` | look (below) |
| `animated` | bool | `false` | fade colours on change (120 ms) |
| `fg` / `bg` / `edge` | color | from tone | words / fill / hairline override (the updater's palette) |

Colours: fg = `toneColor(tone)` (quiet: `colorText3`), bg = `toneLight(tone)` (quiet: `colorBg3`), edge = fg (quiet: `colorBorderMid`; capsuleSoft: fg at 30 %). `tag` inverts: solid tone fill, `colorSurface` words.

| style | geometry |
|---|---|
| `box` | 26 px high, words + 16, `Theme.radius`, micro data 10 px |
| `round` | 22 px, radius 11, micro data |
| `capsule` | fits words + 6 px high, words + 14 wide, 9 px data, micro tracking 0.8 |
| `capsuleSoft` | capsule with its hairline at 30 % |
| `tag` | 14 px, radius 1, no border, solid tone, 7 px words |
| `chip` | 20 px, radius 4, micro data, data tracking 0.3 |

- The state is in the word: colour repeats it, never replaces it.
