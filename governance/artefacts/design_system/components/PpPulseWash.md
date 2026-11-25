# PpPulseWash

The after-shot pulse: one soft ink wash over a row (or a tile, a mark's cell) whose condition changed state on the shot just surfaced. It rises fast and fades slowly, staggered by the row's place in the pulse.

Source: `src/Gui/typeset/PpPulseWash.qml`.

## When to use
In every block with condition rows, so a new shot shows what changed. Put it as the FIRST child of the row so it sits under the text: `PpPulseWash { anchors.fill: parent; … }`. The register starts it by calling `brush.pulse(ids, focusId)` (through PpTypeset's `takePulse()`).

Not for: hover or selection (PpBrush's `rowHover` / `pickWash`), or a transient message (`PpToast`).

## Consumer supplies
`roles`, `brush`, and the row's `conditionId`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | null | The panel's PpTypeset. |
| brush | var | null | The register's PpBrush (its `pulseIds`, `pulseToken`, `pulseStagger`, `pulseHold`). |
| conditionId | string | "" | This row's condition. |
| level | real | 0 | Animated 0 → 1 → 0. |

Read-only: `order` (`brush.pulseOrder(conditionId)`), `held`, `pulsing`. Function: `fire()` (called on each new `pulseToken`).

objectName: `sdPulseWash`.

## Tokens
`roles.ink` at 0.11 (dark) / 0.08 (light) via `Theme.dark`; rise `Theme.durationFast`, fade `Theme.durationSlow` (OutCubic); delay = order × `brush.pulseStagger` (70 ms).

## States
- Inert under `Theme.reduceMotion`, in print, and when the condition is not in the pulse.
- `brush.pulseHold` (renders only) holds the wash at full so a still can show it.
- A new token restarts the wash even for the same ids.

## Usage
```qml
Item {                                     // a condition row
    PpPulseWash { anchors.fill: parent; roles: ro; brush: br; conditionId: row.id }
    Text { … }
}
```

## Used by
Session diagnostics: `diagnostics/PpLedgerTable.qml`, `PpBarcode.qml`, `PpDotPlot.qml`, `PpSmallMultiple.qml`, `PpSparkRow.qml`.
