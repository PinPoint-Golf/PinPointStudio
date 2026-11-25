# PpFactStrip

A strip of read-only facts across the foot of a Settings device card (cameras, IMUs, phones): equal-width cells, each a micro caps key over its value.

Source: `src/Gui/components/PpFactStrip.qml`.

## Consumer supplies
`cells`: `[{ key: "Battery", val: "82%", tint: "good" }]`; fill the card's width (it sizes its own height).

## Props
| name | type | default | meaning |
|---|---|---|---|
| cells | var | [] | One entry per cell; `tint` "accent" · "good" · "warn" · "error" · "" (plain). |
| animateTint | bool | false | Fade a value's colour change over `durationFast` (120 ms). |

## Tokens & values
Cells on `colorBg2`, padding gap(9) top, gap(14) sides, height = content + 18 px; key PpMicro kind "caps" (data face 10 px, theme caps); value data face 12 px in `toneColor(tint, colorText)`, elided. A 1 px `colorBorderMid` separator at `borderOpacityNormal` between cells, none after the last.
