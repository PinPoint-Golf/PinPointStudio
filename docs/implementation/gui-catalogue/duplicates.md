# GUI duplication — consolidated findings

**Re-audited 31 Oct 2026 at `9ac19450`** (first audit 19 Oct at `8f5225a8`; 80 commits to `src/Gui` since). Sources: four read-only re-check reports, one per cluster, which re-counted every original finding on both trees with one method and then read the code added since — [A interactive primitives](findings/2026-10-31-A-interactive-primitives.md), [B containers & scaffolding](findings/2026-10-31-B-containers-scaffolding.md), [C charts & data-viz](findings/2026-10-31-C-charts-dataviz.md), [D screens & flows](findings/2026-10-31-D-screens-flows.md) — plus the regenerated scripted evidence in [data/duplicate-evidence.md](data/duplicate-evidence.md). The 19 Oct reports ([A](findings/A-interactive-primitives.md), [B](findings/B-containers-scaffolding.md), [C](findings/C-charts-dataviz.md), [D](findings/D-screens-flows.md)) hold the original file:line detail and proposed APIs; the 31 Oct reports hold the current locations. Finding IDs (`A1`, `B3`…) point into them; IDs above the old ranges (A20–A24, B26–B34, C23–C36, D16–D24) are new.

**Nothing here has been changed in the repo, and no finding was run in the app.** Visual-difference and drift claims are read from code, not rendered. Anything that changes how something looks needs Mark's eye before it is unified.

## Headline

- **Nothing was consolidated in twelve days; the duplication grew.** Of the 81 original findings: **0 fixed by merging**, 3 fixed or moot only because files were deleted (B20, B18, C8), 7 partly better (D1's bug fix, deletions, Home's new shared rows), 47 unchanged, **24 worse** (more copies). 37 new findings were filed; 33 after merging the ones two clusters both found (A20 = B30, A22 = B31, A23 = B34; B33 re-counts D9).
- **The GUI layer grew 17 %:** 290 → 340 app QML files, 90k → 105k lines; GUI C++ 73k → 98k. A whole new area, the session report and Sessions screen (`report/`, 22 files, 6.5k lines), was built since.
- **New features were built by copying the nearest sibling, not by extracting a shared base.** Posture and Club delivery copied the Wrist chart files (C23–C26); Trunk calibration copied the arm routine, status column and Check page (D3, D4, D16); Settings → Studio copied ArchivingPanel's row furniture (B28); IntentPage copied GoalsPage's tile (A3). Each of these is now a three-way copy where it was two.
- **Shared components did appear, and new code uses them** — `PpConfirm`, `PpCheckRow`, `PpRowAction`, `PpExpandRow`, `PpTableRow`, `PpFilePicker`, `PpCard{table}`, `HmFaultDetail`, `PpChTrustFold`, `PpChShotShapeGrid`, `PpReportProfile`; `PpChBoxCell` doubled its users and the one-box-plot rule holds. But **no old copy was migrated onto them** — `Main.qml`'s close prompt is still the hand-drawn original that `PpConfirm` was extracted from (A20).
- **Copies are starting to disagree in ways a user can see** — see the next section. That is the strongest reason to act now.

## Status after the 31 Oct fixing run (uncommitted at the time of writing)

Each item was implemented, independently reviewed and shown on a before/after sheet (`build/run-me/gui-dedupe/REVIEW.md`,
`sheets/`). Fixed: §1 Settings skeleton/rows/switch (all 11 panels, `SettingsPanelBase`, `SettingsRow`, `SettingsRowTitle`,
`SettingsSectionHead`, `PpSwitch`) · §2 every switch · A20/B30/D13 close prompt · A21 links · A22/B31 menu rows · A23 `PpTextArea` ·
A24 checkbox · A5 scoped action · A11/A13 pickers · A17/D24/D10-maths 3-D helpers · B5/C11 reader's place and session folder ·
B8/B9 fact strip and device-card shell (settings) · B14/B13 section heads and trust toggle · B17/C10/C9 flick, review foot, cast zoom,
column wrap · B19 avatar · B22 setup intros · B24 `Qt.alpha` (100 sites) · B26/B27/B29 Sessions helpers · B32/D6/D19/D20 Home ·
C1/A18/D11 dead files and `FilmController` (≈3,500 lines) · C3 samples library · C5/C6/C20 typeset primitives · C21 corridor ladder
published by C++ (fixed an upside-down one-sided tile) · C15/C16/C17/C33/C34/C35 one C++ `src/Core/figure_format.h` + its QML twin ·
C23/C25/C26 Charts profile/picture/ledger · C24/C18 section shell and seek · C27 (At address)/C28/C30/C31 report reuse ·
C32 one shot-shape classifier (LM thresholds) · D2 Resource Monitor · D3/D4/D18/D1 calibration base, status column, guide view ·
D5 updater wording · D7/D9/D23 athlete · D16/D17 setup check pages · D21 capability row.
Held on purpose: §21 look-bearing variants and typeset-pending pieces (A2, A3, A4, A8, A9, A10, A14, A15, B4, B6, B7, B10, B11,
B12, B16, D8, D14), C2 (retire `PpMetricChart`), C29 (report roles), C27's What-moves-with print mode, D10 scene base, the monitor
cards' chrome, the shown-session `sessionDir` expression ×3. Existing bugs found on the way: #127, #128, #129.

## ⚠ Copies that already disagree (user-visible or rule drift)

