# PpToolPill

THE standard toolbar item: a glyph tile, a micro label, the current value and a ▾, with the shared hover/press motion.

Source: `src/Gui/components/PpToolPill.qml`.

## When to use
Every session-toolbar pill (View, Motion, Club, Cameras, IMUs) and anywhere outside the toolbar that presents the same control — never a lookalike. A popup it owns parents to the pill: `y: pill.height + 10; x: pill.width - width`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| label | string | "" | The current value. |
| active | bool | false | Its popup is open: accent border, accent glyph, held grown. |
| glyph | string | "▦" | Glyph in the 34 px tile (18 px symbol). |
| microLabel | string | caps("View") | The micro label above the value. |
| badge / badgeColor | bool / color | false / colorGood | 11 px corner dot (taped-club marker). |
| status | string | "" | Device state badge: "ok" check, "off" dashed ring, "attention"/"error" target in that tone; value inked in the warning tones. |
| chevron | bool | true | The ▾ affordance; device pills drop it. |
| signal clicked() | | | |

## Tokens / states
Height 44, width = content + 24, `Theme.radius`, 1 px border; tile 34×34 `colorSurface`; padding 11 (13 right without chevron), gap 11. Rest `colorBg2` + `colorBorderMid`; hover `colorBg3` + `colorAccentMid`; active `colorAccent` border. Value 12 px `colorText`; ▾ 10 px `colorText2`.
