# PpFactStrip

A strip of read-only facts across the foot of a Settings device card (cameras, IMUs, phones): equal-width cells, each a micro caps key over its value.

Source: `src/Gui/components/PpFactStrip.qml`.

## Consumer supplies
`cells`: `[{ key: "Battery", val: "82%", tint: "good" }]`; fill the card's width (it sizes its own height).

A Battery cell takes its tint from the device's battery band: ok "good", low "attention" (with "low" after the level), very low "error" (with "very low"). A very low battery is red, an approved exception to the README's "failure only"[^battery].

## Props
| name | type | default | meaning |
|---|---|---|---|
| cells | var | [] | One entry per cell; `tint` "accent" · "good" · "attention" · "warn" · "error" · "" (plain). |
| animateTint | bool | false | Fade a value's colour change over `durationFast` (120 ms). |

## Tokens & values
Cells on `colorBg2`, padding gap(9) top, gap(14) sides, height = content + 18 px; key PpMicro kind "caps" (data face 10 px, theme caps); value data face 12 px in `toneColor(tint, colorText)`, elided. A 1 px `colorBorderMid` separator at `borderOpacityNormal` between cells, none after the last.

[^battery]: Product Owner and Design Lead, 26 Nov 2026 (the battery colours: one set for every device; red for a very low battery approved as an exception to "red for failure only"; the words "low" and "very low" approved). "attention" in the tint list: the same ruling ("C5, and the battery colours", 26 Nov 2026), whose amber for a low battery reaches the Settings strips only through this tint; the component already colours it through `Theme.toneColor`. Logged for the Design Lead's review afterwards (UI-20).
