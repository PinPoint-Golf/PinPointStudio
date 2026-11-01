# PpViewFlick

One register view's own named Flickable: it hangs under the tab row, fills the rest of its parent, builds its view on first show and never tears it down, so scroll and hover survive a republish.

Source: `src/Gui/typeset/PpViewFlick.qml`.

## When to use
Each view of a typeset register with tabs (Charts, Session diagnostics Analyse and Replay). A register specialises it once — its roles, its shown tab, its own measures — as an inline component, then names one per view; the panel can save and restore its place by the flick's objectName.

Not for: a plain scrolling page outside a typeset register, or print (`PpPager`).

## Consumer supplies
`roles`, `key`, `view` (a Component), `shown` (the register's current tab), `below` (the item it hangs under), and the register's `gap` / `foot`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | null | The register's PpTypeset. |
| key | string | "" | The tab this view belongs to. |
| view | Component | null | Built the first time `shown === key`. |
| shown | string | "" | The register's current tab. |
| below | Item | null | Hangs under it (the tab row, or what opens under it). |
| gap | real | 0 | Space above the view (design units, through `roles.g`). |
| foot | real | 0 | Room after it (through `roles.g`). |
| scrolls | bool | true | false takes the drag away (the auto-closing cast). |
| lockWhenFits | bool | true | false: a view that fits still takes the drag (Analyse). |

Read-only: `on` (`shown === key`), `content` (the built view), `built`.

## Typeset tokens
`roles.g(gap)` / `roles.g(foot)` (`Theme.gap` with no roles), `roles.u(8)` (the view is built at the flick's width less u(8)). Vertical `ScrollBar` as needed; clipped; stops at bounds.

## Usage
```qml
component ViewFlick: PpViewFlick { roles: ro; shown: root.shown; below: under; gap: 10; foot: 20; scrolls: root.interactive }
ViewFlick { id: shotFlick; objectName: "chShotFlick"; key: "shot"; view: shotC }      // shotC: the view's Component
```

## Used by
`charts/PpChartsRegister.qml` (Charts), `diagnostics/PpDiagAnalyse.qml` and `diagnostics/PpDiagReplay.qml` (Session diagnostics).