| # | What | Where | Effect | Finding |
|---|---|---|---|---|
| 1 | Byte sizes: binary (1024³) vs SI (10⁹) | `ScreenSessions`/`PpSessionReportList`/`PpSessionsSelectionBar` `bytesWords` vs `StoragePanel`/`ArchivingPanel` `formatBytes` (+3 more formatters) | The same library reads ≈7 % different on Sessions and Settings → Storage; empty shows "0 MB", "" or "—" | B26 |
| 2 | Shot shape classified twice in C++ | `LaunchMonitor/lm_inferred_reads.h:62` vs `charts/session_delivery.cpp:689` | A 1.8° spin axis is a curve in Club delivery and straight on the LM card; 9–10° axis is a slice in one, a fade in the other; names spelled differently | C32 |
| 3 | Figure + unit joined 4 ways | `charts_words.h figUnit`, `session_spread.h`, `session_diagnostics_model.cpp`, `report_words.h` … (7 copies) | "13 % hand rise" (Diagnostics) / "13% hand rise" (report) / "13%" (Charts) | C33 |
| 4 | "At impact" window | `charts_words.h:186` (±2 ms) vs `report_words.h:313` (rounds to 0) | 1.6 ms before impact reads "at impact" in Charts, "2 ms before impact" in the report | C34 |
| 5 | Trunk heading re-derived in QML | `CheckTrunkPage.qml:65-82` vs C++ `trunk_angles::liveHeadingDeg` | Page shows a number where the C++ (and the routine) say "no reading" — non-finite, near-vertical, uncalibrated | D17 |
| 6 | `file://` URL built without encoding | `PpSessionReportList.qml:234` vs `StudioPanel.qml:97`, `PpFilePicker.qml:86` | A PDF path with `#` or `%` would open the wrong file (from reading only) | D22 |
| 7 | Capability dots, same state two colours | `ReadyPage.qml:241` vs `HmRigCapability.qml:68` (`dotKind` re-derives `CapRows.toneOf`) | "partly" is amber on Ready, accent on Home; "none" grey vs outline | D21 |
| 8 | Session-action reasons worded twice | `PpSessionRowMenu`, `PpSessionsSelectionBar` (rule in 4 files) | "still recording" vs "The live session is still recording", etc. | B29 |
| 9 | Updater wording (unchanged since 19 Oct) | `PpAboutDialog`, `GeneralPanel`, `PpUpdateBanner` | "Checking…" vs "Checking for updates…"; `unsupported` → "✓ Up to date" in one | D5 |
| 10 | Calibration routine contract | `TrunkRoutine` vs the two arm routines | Trunk has no `reset()` and logs differently — the host contract does not bind it | D3 |

