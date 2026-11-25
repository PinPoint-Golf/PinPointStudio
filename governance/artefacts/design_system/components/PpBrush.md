# PpBrush

The shared focus state of a register: the picked shot, the hovered shot, the open condition and any highlight — so a thing that is hovered or picked is shown ONCE in every block that contains it.

Source: `src/Gui/typeset/PpBrush.qml`.

## When to use
One instance per register, passed to every block as `brush`. Blocks write `hoveredShot` / `hoveredCondition` themselves on hover, so brushing needs no wiring; clicks are only signals, which the register routes to the real body. It also carries the after-shot pulse that PpPulseWash draws.

It draws nothing (a `QtObject`). Not for: app-wide selection outside a typeset register (a list's own selection, a filter) — those stay in their screen's model.

## Consumer supplies
`pickedShot` (bound to the readout's shotId), optionally `pickedCondition` and `cards`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| pickedShot | int | -1 | The carousel's shot: the ink ring (points) or the pick column (strips). Stays. |
| hoveredShot | int | -1 | Follows the pointer: the same form, lighter. |
| pickedCondition | string | "" | The open condition (detail.id), drawn strong in lists. |
| hoveredCondition | string | "" | |
| highlightShots | var | [] | Shot ids: the answer to a question; everything else recedes. |
| highlightConditions | var | [] | Condition ids, likewise. |
| question | string | "" | The active question's id (PpQuestions). |
| cards | var | [] | The session cards: teaches the brush each shot's swingDir. |
| pulseIds | var | [] | Condition ids to pulse, focus first. |
| pulseToken | int | 0 | Bumped by `pulse()`; a new token restarts the wash. |
| pulseStagger | int | 70 | ms between rows, in pulse order. |
| pulseHold | bool | false | Renders and demo states only: hold the wash at full. |

Read-only: `shotDirs`, `shotFilter`, `conditionFilter`.

Functions: `dirOf(shotId)`, `isPicked(id)`, `isHovered(id)` (hovered and not picked), `shotLit(id)`, `conditionLit(id)`, `shotOpacity(id, dim)` / `conditionOpacity(id, dim)` (1, or `dim`, default 0.28), `hoverShot(id, on)`, `hoverCondition(id, on)`, `lightShots(ids, q)`, `lightConditions(ids, q)`, `clear()`, `pulse(ids, focusId)`, `pulseOrder(id)`.

## Tokens
None of its own. Blocks pass `roles.dim` (0.28) to the opacity functions; the pulse timing lives in PpPulseWash (`Theme.durationFast` / `durationSlow`).

## States
- **Picked** vs **hovered**: `isHovered` is false for the picked shot, so a shot never draws both.
- **Highlight**: `lightShots` clears any condition highlight and vice versa; `clear()` drops highlights, question and hover.
- **Pulse**: `pulse(ids, focusId)` moves the focused condition to the front and bumps the token; PpTypeset's `takePulse()` hands a register's cue to it once per token.

## Usage
```qml
PpBrush {
    id: br
    cards: root.cards || []
    pickedShot: ss.shot ? ss.shot.shotId : -1
    pickedCondition: root.view === "detail" && root.detail ? (root.detail.id || "") : ""
}
PpLedgerTable { roles: ro; brush: br }
```

## Used by
`charts/PpChartsRegister.qml` (Charts), `diagnostics/PpDiagAnalyse.qml`, `diagnostics/PpDiagReplay.qml` and `diagnostics/PpDiagPrint.qml` (Session diagnostics Analyse, Replay, Print).
