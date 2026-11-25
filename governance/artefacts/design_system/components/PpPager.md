# PpPager

Print: one document on A4. The blocks inside it are laid out once as a galley at the text-block width, then cut into pages only between atoms, with a running head, a folio and the page's key on every page.

Source: `src/Gui/typeset/PpPager.qml`.

## When to use
A typeset register's print medium: put the same blocks you show on screen inside it, with a print PpTypeset. Because the galley is the text-block width, the same size classes apply (portrait ≈ compact, landscape ≈ medium).

Not for: screen scrolling (`PpViewFlick`), or a single fixed printed page that needs no cutting.

## Consumer supplies
`roles` (a PpTypeset with `medium: "print"`), the blocks as children, `printPage`, and optionally `readout`, `source`, `footer`, `title`. The register binds its print contract to it: `printPageCount: pager.pageCount`, `pager.printPage: printPage`.

## How it cuts
- **Atoms:** any item with `atom: true` (a table row, an entry, a figure, a note, a section head); a library block (it has `keyKinds`) with no atoms inside is one atom; plain containers are walked; invisible or zero-height items are skipped.
- **Keeps:** `keepNext` binds an atom to the next (heads travel with what follows); `keepPrev` binds a footnote to its table; `breakBefore` starts a new page.
- **Continued:** a page that continues a section repeats its heading as "<section>, continued" (named by a PpSectionHead, or any block with a `section` property).
- **Repeating heads:** a block with a non-empty `printGroup` names a list; its first atom is the list's head and is redrawn at its true height (a live 2× ShaderEffectSource) on a page that opens part-way through the list.
- **No stranded pages:** when a keep-chain would leave a page less than `minFill` full, it may break inside a list (never between a head and its first item); a list cut carries at least `minWidow` atoms over unless that is already a quarter page; `minTail` (opt-in) keeps a section's last page at least that share full.
- An atom taller than a page overflows honestly: clipped at the foot, never split.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The print PpTypeset (page geometry from `roles.p`). |
| brush | var | null | |
| readout | var | null | For the running head and key. |
| source | var | null | For the running head (session meta). |
| footer | string | "" | The folio's footer. |
| title | string | "" | The running head's left side; "" keeps PpRunningHead's own. |
| printPage | int | 0 | The 0-based page drawn. |
| showKeys | bool | true | A key in each page's folio. |
| minFill | real | 0.62 | |
| minWidow | int | 3 | |
| minTail | real | 0 | 0 = off; the session report sets 0.25. |
| blockGap | real | `roles.g(18)` | Between blocks in the galley. |
| content | default alias | | The galley's children. |

Read-only: `pageCount`, `pages`, `textW`, `pageH`, `contH`, `cur`. Functions: `pageOf(item)` (the 1-based page an item starts on), `repaginate()`, `paginate()`.

## Typeset tokens
`roles.p.marginX`, `p.headY`, `p.top`, `p.bottom` (A4 at 96 dpi logical), `roles.u(22)` (the "continued" line), `roles.g(8)` (under a repeated head), `roles.g(14)` (folio from the foot), `roles.fLabel` + `roles.ink3` ("continued"). Paper is `#FFFFFF`.

## Usage
```qml
PpPager {
    id: pager
    anchors.fill: parent
    roles: ro                              // PpDiagRoles { medium: "print" }
    readout: root.readout; source: root.source
    footer: qsTr("Final session state")
    printPage: root.printPage
    PpSectionHead { roles: ro; number: "1"; title: qsTr("Session patterns"); breakBefore: true }
    PpLedgerTable { roles: ro; … }
}
```

## Used by
`diagnostics/PpDiagPrint.qml` (Session diagnostics print) and `report/PpSessionReport.qml` (the session report, with `minTail: 0.25`; it paints over the pager's furniture and draws its own).
