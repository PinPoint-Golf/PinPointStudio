# PpAvatar

An athlete's initials in a small circle, in that athlete's own colour.

Source: `src/Gui/components/PpAvatar.qml`.

Use it wherever an athlete is listed — the athlete picker's cards and its list rows both draw one, and one athlete must be the same colour in both.

The consumer supplies the athlete's `uuid` (picks the colour) and `initials`.

## Props

| name | type | default | meaning |
|---|---|---|---|
| `uuid` | string | `""` | colour = `avatarColors[uuid.charCodeAt(0) % 6]`; empty → the first |
| `initials` | string | `""` | shown; empty → "?" |

## Geometry and colour

28 × 28 px circle (radius 14). Initials in the data face, 10 px, centred.

The six pastels are **identity colours, not theme colours**: an athlete keeps theirs in every theme, so they are literals here, not tokens — `#A8C4E0`, `#7EBFAA`, `#E0C4A8`, `#C4A8E0`, `#A8E0C4`, `#E0A8C4`. Border: 1 px `Qt.darker(colour, 1.3)`; ink `Qt.darker(colour, 2.2)`.

## Don't

- Don't map the avatar colours to the theme palette or the metric palette — the colour must not change when the theme does.
