# PpNotes

Small print: a footnote paragraph, optionally under a kicker and a short rule, held to 1.3 × the prose measure.

Source: `src/Gui/typeset/PpNotes.qml`.

## When to use
Footnotes and notes in a typeset register, on screen and in print — what a table's figures mean, how a session was read, the notes on a printed board. Rich text is allowed. In print, `keepPrev` binds a footnote to the table above it when the pager cuts pages.

Not for: the app's quiet body line in a card (`PpCardNote`), a caption over table columns (`PpCaption`), or a notice that wants attention (`PpNotice`).

## Consumer supplies
`roles` and `text`; optionally `kicker`, `rule`, `tone`, `italic`, `small`, `keepPrev`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The register's PpTypeset. |
| brush | var | null | |
| medium | string | `roles.medium` | |
| text | string | "" | Rich text. |
| kicker | string | "" | A kicker over the note. |
| rule | bool | false | A short rule over it (u(28) wide). |
| tone | int | 3 | 3 = ink3, 2 = ink2. |
| italic | bool | false | `fSmallI`. |
| small | bool | false | false = micro size (footnotes); true = small size. |
| atom | bool | true | A pager unit. |
| keepPrev | bool | false | Keep with the atom above (print). |
| keyKinds | var (ro) | [] | Marks it uses (none). |

## Typeset tokens
`roles.fMicro` / `fSmall` / `fSmallI` (text), `roles.ink3` / `ink2` (by `tone`), `roles.fKicker` + `ink3` (kicker), `roles.rule` + `roles.hw` (rule), `roles.r.smallLeading` (line height), `roles.flows.measure × roles.em × 1.3` (width), `roles.g(5)` (spacing and top padding).

## Usage
```qml
PpNotes {
    width: parent.width; roles: ro
    kicker: qsTr("Notes"); rule: true; keepPrev: true
    text: qsTr("Each figure is the median of the shots in the window.")
}
```

## Used by
Session diagnostics: `diagnostics/PpDiagAnalyse.qml`, `PpDiagPrint.qml`, `PpDiagReviewFoot.qml`, `PpDiagSessionOutcomes.qml`. Charts: `charts/PpChartsKindsView.qml`, `PpChartsShotView.qml`. The launch monitor board: `session/PpLmGraphicsBody.qml`.
