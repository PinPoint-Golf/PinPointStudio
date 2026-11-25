# PpSwitch

The app's on/off switch: a 34 × 18 pill, accent when on, with a 12 px knob that slides across.

Source: `src/Gui/components/PpSwitch.qml`.

## When to use
Every boolean setting (Settings panels, toolbar camera/IMU panels, shot filter). It never changes itself: a press emits `toggled(!checked)` and the caller writes the setting, so `checked` stays bound to the value it shows.

## Props
| name | type | default | meaning |
|---|---|---|---|
| checked | bool | false | The value shown. |
| knobColor | color | white | The knob (camera/IMU panels pass `colorKnob`). |
| largeKnob | bool | false | Session View/Motion look: round ends, 14 px knob 2 px in, `colorOnAccent` when on, `colorText3` when off. |
| signal toggled(bool) | | | |

## Tokens / states
Track `colorAccent` on / `colorBg3` off, radius 9 (largeKnob: half height). Knob 12 px, radius 6, 3 px inset; slides over 120 ms.
