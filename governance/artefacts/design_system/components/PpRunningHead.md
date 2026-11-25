# PpRunningHead

Print furniture at the head of a page: a title on the left ("PinPoint Studio · Session diagnostics" by default), golfer · date · club · shot n of N on the right, a rule under it.

Source: `src/Gui/typeset/PpRunningHead.qml`.

## When to use
On a printed page. PpPager draws one on every page (its `title` replaces the left side); place one yourself only for print pages laid out without PpPager. Not for a screen view's head (`PpStandfirst`, `PpSectionHead`, or `PpPageHeader` in the app's chrome) and not for the page foot (`PpFolio`).

## Consumer supplies
`roles`, `source` (its `sessionDir` names golfer and date), `readout` (club, shot n of N), optionally `title`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The register's PpTypeset. |
| source | var | null | `{ sessionDir }`. |
| readout | var | null | |
| title | string | "PinPoint Studio  ·  Session diagnostics" | The left side. |

Read-only: `meta` = `roles.sessionMeta(source, readout, false)`; empty parts are left out of the right side.

## Typeset tokens
`roles.fMicro` + `roles.ink2` (both sides), `roles.rule` + `roles.hw` (the rule), `roles.g(6)` (under the text).

## Usage
```qml
PpRunningHead {
    x: ts.p.marginX; y: ts.p.headY; width: textW
    roles: ts; source: root.source; readout: root.readout
    title: qsTr("PinPoint Studio  ·  Launch monitor")
}
```

## Used by
`typeset/PpPager.qml` (Session diagnostics print) and `session/PpLmGraphicsBody.qml` (the launch monitor board's printed page).
