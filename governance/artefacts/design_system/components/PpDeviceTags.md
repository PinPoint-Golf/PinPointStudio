# PpDeviceTags

What recorded a session — one PpChip per device: grey outlines, and the ones the device filter asked for in the accent.

Source: `src/Gui/components/PpDeviceTags.qml`.

Use it as a row of any session list (the carousel's drawer, the golfer's sessions on Home and the golfer page), alongside a PpDeviceFilterBar. Hidden when there are no labels.

The consumer supplies the session's device `tokens` (session_summary.h, display order), their short `labels` (same order) and the filter's `selected` tokens.

## Props

| name | type | default | meaning |
|---|---|---|---|
| `tokens` | var | `[]` | device tokens in display order |
| `labels` | var | `[]` | short names, same order |
| `selected` | var | `[]` | the filter's tokens — those chips go accent |

## Parts

Row 18 px tall, 4 px spacing. Each PpChip: 18 px capsule, text + 12 px, mono micro (data face 10 px, `trackingData`).
- Not selected: `tinted: false` → transparent, `colorBorderStrong` outline, `colorText2` ink.
- Selected: tinted in `colorAccent` → fill accent at 9 % (12 % dark), border accent at 45 %, accent ink.