**D1 (issue #50) is fixed** — 58607538 gave `HmInstance` a `fullyCalibrated` and both QML copies read it. Residue: the 15°/25° literals still sit unguarded in `WitmotionArmRoutine.qml:410`, and "wG3 is calibrated" is re-derived as `calibrationState === 2` in three calibration files although `fullyCalibrated` now says exactly that (D1, D18).

---

## Themes, ranked by value — status at 31 Oct

Status key: **WORSE** = more copies than 19 Oct · STILL · PARTLY · FIXED/MOOT.

### 1. Settings panel skeleton and setting rows — *A7, B1, B2, B28, B8, B9, A6* — **WORSE**
- **B1:** the scroll/search skeleton (`lastHighlightId`, `findChild`, `scrollToItem`, `highlightTimer`, `contentCol`) is now in **11** panels (StudioPanel added).
- **A7/B2:** hand-built highlight rows 60 → 61; but **StudioPanel already contains the proposed `SettingsRow`** as a private `component SettingRow` (`StudioPanel.qml:214`, 21 uses via `FieldRow`/`ImageRow`), with `RowTitle` copied from ArchivingPanel (**B28**). Promoting it to `settings/SettingsRow.qml` is the cheapest way into B2.
- **B8/B9/A6:** capability strip ×3 + 2 and device-card shell ×3 + 2 — unchanged.

### 2. One on/off switch — *A1, B3* — **WORSE**
24 → **26** implementations, 43 use sites: 8 inline `TogglePill` (StudioPanel new), `SetupTogglePill`, 2 `MiniToggle`, 15 anonymous (GeneralPanel's spoken-instructions toggle new). Knob `"white"` in 21. Still no `PpSwitch`.

### 3. Buttons, chips, links, menus that bypass the shared ones — *A2, A3, A4, A8–A10, A19, B12, A21, A22, A24* — **WORSE**
- **A2:** 77 hand-sized buttons in 28 files — net zero (two migrated, two added). `PpButton` still has no compact size.
- **A3:** choice chips 22 → **23**, now **6** visual dialects (`PpDeviceFilterBar` new, `IntentPage` copy of GoalsPage).
- **A21 (new):** 22 accent links hand-rolled beside `PpLink`; `PpFilePicker` has its own `PlainLink`; 4 callers repeat the same disabled-colour override because `PpLink` has no disabled look.
- **A22 = B31 (new):** popup menu rows written 3 ways — `PpSessionRowMenu.MenuLine` (best), `PpSessionDrawer`, `PpShotActionBar`; the ⋯ trigger built twice.
- **A24 (new):** `ScreenAthleteForm` hand-draws the checkbox `PpCheckRow` now provides.
- A4, A8, A9, A10, A19: unchanged.

### 4. Charts / Diagnostics / Trust panel shells, and now the chart *sections* — *B5, C10, C11, B17, C9, C7, C12, B15, C24* — **WORSE**
- **B5/C11:** reader's place ×2, `_pickShot` ×2 (+`_focusShot`), `_folder` ×4 + a fifth variant (`PpSessionBulkJobs._same`, skips the backslash step), `_pointAtSession` ×3. 785f1c2f made the same edit to both reader's-place copies — the twins are maintained in lockstep by hand.
- **C24 (new):** the Posture, Wrist and Delivery shot sections (and their Session twins) repeat the same ≈30-line shell: 8-line preamble, `seekTo`, fold head and folded height, `sets` builder, trust fold. **Every future Charts section will copy it again.** → `PpChSection`.
- **C10:** view preamble 5 → 8; **C12:** value→pixel maps 19 → **27**; **C7:** run charts 2 → **3** (C28). C9, B15, B17 unchanged.

### 5. The old `review/` chart stack — *C1, C2, C8, C4* — PARTLY  ⚠ Mark's call
722cf3c2 deleted `PpReplayCharts` and `PpSequenceStrip` (C8 moot). Still dead: `MetricLibrary`, `MetricDetail`, `MetricRow`, `NormativeBar` (≈1,316 lines) and `PpGateNote`, all still in `cmake/PinPointQmlModule.cmake`. `PpMetricChart` (1,521) is still mounted only at `PpCameraTiles.qml:367` (compact) — the commit says it stays as the short replay graph. C4 (two curve painters) unchanged.

### 6. `ScreenResourceMonitor.qml` copy-paste of itself — *D2* — STILL
Untouched since 19 Oct (2,389 lines; 4 tabs, 10 outline buttons, 2 filter boxes, 2 memory tables, zebra rows ×6).

### 7. Calibration — *D3, D4, D16, D18, D24* — **WORSE**
- **D3:** the routine scaffold is now **three** copies (`TrunkRoutine` 887 lines); 51 lines are in all three, and the contract has already drifted (no `reset()`, different logging). → `CalibrationRoutineBase`.
- **D4:** `CalibrateTrunkPage` is a third copy of `ArmCalibrationStatus`'s status column (same "HOLD STILL — CAPTURING" string, hold bar, Recalibrate); guide `BodyVizView` blocks 2 → 3; hand-written step headers 3 → 6. The on-screen cue words are separate from `spoken_instructions.js`, which calls itself "the ONE place each line's words live".
- **D16 (new):** `CheckTrunkPage` is a clone of `CheckArmPage` (intro, readings box, Recalibrate link with a bare `MouseArea`).
- **D18 (new):** C++ tuning constants mirrored in QML — guarded by lint W11 for the trunk routine, unguarded for `WitmotionArmRoutine`'s 15°/25°.
- **D24 (new):** `BodyVizView` holds two copies of its slerp animation (arm and trunk).

### 8. Business rules copied instead of called — *D1, D5–D8, C3, C21, C17, B29, C32, C34, D17, D20–D23* — **WORSE**
- **D1** fixed (see above). **D5** updater ×3, drifted, unchanged. **D7** bag sort 3 → 4 (one missed on 19 Oct); Home club picker now a `PpComboBox`. **D8** tone→colour switch ≈9 → ≈11 sites.
- **D6** PARTLY: Home rows now sit on `PpCard{table}`/`PpTableRow`/`PpExpandRow` and the opened fault is one `HmFaultDetail` — real progress — but **D19 (new):** the "fault opens in place" wiring and `faultFor` are written three times (`HmFocus`, `HmSwingSummary`, `HmAreaRow`). → `HmFaultRow`.
- **D20 (new):** two Home components read-modify-write the same `homeViewByAthlete` map. **D21 (new):** capability *logic* is correctly in one place (`capability_facts.js`) — keep that pattern — but the rows are drawn twice in different colours. **D22/D23 (new):** Studio restates `ReportMailer`'s sender rules; athlete-form text rules beside C++ twins.
- **B29, C32, C34, D17** — see the disagreement table.
- **C3, C21** unchanged. **C17** WORSE: `report_words.h` (1,119 lines, new) carries its own number words, `cap1`, `plural` instead of including `charts_words.h`; `PpDiagRoles` still regex-parses C++ strings.

### 9. Typography: eyebrows, page heads, section heads — *B4, B6, B13, B14, B16, A16, B32, B33* — **WORSE**
Caps-micro labels 89 → 90; eyebrow + title page heads 17 → **20**; typeset `fSection` heads 16 → **21** (Posture/Delivery); local heading components 3 → 4 (StudioPanel's `SectionHead`, which applies `capsFont` *and* `Theme.caps()`); `PpCardNote` recipe inline 24 (churn); collapsible folds: `PpChTrustFold` redraws `PpTrustSummaryFold`'s toggle, and the new shared `PpExpandRow` chevron has no old users. **B32 (new):** `Caption` ×2 in Home copies `PpCard`'s caption. **B33 = D9:** athlete group card ×5, field label ×23.

### 10. Popups, notices, cards, tiles — *B7, A12, D13, A20, B10, B11, B25, B19, B22, D9, A15* — **WORSE**
- **A20 = B30 = D13 (new, cheapest win):** `Main.qml:178-278` close prompt is the hand-drawn original of `PpConfirm`; only the width differs. Hand-written popup preambles 4 → 7.
- **B7/A12:** 13 inline popup backgrounds (unchanged); new popups chose `PpPopoverCard` (17 → 20).
- **B10:** `PpToast` hosts 5 → 6, all `showUndo: false`. **B22:** setup pages bypassing `SetupStepIntro` 2 → 4. **B25** PARTLY: `PpCard` users 2 → 6. B11, B19, A15 unchanged. **B20 FIXED** by deletion (topo backdrop now one user).

### 11. Pickers and text inputs — *A11, B23, A13, A14, A23, C22* — **WORSE**
A14: inputs bypassing `PpTextField` 13 → 15; a fourth search-field variant (`ScreenSessions`). **A23 = B34 (new):** a boxed multi-line field built twice (`StudioPanel.AreaField`, `PpReportSheet`), border tokens already differ → `PpTextArea`. A11, A13, B23 unchanged; C22 still legitimate.

### 12. Colour spelling and missing tokens — *B24, D14* — STILL
`Qt.rgba(X.r,X.g,X.b,a)` 128 → 130 in 36 files (new code mostly writes `Qt.alpha`, +36, but nothing was swept). Ink-on-accent still written three ways at 23 sites; no `Theme.colorOnAccent`/`colorOnScrim`.

### 13. 3-D scaffolding — *D10, A17* — STILL (+D24)

### 14. Plot primitives — *C5, C6, C18–C20, C35, C36* — **WORSE**
Scrub-to-seek ×3 plus 4 new direct `shotReplay` seeks (C18); `byKey` ×2, `byId` ×2 plus new identical `planeLayersOf`/`withPlane` pair (C19); **C35 (new):** QML `fin(v)` ×10, C++ `fin` ×7, `PpChPlaneLabels` re-declares `PpChPlot`'s map. C5, C6, C20 unchanged; `PpBoxPlot` still has no `style`. **C36:** Posture pattern strip hand-built beside `PpShotStrip`.

### 15. C++ formatters — *C15, C16, C17, C33* — **WORSE**
Base number formatters 6 → **9** (the two `r2` copies differ on "−.00"); QML twins 2 → 4; unit joining ×7 with 4 rules (C33). C16: tick generators still 3 with different cutoffs, but `axisTicks` is now the single source for Wrist, Posture, Delivery and the report — no fourth was added.

### 16. Global-singleton coupling — *D12* — STILL (a pattern)
1,058 → 1,086 references in 55 files. Top: `PpMarkupPanel` 130, `Main` 109, `ScreenResourceMonitor` 89, `AudioPage` 79, `FilmPage` 78 (dead), `PpSessionToolbar` 61 (repo-wide `GeneralPanel` 106). New Home cards (`HmHomeTabs`, `HmSwingAreas`) follow the injection convention; `HmRigCapability` reads globals directly.

### 17. NEW — the session report re-implements Charts blocks — *C23, C25–C31, B26, B27* 
Built since 19 Oct; reuses `PpChBoxCell`, `PpChScatter`, `PpChRunBox`, `PpChPostureFrame`, `PpChShotShapeGrid` in print roles (so reuse is proven possible), but also:
- **C23:** the P1–P8 profile drawn three times (`PpChWristProfile`, `PpChPostureProfile` — "the wrist profile's form" — and `PpReportProfile`).
- **C25:** `PpChDeliveryPicture` is "the twin of" `PpChPosturePicture` (containment 0.81). **C26:** `PpChPostureLedger` copies `PpChWristLedger` (0.81).
- **C27:** report "At address" and "moves with the lead wrist" redraw `PpChPostureAddress`/`PpChMovesWith` (byte-identical `px(v)`).
- **C28:** a third run chart in `PpWorkOnPage`. **C29:** the report has its own shot mark and a third roles vocabulary (`rr`, `PpTypeset`, `PpChRoles` on one page). **C30:** page head, trust block, `Para`, squeeze timer repeated per page. **C31:** fit-to-widest-word columns ×3.
- Sessions-screen side: **B26** byte formatters, **B27** `plural` ×3 (bypasses Qt's `%n`), **B29** action rules ×5.

---

## 18. Merge map — which components could become one

> **Read §21 first.** For look-bearing components (chips, pills, badges, buttons, notices, popups) §21 retargets or holds several rows. The "Now" column is the 31 Oct status.

### 18a. Near-identical copies — safe to collapse into one file each

| Target (new unless stated) | Absorbs | Lines | Effort | Now | Finding |
|---|---|---:|---|---|---|
| `PpConfirm` (exists) | `Main.qml` close prompt | ≈90 | S | new | A20, B30, D13 |
| `PpSwitch` (promote `SetupTogglePill`) | 8 `TogglePill`, `SetupTogglePill`, 2 `MiniToggle`, 15 anonymous | ≈600 | S–M | worse (26) | A1, B3 |
| `SettingsPanelBase` | skeleton in 11 settings panels | ≈500 | M | worse (11) | B1 |
| `SettingsRow` + `SettingsRowTitle` (promote StudioPanel's) | 61 hand-built rows, `RowTitle` ×2, `SectionHead` ×2 | ≈1,100 | S to promote, M–L to adopt | worse | A7, B2, B28 |
| `CalibrationRoutineBase` | scaffold in Witmotion, HackMotion and Trunk routines | ≈100 | M | worse (3) | D3 |
| `PpChSection` (+ `PpChSeek`) | shell of Posture/Wrist/Delivery shot sections and Session twins; 4 direct seeks | ≈180 | M | new | C24, C18 |
| `PpChPositionProfile` | `PpChWristProfile`, `PpChPostureProfile` (later `PpReportProfile`) | ≈300 | M (M–L with print) | new | C23 |
| `PpChPictureStrip` | `PpChPosturePicture`, `PpChDeliveryPicture` | ≈90 | S | new | C25 |
| `PpChFindingsLedger` | `PpChWristLedger`, `PpChPostureLedger` | ≈90 | S–M | new | C26 |
| `HmFaultRow` | fault-opens-in-place wiring ×3, `faultFor` ×3 | ≈75 | S–M | new | D19 |
| `PpMenuItem` / `PpMenuRule` (promote `MenuLine`) | drawer and action-bar menu rows, `ModelTools.Action` | ≈150 | S–M | new | A22, B31 |
| `PpTextArea` | `StudioPanel.AreaField`, `PpReportSheet` note | ≈40 | S | new | A23, B34 |
| `Theme.formatBytes` (or `QLocale::formattedDataSize` invokable) | 8 byte formatters (pick SI or binary) | ≈50 | S + decision | new | B26 |
| `PpSessionBulkJobs.whyNot/reportable` | session-action rules in 5 files | ≈60 | S–M | new | B29 |
| `SetupRecalibrateLink`, `SetupReadingsBox` (+ use `SetupStepIntro`) | `CheckTrunkPage` / `CheckArmPage` clones | ≈80 | S | new | D16, B22 |
| `SetupCapabilityRow` | Ready and Home capability rows; drop `dotKind` | ≈60 | S + colour decision | new | D21 |
| report: `PpReportSectionHead`, `PpReportTrust`, `PpReportPara`, `PpFitColumns` | per-page head/trust/Para ×3–4; column fit ×3 | ≈150 | S | new | C30, C31 |
| `ModelTypeAhead` | `ModelPicker`, `ModelCausePicker` | ≈120 | S–M | still | A11, B23 |
| `PpScopedAction` | `ScopedAction` ×2 | ≈55 | S | still | A5, B21 |
| `PpViewFlick` (+ `PpNotes`) | `ViewFlick` ×3, `ReviewFoot` ×2 | ≈100 | S–M | still | B17, C10 |
| `PpAvatar` | avatar code in `PpAthleteCard`/`PpAthleteRow` | ≈50 | S | still | B19 |
| `VizOrientationMarker` | `OrientationTab` ×2 | ≈40 | S | still | A17 |
| `PpUpdateStatus` | updater presentation ×3 | ≈100 | S + wording | still | D5 |
| `PpKeyFigureGrid` | KeyFigures column-wrap ×2 (+2 in `PpLmGraphicsBody`) | ≈40 | S | still | C9 |
| `PpFactStrip` | capability strip ×3 + monitor cells | ≈170 | S–M | still | B8 |
| `PpStagePanelBase` / `PpReaderPlace` | Charts/Diagnostics/Trust panel shell | ≈250 | M (traps in comments) | still | B5, C11 |
| ~~`PpTopoBackdrop`~~ | — | — | — | fixed by deletion | B20 |
| ~~`PpSequenceChecks`~~ | — | — | — | moot | C8 |

### 18b. Extend an existing component, then retire the copies

| Existing component | Add | Retires | Finding |
|---|---|---|---|
| `PpLink` | disabled colour from `interactive`; `current` state | `PlainLink`, 4 overrides, most of 22 inline links | A21 |
| `PpCheckRow` | trailing slot/note | `ScreenAthleteForm` checkbox | A24 |
| `PpExpandRow` | (use it) | the older hand-drawn folds | B13 |
| `PpButton` | `size` (compact/icon), outlined `tone` | 77 hand-built buttons; `TBtn`, `PipBtn`, `MlButton`, `ModelBarButton`; the ⋯ tile | A2, A10, A22 |
| `PpChipGroup` / `PpChip` | `wrap`, multi-select, object options | 23 hand-built choice chips | A3, D7 |
| `PpSegmentedControl` | object options, compact, disabled segment | `ElementSegmented`, `PpSpeedSelector` | A9 |
| `PpComboBox` | — (use `valueRole`) | `ModelEnumField`, `ModelTable` enum popup | A13 |
| `PpTextField` | `compact`, `monospace`, `clearable`; `PpSearchField` | 15 boxed inputs; 4 search-field variants | A14 |
| `PpPopoverCard` | `plain`/border colour | 13 inline popup backgrounds | B7, A12 |
| `PpMicro` / `PpCardNote` | `caps`, `body`; a `label` variant | 90 eyebrows, 4 heading components, 24 note recipes, 23 athlete field labels, `Caption` ×2 | B4, B16, A16, B32, B33 |
| `PpSectionHead` (typeset) | `count`, `subStyle`, `fold` | 21 hand-made `fSection` heads, `PpChFoldHead`, `SectionHeader` ×2 | B13, B14 |
| `PpBadge` | warn/error kinds | `SetupStatusCircle`, the 6 px dots | A15 |
| `PpStageCard` / `PpCardShell` | (use them) | self-drawn frames in two bodies; hand-rolled surfaces | B15, B25 |
| `PpBoxPlot` | `style` | marks assembly in 3 adapters; `PpPageKey` glyph | C6 |
| `PpChRunBox` | band + target label | `PpWorkOnPage.RunChart`; later `PpRunChart` | C7, C28 |
| `PpChPostureAddress`, `PpChMovesWith` | print mode (pager atoms, folio hue) | report's redrawn blocks | C27 |
| `PpTypeset` | `byKey`/`byId`, `fin`, `dashPath`, `wrapColumns` | 4 `byKey`/`byId`, 10 `fin`, 3 dash loops | C19, C20, C35, C9 |
| `charts_words.h` / new `figure_format.h` (C++) | one `fig`, `figSigned`, `withUnit`, `whenImpact`, `fin`, number words | 9 formatters, 7 unit joiners, `report_words.h` word copies | C15, C17, C33, C34, C35 |
| `lm_inferred_reads.h` | one shot-shape classifier returning indexes | `session_delivery.cpp deliveryShotShape` thresholds | C32 |
| `ImuInstance` / `ImuManager` | `trunkTurnFrom(ref)`; tuned constants exposed to QML | `CheckTrunkPage` heading maths; QML-mirrored constants | D17, D18 |
| `ReportMailer` | `fromAddress`, `senderName` | Studio's restated sender rules; a `toFileUrl` helper for D22 | D22 |
| `AthleteController` | `bagClubIds`, `greetingDefault`, `normaliseReportEmails`, `validDob` | bag sort ×4; form rules | D7, D23 |
| `AppSettings` | `homeView(uuid)` / `setHomeView` | the two read-modify-writes | D20 |

### 18c. Merge only after a decision from Mark (look, behaviour, or product)

| Candidate | Why it needs a decision | Finding |
|---|---|---|
| Byte sizes: binary or SI | one screen's numbers change by ≈7 % | B26 |
| One shot-shape classifier | thresholds and names are product choices; both pinned by tests | C32 |
| One unit-joining rule; one "at impact" window | strings pinned by many tests | C33, C34 |
| Capability-row colours (Ready vs Home) | which is right under "colour only for outside its corridor" | D21 |
| `PpToast` + `PpNotificationHost` | notification-centre contract; all 6 `PpToast` uses set `showUndo: false` | B10 |
| `PpChRunBox` + `PpRunChart` (+ report run chart) → one run plot | linear vs fault-forward scale; heavily probed | C7, C28 |
| Report roles: one `PpReportRoles extends PpTypeset` | whether the report shares the typeset type scale | C29 |
| `PpDeviceCard` shell | huge files; extract the shell only | A6, B9 |
| `PpPageHeader` (with `SetupStepIntro` as setup variant) | 20 screens; small visual drift | B6, B22 |
| `VizScene` base for the 3-D views | Qt 6.11 debug-render lifetime guards | D10, D24 |
| `Theme.toneColor()`, `Theme.colorOnAccent` | light-mode ink changes visibly at ~23 sites | D8, D14 |
| `PpGroupCard` for the athlete form (+ clubs section) | consolidation across screens | D9, B33 |
| Retire `review/PpMetricChart` and dependants | product decision; Capture uses it as the short replay graph | C2 |
| Delete the dead metric-catalogue chain, `PpGateNote`, `FilmPage` | deletion | C1, D11 |

## 19. Likely copied code that is not a component

| What is copied | Where | Copies | Single home | Finding |
|---|---|---:|---|---|
| "any connected IMU needs calibrating" loop; "connection gave up" label match | `PpImuPanel`, `PpSessionToolbar`; 3 files | 2 / 3 | `ImuManager` invokable | D1 |
| `calibrationState === 2` for "wG3 calibrated"; 15°/25° literals | `ImuCalibrationFlow`, `HackMotionArmRoutine`, `ArmCalibrationStatus`; `WitmotionArmRoutine:410` | 3 / 1 | `fullyCalibrated`; exposed C++ constants | D1, D18 |
| trunk heading and wrap | `CheckTrunkPage`, `TrunkRoutine._wrap180`, C++ `trunk_angles` | 3 | C++ | D17 |
| `findChild` / `scrollToItem` / `highlightTimer` | 11 settings panels | 11 | `SettingsPanelBase` | B1 |
| `bytesWords` (identical) + 5 other byte formatters | Sessions files; Storage, Archiving, monitor, file picker, table row | 3 + 5 | `Theme.formatBytes` | B26 |
| `plural(n, one, many)` (identical) + inline | `ScreenSessions`, `PpSessionsSelectionBar`, `PpSessionReportList` + 7 inline | 3 + 7 | `qsTr("%n …", "", n)` | B27 |
| can-report / has-PDF / who + reasons | 5 Sessions files | 4–5 | `PpSessionBulkJobs` | B29 |
| `faultFor(conditionId)` | `HmFocus`, `HmSwingSummary`, `HmAreaRow` | 3 | `HmFaultRow` | D19 |
| `homeViewByAthlete` read-modify-write | `HmHomeTabs`, `HmSwingAreas` | 2 | `AppSettings` | D20 |
| sender address / name | `StudioPanel`, `PpSessionReportList`, C++ `ReportMailer` | 2 + C++ | `ReportMailer` | D22 |
| path → `file://` URL | `StudioPanel`, `PpFilePicker`, `PpSessionReportList` (unencoded) | 3 | `QUrl::fromLocalFile` invokable | D22 |
| `_folder()` session-folder normaliser | Charts, Trust ×2, +1; `PpSessionBulkJobs._same` (variant) | 4 + 1 | one session-path function | C11, B5 |
| `_pointAtSession`, `_screenActive` | Charts, Trust, Diagnostics; 5 `_screenActive` | 3 / 5 | `SessionMode` | B5, C11 |
| `_pickShot()` | Charts, Diagnostics (+ LM `_focusShot`) | 2 + 1 | stage-panel base | B5 |
| reader's-place save/restore | Charts, Diagnostics | 2 | `PpReaderPlace` | B5 |
| `seekTo(us)` (identical) + inline seek | 3 ShotSections + `PpChPostureSession` | 4 | `PpChSeek` | C18, C24 |
| measured / mean / nearest predicates | 4 QML files (2 `_measuredAt`) + C++ | 4 + C++ | one helper | C3 |
| `byKey` / `byId`; `planeLayersOf` / `withPlane` | Charts figures, `PpTypeset`, `PpDiagReplayState` | 4; 2 | `PpTypeset` | C19 |
| value→pixel map; y-axis gutter | Charts and report figures | 27 / 8 | `PpChScale`, `PpChYAxis` | C12 |
| finite test `fin` | 10 QML; 7 C++ | 17 | `PpTypeset.fin`; one C++ | C35 |
| dash-stroke loop | 3 | 3 | one helper | C20 |
| cast-zoom formula | 2 | 2 | `PpCastSurface` | C10 |
| "not measured" stub; break slash | 6 / 3 | 6 / 3 | `PpTypeset` helpers | C5 |
| fit-to-widest-word columns, `xOf` | `PpNumbersTable`, `PpLmAppendix` ×2 | 3 | `PpFitColumns` | C31 |
| `capitalised()`, `maxPips`; dead copies in `HmGoesTogether` | Home files | 3 / 2 | `HmText.js`; delete the dead ones | D6 |
| `clubModel` (the athlete's bag) | `ScreenHome`, `PpClubPanel` (byte-identical), `ScreenAthleteForm`, `AthleteClubsSection` | 4 | `AthleteController::bagClubIds` | D7 |
| "this club is taped" | `PpClubPanel`, `PpSessionToolbar` | 2 | `ClubFormat` | D7 |
| tone-name → colour switch | ≈11 sites | 11 | `Theme.toneColor()` | D8 |
| updater state → text/colour | 3 | 3 | `UpdateStates.js` | D5 |
| on-screen cue words vs spoken words | `CalibrateTrunkPage`, `spoken_instructions.js` | 2 | `spoken_instructions.js` | D4 |
| slerp animation | `BodyVizView` arm and trunk | 2 | inline `SlerpTrack` | D24 |
| `Qt.rgba(X.r,X.g,X.b,a)` | 36 files | 130 | a lint rule or one replace | B24 |
| ink-on-accent | ≈23 sites | 23 | `Theme.colorOnAccent` | D14 |
| `quatMul` / `quatInv`, world→scene basis | 3-D views | 3 | `VizMath.js` | D10 |
| number words, `cap1`, `list`, `plural` | QML, `charts_words.h`, **`report_words.h`** | 3 languages-files | `charts_words.h` | C17 |
| corridor ladder | `PpSmallMultiple` + C++ | 2 | C++ publishes edges | C21 |
| number formatting (true minus, missing mark) | **C++** ×9 (+ QML ×4) | 13 | `figure_format.h` | C15 |
| figure + unit joining; signed figures | **C++** ×7 (4 rules); ×4 | 11 | `figure_format.h` | C33 |
| "at impact" wording | `charts_words.h`, `report_words.h`, `charts_traces_presenter.cpp` ×4 | 3+ | `whenImpact` | C34 |
| shot-shape classification | `lm_inferred_reads.h`, `session_delivery.cpp` | 2 | `lm_inferred_reads.h` | C32 |
| nice ticks; `shortUnit` | **C++** ×3 each | 3 | `figure_format.h` | C16 |
| `_typeName` | dead chain | 3 | dies with it | C1 |

## 20. Looks duplicated, is not — leave alone
- **Same-named local components with different jobs** (D15, A16): the three `Chip`s, two `Cell`s — do not merge or promote under those names (flat module). The two `ShotTable`s (LM board vs report appendix) have different layouts — share the column code (C31), not the tables.
- **`PpTypePill` ≠ `ModelChipRow`**; `PpTabs`, `PpInlinePicker`, `PpChReadingPicker`, `PpChChoices` stay separate (A, C22).
- **The box-plot rule holds** (C6): every new box — Posture, Delivery, report — goes through `PpChBoxCell` → `PpBoxPlot`.
- **The card family is correctly layered** (B25); `PpCard{table}` deliberately does not use `PpCardShell` (its header says why).
- **Capability logic is shared** (D21): `capability_facts.js` + `setup_capability_rows.js`, with `HmRigCapability` building a draft-less `SetupContext` — the pattern D1/D12 asked for.
- **`spoken_instructions.js`** is the one source of spoken phrases; **`HmFaultDetail`**, **`PpChTrustFold`**, **`PpChShotShapeGrid`**, **`PpReportProfile`**, **`PpRowAction`** are clean single components with several users; `report_posture/wrist/delivery.h` adapt presenter output rather than recompute it; `axisTicks` is the one tick source for the new presenters.
- **`PpcpGuidedPairDialog`** is parked on purpose (restore notes now in `HmRigCapability.qml:166`).
- **`BarButton`** (`PpSessionsSelectionBar`) is a thin `PpButton` wrapper for a disabled-reason tooltip, not a copy.

## 21. How the typeset aesthetic changes the merge plan

*(Unchanged in substance from 19 Oct; re-measured.)* The typeset-built areas (Charts, Session diagnostics, Trust) still use **2 of the now 46** `components/` files (`PpCardShell`, `PpMicro`); the new session report uses 10 alongside typeset roles on some pages. The design system (§15) makes typeset the target for every stage panel and later whole screens, and rejects pills, capsules, chip clouds, tone badges and coloured card rules.

1. **For look-bearing components the merge direction flips:** selection controls converge on `PpTabs`/`PpInlinePicker` (Home's new tabs already use `PpTabs`), status marks on `PpMark`, headings on the typeset roles / `PpSectionHead`, notes on `PpNotes`. Do not design new `PpChip`/`PpBadge`/`PpButton` variants in the old style.
2. **The best-value work is inside the typeset area** — and it has grown: the Charts *sections* (C23–C26, C24's shell) and the report pages (C27–C31) are now the code every later section and page will copy. Consolidating there is on the target path with certainty.
3. **A pure collapse of look-bearing copies is still worth doing — only the collapse.** One `PpSwitch` with today's look makes the later restyle a one-file change instead of 26.

| Item | Under the typeset direction |
|---|---|
| Look-neutral logic and structure: `PpConfirm` adoption, `SettingsPanelBase`, `SettingsRow` (structure), `CalibrationRoutineBase`, `HmFaultRow`, session-action rules, byte/plural helpers, C++ formatters and words, shot-shape classifier, D17/D18/D22 rules, `ModelTypeAhead`, `PpScopedAction`, `PpViewFlick`, `PpUpdateStatus` (logic), `clubModel`, dead-file deletion, `Qt.alpha` sweep | **Proceed as written.** |
| Inside the typeset area: `PpChSection`, `PpChPositionProfile`, `PpChPictureStrip`, `PpChFindingsLedger`, report blocks in print roles, C3–C22, panel base, `PpSectionHead`, `PpStageCard` framing, `PpBoxPlot style` | **Top priority.** |
| `PpSwitch`, `PpMenuItem`, `PpTextArea`, `PpAvatar`, `PpFactStrip`, `PpDeviceCard` shell, `SetupCapabilityRow` | **Collapse now, no variants; re-skin later.** |
| Extend `PpButton`, `PpChipGroup`/`PpChip`, `PpSegmentedControl`, `PpBadge`, `PpMicro`/`PpCardNote` variants | **Retarget or hold** (see 1). `PpLink`'s disabled state (A21) is a small fix to a component typeset pages already use — proceed. |
| Popup chrome, notice card, `Theme.toneColor()`, `PpTextField`/search field | **Hold** for a typeset answer, except the pure collapses above. |

**Gaps the typeset system has still not answered** — a switch, a button, a text/search field, a popup surface, a notice, a device row, a settings row. New screens since 19 Oct (Studio, Sessions, trunk setup) re-rolled exactly these pieces, as predicted.

---

## Dead, parked, test-only — verified file by file

| File | Lines | Finding | Status 31 Oct |
|---|---:|---|---|
| `review/MetricLibrary.qml`, `MetricDetail.qml`, `MetricRow.qml`, `NormativeBar.qml` | ≈1,316 | C1 | still dead, still in the cmake list |
| `trust/PpGateNote.qml` | 74 | C13 | still unreferenced |
| `media/FilmPage.qml` | 597 | D11 | still dead; `filmController` still built in `main.cpp` |
| `home/PpcpGuidedPairDialog.qml` | 377 | D11 | parked on purpose |
| `review/PpMetricChart` + `PpChartSummary`, `PpSegmentBrush`, `PpChartPlot` | ≈2,600 | C2 | production-reachable only as the compact plot in `PpCameraTiles`; Mark's call |
| ~~`review/PpReplayCharts.qml`, `review/PpSequenceStrip.qml`~~ | — | C1, C8 | deleted 722cf3c2 |
| ~~`session/ScreenSessionMode.qml`, `shell/ScreenPlaceholder.qml`~~ | — | B18, B20 | deleted 785f1c2f, ab1f795e |
| ~~`diagnostics/PpDiagPrint.qml`, `print/PpPdfPrinter.qml`~~ | — | overview | now reached from the app (print path) |

---

## Suggested order for fixing (31 Oct)

1. **Cheap, high-certainty collapses (S each):** `Main.qml` → `PpConfirm` (A20); `bytesWords`/`plural` → one helper each (B26/B27, after Mark picks SI or binary); `faultFor`/`HmFaultRow` (D19); `CheckTrunkPage` onto `SetupStepIntro` + shared readings box and link (D16); `PpTextArea` (A23); promote `MenuLine` (A22); promote StudioPanel's `SettingRow`/`RowTitle` (B28); `PpLink` disabled state (A21); `PpScopedAction` (A5); `fin`/`withPlane` helpers (C35); delete the dead `HmGoesTogether` helpers (D6).
2. **Fix the drifted rules (decisions first):** shot shape (C32), unit joining and "at impact" (C33/C34), trunk heading → C++ (D17), `file://` encoding (D22), capability-row colours (D21), session-action reasons (B29), updater wording (D5), Witmotion 15°/25° → C++ constants (D18).
3. **The three-way copies before a fourth arrives:** `CalibrationRoutineBase` (D3) and the status column (D4); `PpChSection` + `PpChPositionProfile` + `PpChPictureStrip` + `PpChFindingsLedger` (C23–C26) — before the next Charts section is built.
4. **Highest line count:** `SettingsPanelBase` then adopt `SettingsRow` across the panels (B1/B2, ≈1,600 lines); `PpSwitch` (A1); the monitor's in-file clean-up (D2).
5. **Report reuse:** print modes for `PpChPostureAddress`/`PpChMovesWith`, `PpChRunBox` for the work-on page, report section scaffolding and roles (C27–C31).
6. **Product decisions:** retire the old review chart stack (C2); delete the dead files (C1, D11).

## Method and honesty notes
- 31 Oct: four read-only agents, one per cluster, re-counted each original finding on HEAD and on a `git archive 8f5225a8` copy with the same script or grep, so "then → now" is like for like; where their baseline differed from the 19 Oct number they say so in their reports. They read every file added since `8f5225a8` in their cluster. Nothing was built, run or probed; line numbers are from HEAD `9ac19450`.
- Drift claims in the disagreement table are derived from code and constants (B26 arithmetic, C32 thresholds, C33/C34 string rules, D17 guards, D22 encoding); none was seen on screen. B26's two divisors were spot-checked by hand.
- Not examined (see each report's last section): interiors of `CameraDeviceRow`/`ImuDeviceRow`; sliders; `PpChPostureFrame`/picture C++ against `report_stills*`; whether `whatChangedOf` and `session_posture.cpp` compute posture readings twice; report pager logic against `PpPager`; `PpSessionsSelectionBar` vs `PpShotActionBar`; `PpDataViewer`, `PpTransitTimeline`, `PpCauseGraph`, `PpPathChains`; most of `ScreenResourceMonitor` beyond confirming no change. Absence from this page is not evidence of no duplication.
- Effort labels (S/M/L) and confidence are the agents' estimates (high unless stated).
