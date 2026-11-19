# Charts and print: reuse, duplication and what to rationalise

> **Date:** 2026-11-19 · **Status:** proposal, nothing built. Parts B and C change the design system, so each needs Mark's approval before it is built: the need, the screens affected and a before/after sheet (CLAUDE.md, "UI and design system"). · **Scope:** `src/Gui/charts`, `src/Gui/typeset`, `src/Gui/components`, `src/Gui/report`, `src/Gui/diagnostics` at main `3620840d` + working tree, 18–19 Nov 2026.

> **Relation to the refactor (19 Nov 2026):** Part A is already planned as step 5's "one presenter base and one threading
> idiom for the six presenters" (`docs/implementation/swe-audit/04-remediation-plan.md`, step 5), which also moves the
> presenters' model/snapshot wiring and per-shot input cache onto the Session — so Part A is done there, not on its own.
> Parts B and C are design-system changes outside the plan: post-refactor (feature freeze) unless Mark adds them.

## 1. Summary

The Charts panel and the printed session report are mostly built the right way. Numbers are computed once, chart pieces are shared between screen and paper, and one typeset system draws on either medium. Copy-paste is low everywhere: 6% in `charts`, and 1% or less in the other four folders.

Duplication lives in three places:

1. **The Posture, Delivery and Wrist chart presenters** copy one another's generic machinery: the inputs, the job lifecycle, the cache, stills and test hooks. Their headers are 87–94% identical. → **Part A**, a shared presenter base. C++ only; no visible change.
2. **The session report draws its client pages in a palette and type system of its own** (`rr` in `PpSessionReport.qml`). The appendix, and every chart block on those same pages, draw with the typeset print medium (`Theme.typesetPrint`). So one PDF carries two print styles. → **Part B**, one print style. Design-system change.
3. **Print versions of screen components** that draw the same thing a second way: the P1–P8 profile, the trust note, the section head and the consistency layout. → **Part C**, one component per thing, with the medium as a setting. Design-system change. Depends on B.

Order: A is independent and can run at any time. B comes before C, because C's shared components need both media to draw with the same inks.

## 2. Method

- **Copy detection.** I normalised every non-blank, non-comment line of the `.qml`, `.js`, `.h`, `.cpp` and `.mm` files (whitespace collapsed; string literals → `S`; numbers → `N`; the family words posture, wrist, delivery, report, print and so on → `X`, to catch renamed copies). I then hashed every run of 6 lines and counted the runs found in more than one file. Tests were excluded. Licence headers, imports and brace-only lines were skipped.
- **Pairwise similarity** of parallel files (for example the three presenter headers) used a line-level sequence match. It over-counts short common lines (`}`, single properties), so it is treated as an upper bound.
- **Reading the code** for how screen and print share data and components: includes, the components each page instantiates, and the header comments that state intent.
- **Limits.** This is structural only. Nothing was rendered, the design lint was not run against `rr`, and the logic inside the presenters was not checked line by line. The copy figures say where code is repeated, not whether the copies have drifted. No drift bug was found or looked for.

## 3. Findings

### 3.1 Copy-paste by folder

| Folder | Files | Code lines | Repeated in another file |
|---|---|---|---|
| `charts` | 112 | 23,916 | 1,467 (6.1%) |
| `typeset` | 24 | 1,651 | 0 (0%) |
| `components` | 70 | 4,487 | 58 (1.3%) |
| `report` | 60 | 10,698 | 110 (1.0%) |
| `diagnostics` | 45 | 13,024 | 135 (1.0%) |

Between folders there are only 29 shared six-line runs in all (charts↔report 13, charts↔diagnostics 8, diagnostics↔report 8). Screen and print are not copies of each other.

### 3.2 What is done well — keep it

