# PpMark

THE point mark used by every chart (dot plot, spread, run, sparkline, history, box plot, key): one shot at (cx, cy), fired or clean, with the picked shot's ink ring or the hovered shot's hairline ring.

Source: `src/Gui/typeset/PpMark.qml`.

## When to use
Any single shot drawn as a point in a typeset figure. The shape is the theme's (round in most, square in Vector), so never draw a shot dot from a Rectangle in a panel. Hovering it brushes the shot; clicking it emits `activated()`.

Not for: a session strip's cells (`PpShotStrip`), the not-measured mark (`PpStub`), a session pip in the app's chrome (`PpPip`), or a coaching-line badge (`PpBadge`).

## Consumer supplies
`roles`, `cx` / `cy`, `fired`, and for a live mark `brush`, `shotId` and `interactive: true`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | null | The panel's PpTypeset. No roles, nothing drawn. |
| brush | var | null | The register's PpBrush. |
| shotId | int | -1 | |
| fired | bool | false | Outside its corridor: warn, filled. |
| other | bool | false | Out on the other side (drawn as clean). |
| cx / cy | real | 0 | The centre. |
| size | real | `roles.markFired` / `markClean` | The mark's diameter (side). |
| ringSize | real | `roles.ringD` | The picked / hovered ring (smaller in a sparkline). |
| interactive | bool | false | Hover brushes, click activates (only with a shotId ≥ 0). |
| ringed | bool | `brush.isPicked(shotId)` | The ink ring. |
| signal activated() | | | |

Read-only: `ok`, `hovered` (`brush.isHovered(shotId)`), `square`, `hollow`.

## Typeset tokens
`roles.warn` (fired fill), `roles.ink3` (clean fill or outline, hover ring), `roles.ink` (picked ring), `roles.square` / `roles.r.cleanHollow` (shape), `roles.markFired`, `roles.markClean`, `roles.ringD`, `roles.dim` (opacity under a highlight), `roles.normal` (opacity fade).

## States
- **Fired:** filled in warn. **Clean:** ink3 — a 1 px ring where the theme's clean mark is hollow, filled where it is not (Vector).
- **Picked:** + an ink ring, 1.25 px. **Hovered** (not picked): + an ink3 ring, 1 px.
- **Highlight elsewhere:** opacity `brush.shotOpacity(shotId, roles.dim)`, faded over `roles.normal`.
- Round marks snap to whole pixels with an even diameter; the hit area is at least 12 px.

## Usage
```qml
Repeater {
    model: shots
    delegate: PpMark {
        required property var modelData
        roles: ro; brush: br; interactive: true
        shotId: modelData.shotId; fired: modelData.fired
        cx: xOf(modelData.v); cy: rowY
        onActivated: root.openShot(shotId)
    }
}
```

## Used by
Session diagnostics: `diagnostics/PpDotPlot.qml`, `PpRunChart.qml`, `PpSparkline.qml`, `PpSmallMultiple.qml`, `PpHistoryStrip.qml`, `PpCauseGraph.qml`. Charts: `charts/PpChRunBox.qml`, `PpChScatter.qml`, `PpChMovesWith.qml`, `PpChSeqSession.qml`, `PpChWristSession.qml`. Inside `typeset/PpBoxPlot.qml` and `typeset/PpPageKey.qml`.
