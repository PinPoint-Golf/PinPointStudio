# PpBadge

A leading round mark at the head of a coaching line or table row — one shape per meaning, so colour is never the only channel.

Source: `src/Gui/components/PpBadge.qml`.

**Use** at the start of a coaching line, a table row (plain marks) or a setup check row (`status`), and as the mark inside `PpQualityPill`. **Don't** use a kind for a meaning it does not carry.

The consumer supplies `kind` (and optionally `tone`, `size`, `status`).

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `kind` | string | `"check"` | `check`, `target`, `easing`, `unconfirmed`, `dot`, `ring`, `status` |
| `tone` | color | check/easing `colorGood`; target/dot `colorAccent`; ring `colorAttention`; else `colorText3` | glyph colour (and its wash) |
| `size` | int | `sp(20)` = 20 px | diameter |
| `plain` | bool | `false` | drop the fill circle: the glyph alone |
| `status` | string | `""` | kind `status`: the tone name of the ring (`good` adds a ✓) |
| `animated` | bool | `false` | the dot fades on tone change (120 ms) |

| kind | glyph | meaning |
|---|---|---|
| `check` | tick, 1.6 px round stroke | done well, an ideal to aim for |
| `target` | ring (56 % of size, 1.5 px) + dot (20 %) | something to work on |
| `easing` | down arrow | a fault that is fading |
| `unconfirmed` | dashed ring [2,2] + dot, no fill | placed on evidence no recent session renewed |
| `dot` | filled dot, ≤ 10 px | a strength (table row) |
| `ring` | 2 px ring, ≤ 10 px, empty | a fault (table row) |
| `status` | 1 px ring in the tone on `toneLight` wash; ✓ when good; empty grey ring when none | setup check row |

Fill circle: tone at 12 % (light) / 16 % (dark), except `unconfirmed`, `status` and `plain`. A plain dot at 6 px is a device's status dot.