- **One computation, two renderers.** `report_posture.h`, `report_wrist.h` and `report_delivery.h` cut the report's pages from the chart presenters' own session payloads (`postureSessionReportPayload` and the others). Sentences and figures are kept verbatim, and the report adds only renumbering, caps and its one permitted sentence. In the header's words: "the page is the screen's reading, not a second one". The launch-monitor appendix runs the panel's own `LmSessionModel` and draws with its `PpLmGraphicsBody`. Tendencies come from `src/Coaching`.
- **Print embeds the screen's chart blocks.** Report pages instantiate `PpChMatrix`, `PpChBoxCell` → `PpBoxPlot` (the one box plot), `PpChScatter`, `PpChShotShapeGrid`, `PpChPostureAddress`, `PpChPostureFrame`, `PpChRunBox`, `PpSankey`, `PpBandBar` and `PpVariabilityBadge`, rather than redrawing them.
- **One typeset system, medium per instance.** `PpTypeset` takes a `medium`, and `PpChRoles` and `PpDiagRoles` are `PpTypeset`s with each panel's vocabulary on top. A window can show a panel and lay out its printed form at the same time. The diagnostics Print register (`PpDiagPrint`), now the report's technical appendix, uses the same blocks as Replay and Analyse and paginates on the shared `PpPager`.
- **Charts QML composes.** The five views share `PpChViewBase`. Each family sits in `PpChSection` with `PpChSectionControls`. Posture and Wrist draw their profile with one `PpChPositionProfile`. The ledgers are 24-line wrappers of `PpChFindingsLedger`, the picture strips wrap `PpChPictureStrip`, and every family's trust note is `PpChTrustFold`. The family Session pages share a skeleton but only about 30% of their lines, which is right for different content.
- **One background pool** (`presenter_pool.h`) serves all six presenters. The 11 Nov shutdown-freeze fix landed once and reached every chart.
- `charts` has no entries in the design-lint allowlist.

### 3.3 Duplication 1 — the Posture, Delivery and Wrist presenters

`ChartsPosturePresenter`, `ChartsDeliveryPresenter` and `ChartsWristPresenter` each derive directly from `QObject` and carry their own copy of the same machinery.

| Pair | Header lines identical | `.cpp` shared six-line runs (names normalised) |
|---|---|---|
| Posture ↔ Delivery | 94% of the smaller | 220 |
| Posture ↔ Wrist | 88% | 99 |
| Delivery ↔ Wrist | 87% | 63 |

**What is repeated:**

- **The QML-facing surface** (all three, plus Traces for most):
  - inputs: `model`, `pickedSwingDir`, `pickedDetailRef`, `register`, `scope`, `playheadUs`;
  - results: `available`, `busy`, `choices`, `thisShot`, `session`, `selected`, `standfirst`, and `seekUsFor()`.
  - `against` and `referenceSwingDir` are on Posture and Wrist, not Delivery.
