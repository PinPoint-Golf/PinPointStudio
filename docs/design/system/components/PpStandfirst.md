# PpStandfirst

A view's head block: a kicker, a display head, a standfirst sentence built from the data, and (screen only) quiet filter links.

Source: `src/Gui/typeset/PpStandfirst.qml`.

## When to use
Opening a typeset view, a masthead or a detail. `size: "head"` for a view, `"feature"` for a masthead or a detail opening. The standfirst is held to the prose measure. The sentence comes from the panel's own helper (Session diagnostics: `PpDiagRoles.shotStandfirst` / `sessionStandfirst` / `watchStandfirst(…, long)`, with `long = roles.shows("longStandfirst", width)`). In print, a standfirst that opens a section names it through `section`.

Not for: a section inside a view (`PpSectionHead`), a page's head in the app's chrome (`PpPageHeader`), or the display title with the brand sweep (`PpDisplayText`).

## Consumer supplies
`roles`, `head`, `standfirst`, optionally `kicker`, `size`, `chips` and `onChipActivated(key)`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The register's PpTypeset. |
| brush | var | null | |
| medium | string | `roles.medium` | |
| kicker | string | "" | |
| head | string | "" | The display head. |
| standfirst | string | "" | Rich text. |
| size | string | "head" | "head" (`fTitle`) · "feature" (`fFeature`). |
| chips | var | [] | `[{ key, label, n, on }]` — filter links, screen only. |
| interactive | bool | `medium === "screen"` | |
| section | string | "" | Print: the section it opens ("<section>, continued"). |
| atom / keepNext | bool | true | Pager unit; travels with what follows. |
| keyKinds | var (ro) | [] | |
| signal chipActivated(string key) | | | |

objectNames: `sdStandfirst`, `sdFilterTool` (each filter link; carries `key`, `on`).

## Typeset tokens
`roles.fKicker` + `ink3` (kicker), `roles.fTitle` / `fFeature` + `ink` (head, line height 1.08), `roles.fStand` + `ink2` + `roles.r.leading` (standfirst, width `roles.measureW(width)`), `roles.fLabel` + `ink` (a filter that is on), `roles.fLabelL` + `roles.link` (one that is off), `roles.g(7)` (stack), `roles.g(12)` (between filters), `roles.g(2)`.

## Usage
```qml
PpStandfirst {
    width: parent.width; roles: ro
    kicker: root.shotLine                         // e.g. "Shot 7 · 7 iron"
    head: root.fired.length ? ro.cap1(ro.word(root.fired.length)) + " outside the corridor" : "Inside every corridor"
    standfirst: ro.shotStandfirst(root.readout, ro.shows("longStandfirst", width))
    chips: root.filterChips
    onChipActivated: key => root.toggleFilter(key)
}
```

## Used by
Session diagnostics: `diagnostics/PpDiagAnalyse.qml`, `PpDiagPrint.qml`, `PpMasthead.qml`.
