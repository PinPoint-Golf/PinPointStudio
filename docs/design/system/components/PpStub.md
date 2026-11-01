# PpStub

The not-measured mark: a short flat ink3 bar standing on the foot of a figure or strip, so a shot that could not be measured is present and never a gap.

Source: `src/Gui/typeset/PpStub.qml`.

## When to use
Every place a typeset figure sets down a shot it has no reading for — one drawing for all of them. The caller places it (x / y or anchors) and names it; tests count stubs by objectName (`sdRunStub`, `sdSparkStub`, `sdTileStub`, `sdShotStub`).

Not for: a missing figure in text (that is "-", from `roles.figure()`), a clean shot (an ink3 tick in PpShotStrip, a clean PpMark in a plot), or an empty session pip (`PpPip`).

## Consumer supplies
`roles`, `size` (the mark height it stands in for), and the shot `pitch` where there is one; a position and an objectName.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | var | null | The panel's PpTypeset. No roles: zero height, transparent. |
| size | real | 12 | The full mark height `stubH` is a share of, px. |
| pitch | real | Infinity | The shot pitch, px. |
| share | real | 0.5 | Of the pitch. |
| minW | real | 2 | |
| maxW | real | `roles.u(4)` | Width with no pitch. |

Width = max(minW, min(maxW, pitch × share)); height = `roles.stubH(size)` (22 % of the size, half-pixel, at least 1.5).

## Typeset tokens
`roles.ink3`, `roles.stubH()`, `roles.u(4)`.

## Usage
```qml
PpStub {
    objectName: "sdRunStub"
    x: xOf(i) - width / 2; y: plotBottom - height
    roles: ro; size: ro.markFired; pitch: pitchPx
}
```

## Used by
Session diagnostics: `diagnostics/PpRunChart.qml`, `PpSparkline.qml`, `PpSmallMultiple.qml`. Charts: `charts/PpChRunBox.qml`. Inside `typeset/PpShotStrip.qml` and `typeset/PpPageKey.qml`.