- **The job lifecycle:** `schedule()`, `startJob()`, `apply()`, `flushIfSynchronous()`, `setSynchronous()`, `waitForIdle()`, the dirty flag and the pool wiring. In Delivery this is the block from `setModel` to `updateSelected`, about 230 lines, mirrored in Posture.
- **Plumbing:**
  - the snapshot and model handling (`setModel`, `currentSnapshot`, `setSnapshot`);
  - the per-shot input cache (`readInputs`, keyed on the shot's weak pointer);
  - finding a shot by swing folder (`cleanDir`).
- **Posture and Delivery only:**
  - the still maker (`StillMaker`, `stillMakerRef`, `makeStills`) and the loading of the picture from the swing document;
  - `setPictureDocumentForTest()`.

**What is not repeated:**

- **Chart logic:** each family's `compute…()`, `buildThisShot`, `buildChoices` and result shape.
- **Presenter-specific results:**
  - Posture: `pattern`;
  - Delivery: `plotLayers`;
  - Wrist: `archetype`, `curvesBehind`, `movesWith`.
- **The other three presenters:** Traces, Relations and Consistency are genuinely different and share only the pool.

Smaller pockets of shared helpers: `posture_picture.cpp` ↔ `delivery_picture.cpp` (12 runs), and `session_posture.cpp` ↔ `session_delivery.cpp` (15).

**Cost today.** A fix to the shared machinery has to be made three times. No case of the copies disagreeing was found.

### 3.4 Duplication 2 — two print styles in one report

The client pages of the session report (cover, About, Order, Tendencies, work-ons, Posture, Wrist, Club delivery, Consistency, the launch-monitor appendix) draw their text, rules and washes with `rr`, a `QtObject` of 61 properties declared at the end of `report/PpSessionReport.qml` and passed to every page as `ro`:

- palette pinned to Folio light by reading `Theme._themes.folio.light` directly, with `paper: "#FFFFFF"` literal;
- about 30 fonts at literal pixel sizes (`fH1` 31, `fBody` 13, `fMicro` 10…) and its own leading (`lead` 1.32);
- its own hairline (`hw` 0.75), wash, tint and edge alphas, and the studio accent.

The chart blocks on those same pages, and the whole technical appendix (`PpDiagPrint`), draw with the typeset print medium instead (`PpChRoles` / `PpDiagRoles` on `Theme.typesetPrint`: `printScale` 1.18, `hair` 0.5, the A4 margins). So a report page holds two style objects (`ro` and `roles`), and the PDF changes typographic system at the appendix. The `PpReportProfile` header says it outright: "INKS: `ro` (the report's palette and type…) and `roles` (the Charts print roles…)".

`rr` also sits outside the project's rule that every colour, font and size comes from `Theme` tokens. Its values are real decisions — Mark judged the mock's sizes — so the fix is to move them, not change them.

### 3.5 Duplication 3 — print versions of screen components

| Print | Lines | Screen | Lines | Shared | Real medium difference |
|---|---|---|---|---|---|
| `report/PpReportProfile.qml` | 240 | `charts/PpChPositionProfile.qml` | 465 | the box (`PpChBoxCell`) only | paper cuts the list into pager atoms (column heads, one atom per row, caption kept with the last row); no hover |
| `report/PpReportTrust.qml` | 94 | `charts/PpChTrustFold.qml` | 59 | nothing | paper has no fold, so it shows the note open, optionally in two balanced columns |
| `report/PpReportSectionHead.qml` | 44 | `typeset/PpSectionHead.qml` | 125 | nothing | a page-starting pager atom with a "continued" title |
| `report/PpConsistencyPage.qml` | 303 | `diagnostics/PpConsistencyView.qml` | 306 | `PpChMatrix` | paper caps the rows and has no picked shot |

Some differences are genuine (folds, pagination, interaction). But each pair draws the same row, cell or heading twice, with separate geometry. So a change to how a reading's band, fault lines, median or ⚠ words look has to be made on both. Minor: `report/PpPosturePage.qml` ↔ `PpWristPage.qml` share 14 runs, and the four `components/*Vocabulary.qml` singletons share one `word` / `line` / `accessibleName` / `_grade` skeleton.

## 4. Proposal

### Part A — a shared base for the section presenters (no visible change)

Introduce `ChartsSectionPresenter` (working name) in `src/Gui/charts/`, a `QObject` that owns:

- the common inputs and results above as `Q_PROPERTY`s, and the `inputsChanged` / `resultsChanged` / `busyChanged` / `selectedChanged` / `playheadChanged` signals;
- the job lifecycle on `PresenterPool` (`schedule`, `startJob`, `apply`, `flushIfSynchronous`, `setSynchronous`, `waitForIdle`);
- the snapshot and model wiring, the per-shot input cache and the find-by-folder helper;
- an optional stills service (still maker, picture-document loading, `setPictureDocumentForTest`), used by Posture and Delivery.

Each family keeps its `Inputs`, `Result` and compute function, and supplies them through a small virtual interface (or a CRTP template, if the result types make that simpler). Family-only results (`pattern`, `plotLayers`, `archetype`, `curvesBehind`, `movesWith`) stay on the subclass. Traces, Relations and Consistency stay as they are unless the base fits them naturally.

- **Size:** an estimated 600–900 lines removed across the three `.cpp`s. The headers shrink to their family parts. The estimate is soft: §2's matcher over-counts.
- **Behaviour:** none. QML property names and objectNames are unchanged.
- **Tests:** the existing presenter tests (via `setSynchronous` / `waitForIdle`) must pass unchanged. Add one test of the base's lifecycle with a trivial compute.
- **Approval:** not a design-system change. A refactor needs only Mark's usual go-ahead.

### Part B — one print style for the whole report (design-system change)

Move `rr`'s values into `Theme` as print tokens, under `Theme.typesetPrint` (for example a `report` key, the way `ch` is merged for Charts). Then give the client pages a `PpTypeset`-based roles object on the print medium — `PpReportRoles`, beside `PpChRoles` and `PpDiagRoles` — in place of `rr`. Then the client pages, the chart blocks on them and the appendix share one palette, one set of type roles, one hairline and one accent rule.

- Carry the current values over first, so the client pages draw as they do now. Settle the open differences (hairline 0.75 vs 0.5, `lead` vs the typeset leading, the literal `#FFFFFF`, Folio pinned vs the print medium's own palette) as explicit decisions, page by page, with Mark.
- Keep `Font.PreferNoHinting` (the reason is in `rr`'s comment: column fitting on Windows). It belongs in the print font roles for every print block.
- After changing `Theme.qml`: run `python3 tools/theme/make_design_system.py` and commit `tokens.json`.
- **Screens affected:** every page of the session report PDF; nothing on screen.
- **Before/after:** owed before approval. Render the stress set (`session_report_design.md` §9.2) old vs new and show a per-page diff sheet. If no value changed, it should be pixel-identical except for decisions Mark takes.

### Part C — one component per thing, the medium as a setting (design-system change; after B)

| Thing | Shared component | Screen use | Print use |
|---|---|---|---|
| P1–P8 profile row | one row component (label, band wash, fault lines, box, median, side-of-band word, ⚠ words, alt reading, row grades) | `PpChPositionProfile` lays the rows out in a column, with hover | `PpReportProfile` wraps each row as a pager atom and adds the column heads and caption |
| Trust note | `PpChTrustFold` with `open` / `columns` settings | folded | open, 1 or 2 columns |
| Section head | `PpSectionHead` with a page-start mode | as now | `breakBefore`, "continued" title |
| Consistency layout | the part of `PpConsistencyView` around `PpChMatrix` | as now | caps the rows; no picked shot |

- `PpReportTrust` and `PpReportSectionHead` become thin wrappers, or are removed. `PpReportProfile` keeps only the pagination.
- **Screens affected:** Charts › Posture and Wrist (profile), every Charts family's trust fold, the report's Posture, Wrist, Club delivery and Consistency pages, and the Session diagnostics Consistency tab.
- **Before/after:** owed per component, on screen in at least two themes, and in print.
- **Optional, small:** a shared base for the four `*Vocabulary.qml` singletons. Low value; do it only if one of them is being changed anyway.

## 5. Risks

- **Report layout is tuned and judged.** Mark accepted the client pages' look, and the pager depends on atom heights. B and C must start pixel-identical and change only by decision, proven with the stress renders, not a single fixture.
- **Teardown order.** Pages keep their roles object last among their children so boxes go before their inks (`PpReportProfile` header). A shared roles object must keep that rule.
- **Two media in one window.** `PpTypeset` is per-instance precisely so a panel and its printed form can coexist. A shared row component must take its inks from the `roles` it is given, never from a live theme token.
- **Windows.** Font metrics differ (the hinting note). Part B needs one render on the cabin PC before it is called done.
- **Another session is active in `src/Gui`.** Coordinate before starting A, which touches the three presenter files.

## 6. Open decisions for Mark

1. Approve Part A as a refactor (no visible change)?
2. Part B: one print style for the whole PDF? If yes, for each difference — hairline, leading, paper colour, Folio-pinned palette — keep the client pages' value or the typeset print value?
3. Part C: which components to unify first? The profile row is the largest and most-changed.
4. The `*Vocabulary.qml` base: skip unless touched?
