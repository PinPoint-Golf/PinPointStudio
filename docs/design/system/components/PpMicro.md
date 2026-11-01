# PpMicro

A micro heading: the app's caps micro type, quiet by default — the eyebrow over a page or group, a field's head, a pill's small label, a card's title.

Source: `src/Gui/components/PpMicro.qml`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| kind | string | "" | "" data face, mixed case (spell capitals with Theme.caps; it carries live values) · "caps" data face, theme caps · "body" body face, theme caps · "bodyWeight" "body" at the body weight |
(Plus Text's `text`, `color`, `font.letterSpacing`.)

## Tokens & values
10 px (fontSzMicro), `trackingMicro` (0.8 px), weight 400 (body weight for "bodyWeight"), `colorText3`. Callers tint it (a card's title in its tone) or loosen to `trackingData` for asides and counts.

## Do / don't
- Do keep acronyms (IMU, P4, DTL) in capitals in the source.
- Don't set kind "caps"/"body" on a label carrying live values.
