# PpDivider

A hairline rule — horizontal by default, vertical on request.

Source: `src/Gui/components/PpDivider.qml`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| orientation | int | Qt.Horizontal | Qt.Horizontal (24 × 1 implicit) or Qt.Vertical (1 × 24). |
Callers size it with `Layout.preferredWidth` or an explicit width.

## Tokens & values
1 px `colorBorder` at opacity 0.6 — reads as a 0.5 px hairline (the same weight as `colorHairline`).
