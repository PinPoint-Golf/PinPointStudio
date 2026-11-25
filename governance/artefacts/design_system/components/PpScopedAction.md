# PpScopedAction

One tile of a device panel's scoped action row (Scan / Connect / Calibrate): a glyph over a short label, outlined at rest and attention-framed when `primary`.

Source: `src/Gui/components/PpScopedAction.qml`.

## When to use
The camera and IMU panels' action rows. The caller sets `width` (the panels split the row into three equal cells).

## Props
| name | type | default | meaning |
|---|---|---|---|
| glyph | string | "" | Symbol-font glyph, 16 px. |
| label | string | "" | 12 px body label. |
| primary | bool | false | Work to do: attention wash and frame. |
| connecting | bool | false | Runs the PpConnectingFrame comet around the border. |
| signal triggered() | | | |

## Tokens / states
Height 50, `Theme.radius`, 1 px border, glyph/label 4 px apart. Outline: transparent → `colorBg2` on hover, `colorBorderStrong`, glyph `colorText2`, label `colorText`. Primary: `colorAttentionLight` (lighter 1.08 on hover), `colorAttention` border, glyph and label. Hover-grow/press-dip via PpPressable.
