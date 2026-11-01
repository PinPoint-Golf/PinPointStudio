# PpFolio

Print furniture at the foot of a page: the page's key (only the marks this page uses), a hairline, a quiet footer with the generated date, and "Section · Page n of N".

Source: `src/Gui/typeset/PpFolio.qml`.

## When to use
On a printed page. PpPager draws one on every page itself; place one yourself only when you lay out print pages without PpPager. Not for screen views (a view's key is a PpPageKey above its figures) and not for a page head (`PpRunningHead`).

## Consumer supplies
`roles`, `footer`, `section`, `page`, `pageCount`, and the page's `keyKinds`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The register's PpTypeset (the default only covers creation order). |
| readout | var | null | Passed to the key ("shot n of N"). |
| footer | string | "" | The quiet footer; " · generated <d MMM yyyy>" is appended. |
| section | string | "" | Set before "Page n of N". |
| page | int | 1 | |
| pageCount | int | 1 | |
| keyKinds | var | [] | The PpPageKey kinds this page uses; empty hides the key. |

## Typeset tokens
`roles.g(5)` (stack), `roles.g(10)` (key spacing), `roles.g(20)` (footer to page number), `roles.hw` / `roles.hair` (the rule), `roles.fMicro`, `roles.ink3` (footer), `roles.ink2` (page number).

## Usage
```qml
PpFolio {
    x: roles.p.marginX; width: textW
    roles: ts; footer: qsTr("Final session state")
    section: cur.section; page: printPage + 1; pageCount: pages.length
    keyKinds: cur.keys
}
```

## Used by
`typeset/PpPager.qml` (so Session diagnostics Print; the session report, `report/PpSessionReport.qml`, paints paper over the pager's furniture and draws its own) and `session/PpLmGraphicsBody.qml` (the launch monitor board's printed page).
