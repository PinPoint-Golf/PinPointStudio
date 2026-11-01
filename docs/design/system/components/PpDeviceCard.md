# PpDeviceCard

The frame of one device card in Settings (cameras, IMUs, phones): the surface, a state-coloured border drawn over everything, and a 54 px header row that opens with a status dot.

Source: `src/Gui/components/PpDeviceCard.qml`.

Only the frame lives here; each Settings panel keeps its own body. Use it for every device row in Settings → Cameras / Motion Sensors / Phones so they share one frame.

The consumer supplies `headerItems` (what follows the dot: an alias field, toggles, buttons), the body (anchored to `headerRow.bottom`), and the card's `implicitHeight` from the header and the body (the header's top margin is not counted for you).

## Props

| name | type | default | meaning |
|---|---|---|---|
| `borderColor` | color | `colorBorderStrong` | the 1 px border, drawn above content (z 100); fades in `durationFast` |
| `statusColor` | color | `colorText3` | the status dot; fades when it changes |
| `dimmed` | bool | `false` | 0.5 opacity — a device that is excluded or forgotten |
| `headerItems` | list | — | alias of the header row's data |
| `headerRow` | Item | — | read-only, to anchor the body under |

## Geometry

Fill `colorSurface`, radius `Theme.radius`, clips its content. Header: 14 px margins, 54 px tall, 10 px spacing. Status dot: PpBadge `kind: "dot"`, plain, 6 px, centred vertically.

## Typical states (set by the panels)

| state | border | dot |
|---|---|---|
| idle / not connected | `colorBorderStrong` | `colorText3` |
| connected | `colorBorderStrong` | `colorGood` |
| needs attention (uncalibrated) | `colorAttention` | `colorAttention` |
| error | `colorError` | `colorError` |
| excluded | (any) at 0.5 opacity | |
