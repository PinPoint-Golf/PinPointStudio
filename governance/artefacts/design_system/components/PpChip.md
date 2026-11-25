# PpChip

A small pill of mono words in a tone ("NEXT", "last seen 4 Jul"), or — given a `trend` — the trend tag "↑ growing" / "↓ easing".

Source: `src/Gui/components/PpChip.qml`.

**Use** to state a small fact beside a title or row without weight. **Don't** use it as a control — a chip the user picks is `PpChoiceChip`. With empty `text` it takes no room.

The consumer supplies the words (or a `trend`), and optionally a tone.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `trend` | int | `0` | >0 "↑ growing" in `colorWarn`; <0 "↓ easing" in `colorAccent` |
| `text` | string | from `trend`, else `""` | the words; empty hides the chip |
| `tone` | color | `colorWarn` if trend>0, else `colorAccent` | words, wash and outline |
| `tinted` | bool | `true` | false: transparent with a `colorBorderStrong` outline |
| `tracking` | real | trend: 0; else `trackingData` (0.3) | letter-spacing |
| `isTrend` | bool (read-only) | | a trend was given |

**Geometry.** Plain: 18 px high, words + 12 px wide, capsule; micro data face (10 px, `Theme.fontData`), normal weight, 0.3 px tracking; fill = tone at 9 % (light) / 12 % (dark); 1 px border = tone at 45 %. Trend: 20 px high, words + 14 px; body face 11 px **semibold (600)**; fill = tone at 13 % / 18 %; no border.

Tokens: `colorWarn`, `colorAccent`, `colorBorderStrong`, `Theme.fontData`, `Theme.fontBody`.

- Only the trend case wears the semibold body face; every chip that sets its own words keeps the mono pill.
- The trend is told by its arrow and word, never by colour alone.
