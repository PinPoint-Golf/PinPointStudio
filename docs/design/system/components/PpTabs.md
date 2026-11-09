# PpTabs

The view toggles of a typeset register: label + quiet count, the current one ruled underneath; optionally a note and a quiet chip at the right. Chrome thins last and least — the toggles never go.

Source: `src/Gui/typeset/PpTabs.qml`.

## When to use
Switching between the views of a typeset register (Session diagnostics Analyse and Replay, the Charts register, the launch monitor board) and Home's Coaching | Your swing | Tendencies | Matchups bar (`size: "title"`). Screen only. Keyboard: the bar is ONE Tab stop (the current tab, or the first when none is current); Left/Right move between tabs (wrapping), Home/End go to the ends, Space/Enter/Return activate; a focused tab or chip draws an accent focus ring. Tabs are `Accessible.PageTab` in an `Accessible.PageTabList`; the chip is a `Link`.

Not for: one tab of a stock TabBar in the app's chrome (`PpTabButton`), or choosing a value in place (`PpSegmentedControl`).

## Consumer supplies
`roles`, `tabs`, `current`, and `onTabActivated(key)` that writes `current` back; optionally `note`, `chip` + `onChipActivated()`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The register's PpTypeset. |
| brush | var | null | |
| medium | string | `roles.medium` | |
| interactive | bool | `medium === "screen"` | false on an auto-closing cast: drawn, never pressed. |
| tabs | var | [] | `[{ key, label, count }]`. |
| current | string | "" | The current tab's key. |
| note | string | "" | e.g. the review note (tier 3). |
| chip | string | "" | e.g. "‹ Back", a chart preset; "" = none. |
| chipOn | bool | false | Draw the chip in ink (its picker is open). |
| chipObjectName | string | "sdTabChip" | |
| size | string | "" | "" (the view toggles) · "title" (Home's tab bar). |
| accentUnderline | bool | `size === "title"` | Rule the current tab in accent, `Theme.tabUnderline` thick. |
| noteAfterTabs | bool | false | Put the note straight after the tabs, on the labels' baseline. |
| signal tabActivated(string key) | | | |
| signal chipActivated() | | | |

Read-only: `isTitle`, `counts`, `tabFont`, `onBaseline`, `currentIndex`, `keyKinds`. Function: `focusTab(i)`.

objectNames: `sdTabBar` (callers may override), `sdTabRow`, `sdTab`, `sdTabLabel`, `sdTabCount`, `sdTabRule` (the current tab's underline), `sdTabFocus`, `sdReviewNote`, the chip (`chipObjectName`), `sdChipFocus`.

## Typeset tokens
`roles.fTab` (labels; title size: the display family at `roles.u(Theme.fontSzTab)` and `r.displayWeight`), `roles.ink` (current) / `ink2` (others), `roles.fFigS` + `ink3` (counts), `roles.ink` 2 px rule or `roles.accent` at `u(Theme.tabUnderline)`, `roles.fSmall` / `fSmallL` + `roles.link` / `ink3` / `ink` (chip), `roles.fSmall` + `ink3` (note), `roles.hair` (1 px rule under the bar), `roles.accent` (focus ring), `roles.u(28)` (height; title size: font height + u(14)), `roles.g(26)` between tabs from compact (else `g(18)`), `roles.g(5)`, `roles.g(24)`. Theme: `fontSzTab`, `tabUnderline`.

## States
- **Counts** show from the compact class (`shows("tabCounts")`); below it each view's head states its own.
- **Note** shows from the medium class (`shows("reviewNote")`) and only when it fits beside the tabs and chip.
- **Chip:** a link while interactive, plain ink3 otherwise, ink when `chipOn`.
- **Title size:** larger display labels, accent underline, note and chip on the labels' baseline.

## Usage
```qml
PpTabs {
    width: root.width; roles: homeTs
    size: "title"; noteAfterTabs: true
    tabs: [{ key: "coaching", label: qsTr("Coaching") }, { key: "swing", label: qsTr("Your swing") }]
    current: root.tab
    onTabActivated: key => root.tab = key
}
```

## Used by
`home/HmHomeTabs.qml` (Home, title size), `diagnostics/PpDiagAnalyse.qml` and `diagnostics/PpDiagReplay.qml` (Session diagnostics), `charts/PpChartsRegister.qml` (Charts), `session/PpLmGraphicsBody.qml` (the launch monitor board).
