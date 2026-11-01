# PpSectionHead

THE typeset crosshead: a section's title with a quiet sub-line — numbered for a report or a long scroll ("1  Session patterns"), or the plain "title + quiet sub-line / count" head every typeset panel draws.

Source: `src/Gui/typeset/PpSectionHead.qml`.

## When to use
Heading a section inside a typeset register: Charts' sections and ledgers, Session diagnostics, Trust, the launch monitor board. In print it names the section PpPager repeats as "<section>, continued", and `breakBefore` starts it on a new page. The parts past number / title / sub are off unless asked for, so a plain head is exactly the numbered one.

Not for: a page's head in the app's chrome (`PpPageHeader`), a card's title (`PpCard.title` / `PpMicro`), or a view's opening head with a standfirst (`PpStandfirst`).

## Consumer supplies
`roles` and `title`; optionally `number`, `sub`, `count`, and the fold parts.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The register's PpTypeset. With no roles it draws nothing. |
| brush | var | null | |
| medium | string | `roles.medium` | |
| number | string | "" | The section number (`fFigS`, ink3). |
| title | string | "" | `fSection`, ink. Also its `section`. |
| sub | string | "" | The quiet sub-line. |
| rule | bool | false | A rule over the head. |
| breakBefore | bool | false | Print: start a new page. |
| paged | bool | true | false: not a pager unit (a head inside another block's atom, or screen only). |
| atom / keepNext | bool | `paged` | Pager unit; travels with what follows. |
| count | string | "" | A figure after the title: how many rows follow. |
| countOnBaseline | bool | false | The count on the title's baseline rather than centred in its line. |
| countFont | font | `roles.fFigS` | |
| foldable / folded | bool | false | A ▾ / ▸ caret before it all. |
| subFont | font | `roles.fSmall` | e.g. `fSmallI`, `fMicro`. |
| subColor | color | `roles.ink3` | e.g. a warn ink. |
| subGap | real | 0 | More room before the sub-line (u). |
| subFit | string | "line" | "line" centred in the title's line · "grow" the same, as tall as it wraps · "top" its own height, at the top · "baseline" on the title's baseline. |
| subWrap | bool | true | false: one line at its own width (its own line when it does not fit). |
| subClip | bool | false | One line, cut at the head's right edge. |
| note | string | "" | After the sub-line, ink2 (a folded section's one-line state). |
| gap / pad / lead | real | 10 / 10 / 0 | Room between the parts, under the head, over it (design units, through `g`). |
| ruleWeight | real | `roles.hw` | |
| ruleGap | real | 6 | Rule to the line. |

Read-only: `section` (= title), `keyKinds` ([] when paged, else undefined). It sizes itself to its parts when no width is given (a head in a Row); its `baselineOffset` is the title's.

## Typeset tokens
`roles.fSection` + `roles.ink` (title), `roles.fFigS` + `ink3` (number, count), `roles.fSmall` + `ink3` (sub, caret), `roles.fSmall` + `ink2` (note), `roles.rule` (rule), `roles.g(gap / pad / lead / ruleGap / subGap)`. The title's case and tracking come from the theme's typeset (`sectionCaps`, `sectionTracking`; Vector sets section heads in tracked capitals).

## Usage
```qml
PpSectionHead {
    width: parent.width; roles: ro
    number: "1"; title: qsTr("Session patterns")
    sub: qsTr("what held across the session")
    count: String(rows.length)
}
```

## Used by
Charts: `charts/PpChartsKindsView.qml`, `PpChartsRelationsView.qml`, `PpChFindingsLedger.qml`, `PpChFoldHead.qml`, `PpChPictureStrip.qml`, `PpChDeliverySession.qml`, `PpChPostureAddress.qml`, `PpChPostureMoved.qml`, `PpChPostureSession.qml`, `PpChPostureShotSection.qml`, `PpChWristSession.qml`, `PpChWristShotSection.qml`. Session diagnostics: `diagnostics/PpDiagAnalyse.qml`, `PpDiagPrint.qml`, `PpLedgerTable.qml`, `PpIndexList.qml`, `PpExpectations.qml`, `PpCauseGraph.qml`. Trust: `trust/PpTrustPanel.qml`, `PpTrustInline.qml`. The launch monitor board: `session/PpLmGraphicsBody.qml`.
