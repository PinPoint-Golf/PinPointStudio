# PpPageKey

The key: each entry a drawn mark and its word, flowed. On screen one key per view, above its figures (never repeated per figure); in print one key per page in the footer, holding only the marks that page uses.

Source: `src/Gui/typeset/PpPageKey.qml`.

## When to use
To say what the marks of a typeset view mean. Every swatch is drawn by the same component the figures use (PpMark, PpStub, PpShotStrip, PpBoxPlot, the link-grade strokes), so the key cannot drift from the figures. In print, PpPager (through PpFolio) builds each page's key from the `keyKinds` every block exposes.

Not for: a chart legend of metric colours (`PpMetricSwatches` in the app's chrome), or explanatory prose (`PpNotes`).

## Consumer supplies
`roles` and either `kinds` or a named `set`; optionally `readout` and `extraWords`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| roles | QtObject | `PpTypeset {}` | The register's PpTypeset. |
| brush | var | null | |
| medium | string | `roles.medium` | |
| readout | var | null | Names the ring: "shot n of N" (else "this shot"). |
| set | string | "" | shot · session · watch · figure · paths · pathsShort |
| kinds | var | [] | Explicit kinds; wins over `set`. |
| extraWords | var | {} | A panel's own word for a kind, for this instance. |
| atom | bool | true | A pager unit. |
| keepNext | bool | true | Travels with what follows (print). |
| keyKinds | var (ro) | [] | |

Read-only: `shotLabel`, `words`, `sets`, `list` (the kinds drawn, de-duplicated).

**Kinds:** `fired`, `clean`, `ring`, `hover`, `median`, `fault`, `pass`, `corridor`, `bar`, `tick`, `stub` (`gap` = stub), `strip`, `box`, `fit`, `chance`, `dots`; the cause-graph nodes `n-fired`, `n-clean`, `n-root`, `n-ghost`, `n-outcome`; the link grades `l-movedTogether`, `l-conditionallyDependent`, `l-coherent`, `l-presentTogether`, `l-unanchored`.

**Sets:** `shot` = fired, clean, corridor, fault · `session` = fired, clean, ring, median, fault, corridor · `watch` = bar, tick, stub · `figure` = fired, clean, ring, median, fault, pass, corridor · `paths` / `pathsShort` = the five node kinds and five link grades.

objectName: `sdPageKey`.

## Typeset tokens
`roles.fLabel` + `roles.ink3` (words), `roles.g(14)` (between entries), `roles.g(5)` (swatch to word), `roles.u(10 / 12 / 16 / 22 / 30)` (swatch sizes), `roles.ink`, `ink2`, `ink3`, `warn`, `fault`, `passEdge`, `hw`, `ringD`, `square`, `r.barRadius`, `r.passDash`, `r.ch.fitLineW`, `r.ch.chanceDash`, `linkStyle()`, `dashFor()`, `dashPath()`.

## States
- A `strip` entry absorbs `bar`, `tick` and `stub`/`gap` (a page with a ledger strip and a barcode keys the strip once); `gap` and `stub` together key once.
- The not-measured mark is a stub, never a gap; the link grades key with their own strokes, arrowheads only where the grade carries one.
- In print the box swatch takes the paper weights (`style.print`).

## Usage
```qml
PpPageKey { width: parent.width; roles: ro; readout: root.readout; set: "figure" }
```

## Used by
Session diagnostics: `diagnostics/PpDiagAnalyse.qml`, `PpBarcode.qml`, `PpCauseGraph.qml`. Charts: `charts/PpChartsShotView.qml`, `PpChartsRelationsView.qml`. Print: `typeset/PpFolio.qml` (every page PpPager draws).
