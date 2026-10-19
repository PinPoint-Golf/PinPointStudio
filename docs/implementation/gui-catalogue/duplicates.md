# GUI duplication — consolidated findings

Sources: four read-only review agents, one per cluster, 81 findings in all — [A interactive primitives](findings/A-interactive-primitives.md) (19), [B containers & scaffolding](findings/B-containers-scaffolding.md) (25), [C charts & data-viz](findings/C-charts-dataviz.md) (22), [D screens & flows](findings/D-screens-flows.md) (15) — plus the scripted evidence in [data/duplicate-evidence.md](data/duplicate-evidence.md). The agents overlapped heavily (the same toggle, `ScopedAction`, setting row and picker were found by two clusters each), so this page merges them into **16 themes** (§1–16), then re-cuts the same material as a **merge map** (§17: which components could become one), a **list of copied code that is not a component** (§18), and **§20, how the new typeset aesthetic changes the plan — read it before acting on §17.** Finding IDs (`A1`, `B3`…) point into the per-cluster reports, which hold the file:line locations, the proposed API and the effort/risk notes. Line numbers in those reports were read from the working tree and are approximate in places.

**Nothing here has been changed in the repo, and no finding was run in the app.** Visual-difference claims are read from code, not rendered. Anything that changes how something looks (corner radius, knob colour) needs Mark's eye before it is unified.

## Headline

- **The shared primitives exist but are not used.** `PpButton`, `PpChipGroup`, `PpTextField`, `PpComboBox`, `PpPopoverCard`, `PpCardNote`, `PpMicro` are all in `components/`, yet the code re-implements them inline: ~87 hand-rolled buttons in 31 files (A2), 21+ choice chips (A3), ~88 caps-micro labels (B4), 13 popup backgrounds (B7), 24 inline `PpCardNote` recipes (B16). Part of the reason is real: `PpButton` has no compact size or outlined-tone variant (A2), `PpChipGroup` cannot wrap (D7), and nothing shared exists for a switch, a search field, a setting row or a notice.
- **The biggest single block is Settings.** All ten `settings/*Panel.qml` carry the same ~45-line scaffold (B1, ≈450 lines) and ~61–66 hand-built "title + subtitle + control + search-highlight" rows (A7/B2, ≈1,100 lines). Settings is 12.7k lines, 14 % of all QML.
- **Roughly 2,300 lines are dead and a further ≈2,900 are only reachable as one compact plot** (theme 5 and the table at the end): the whole old `review/` chart stack is mounted in production only as a compact plot in the camera tiles, and a metric-catalogue chain has been replaced by the Diagnostic Model panel. Deleting is Mark's call.
- **Business rules are copied between QML and C++, and between QML files** (theme 8) — including one place where the copy may already be wrong (below).
- **What is *not* duplicated is also on record**: the one-box-plot rule holds (C6), the card family is correctly layered (B25), several same-named local components are deliberately different (D15, A16) — see §19.

## ⚠ One bug found: D1 — confirmed by probe, issue #50

**D1** — `_imuCalibratedOk(inst)` is copied verbatim into `imu/PpImuPanel.qml:50` and `session/PpSessionToolbar.qml:97` and tests `inst.mountDeviationDeg <= 15.0 && inst.mountGravityErrorDeg <= 25.0`. `ImuInstance` (Witmotion) has `mountDeviationDeg`; reading `imu/hm_instance.h` and `imu_device.h`, **`HmInstance` (HackMotion) declares no such property**, so for it `undefined <= 15` is `false`, and a connected, calibrated HackMotion would be reported "needs calibration" (toolbar IMU pill and panel). The C++ already has `ImuInstance::fullyCalibrated` (`imu_instance.h:96`) whose comment says not to re-derive it. `HmInstance` has no `anatCalibrated` either (only its two `HmUnit`s do), so it fails at the first test. **Confirmed 19 Oct** with a `--probe-qml` run that called the real `_imuCalibratedOk` of both files: a Witmotion-shaped object passes in range and fails at 40°; an `HmInstance`-shaped object fails even with `anatCalibrated` true. The two copies also disagree on a missing sensor (panel returns `false`, toolbar `undefined`). Not yet seen on a real wG3 (none attached; the HackMotion object in the probe was a stand-in with the properties the class declares, read from its generated metadata). Filed as GitHub issue #50.

---

## Themes, ranked by value (instances × lines recoverable)

### 1. Settings panel skeleton and setting rows — *A7, B1, B2, B8, B9, A6*
- **B1 — TRUE DUPLICATE, high confidence.** `lastHighlightId`, `findChild`, `scrollToItem`, `highlightTimer` and the same ScrollView/`contentCol` numbers appear in all 10 panels (≈45 lines × 10). `MetricLibrary` and `DiagnosticModel` stub the same contract. → a `SettingsPanelBase`. Effort M.
- **A7/B2.** The setting row (search-highlight rectangle ×60, title/subtitle column ×52) is hand-written 61–66 times; only `ArchivingPanel` extracted it (`RowTitle`). → `SettingsRow`. Effort M–L; **depends on B1**; risk: `SettingsIndex` finds rows by `objectName`.
- **B8.** The "capabilities strip" is three ~55-line copies in Phones/Cameras/Imus (+ monitor `Reading`/`Cell`). **B9/A6.** The device-card shell (surface + z:100 border overlay + 54 px status-dot header) is copied across `CameraDeviceRow` (~2,600 lines), `ImuDeviceRow`, `PhoneDeviceRow` and two monitor cards; the agents advise extracting **the shell only**, not the bodies.

### 2. One on/off switch, built 24 times — *A1, B3*
7 inline `TogglePill` (settings ×6, `PpCameraPanel`), `SetupTogglePill` (its header says it mirrors the ImusPanel copy), 2 identical-ish `MiniToggle`, plus ~15 anonymous copies in `GeneralPanel`, `DisplaysPanel`, `AppearancePanel`, `PpImuPanel`, `PpShotFilter`, `AthleteClubsSection` — ≈44 use sites, ≈580 lines. **Drift:** the knob is `"white"` in 21 copies, theme-dependent in two, and 14 px in `MiniToggle`. → promote `SetupTogglePill` to **`PpSwitch`**. Effort S–M, high confidence, no behaviour change except the knob colour decision.

### 3. Buttons, chips and tabs that bypass the shared ones — *A2, A3, A4, A9, A10, A19, B12*
- **A2:** ~87 label-sized Rectangles used as buttons in 31 files (worst: `CamerasPanel` 14, `ScreenResourceMonitor` 12, `ImusPanel` 7, `PpSessionToolbar` 7). Fix is to **extend `PpButton`** (compact size, outlined-tone variant), not add a component.
- **A3:** 21+ selectable chips in five visual dialects; `CamerasPanel` defines `TuneChip` (used 6×) yet has 4 more inline copies. → `PpChoiceChip` + a selectable-surface base (**B12**: ~12 hand-written selectable tiles).
- **A4:** 17 stock `Button`s and 5 `TabButton`s hand-styled as `PpButton` in the lab pages (`AudioPage`, `CoachPage`, `FilmPage`, `CapturePage`, `VideoPage`).
- **A9:** `PpSegmentedControl`, `ElementSegmented` (PpMotionPanel), `PpSpeedSelector` do one job; `PpUnitToggle` is a fourth. **A10:** `TBtn`, `PipBtn`, `MlButton`, `ModelBarButton` are four small icon buttons.

### 4. Charts / Diagnostics / Trust panel shells are twins — *B5, C10, C11, B17, C9, C7, C12, B15*
- **B5/C11:** the reader's-place save/restore (~90 lines in Charts, ~145 in Diagnostics) is the same algorithm; `_pickShot` is character-identical in both and in `PpLaunchMonitorPanel`; `_folder` ×4, `_pointAtSession` ×3. The comments in the Diagnostics copy record real traps, so unify carefully (effort M).
- **C10/B17:** `ViewFlick` ×3, review-foot ×2, the cast-zoom formula ×2, a 7-property view-head preamble ×5.
- **C9:** the two `KeyFigures` have an identical column-wrap line. **C7:** two run-chart implementations (`PpChRunBox`, `PpRunChart`). **C12:** ~15 copies of the value→pixel map, 5 of the y-axis gutter. **B15:** `PpChartsBody` and `PpSessionDiagnosticsBody` redraw the frame/title/aside that `PpStageCard` already draws — which is why the `framed`/`asideHosted` protocol exists in `PpModeStage`.

### 5. The old `review/` chart stack — largely unreachable — *C1, C2, C8, C4*  ⚠ Mark's call
- **C1 (high confidence): `MetricLibrary`, `MetricDetail`, `MetricRow`, `NormativeBar`** (≈1,316 lines) form a dead chain; `ScreenSettings.showMetricDetail` now goes to the Diagnostic Model panel. `PpReplayCharts` (73) is dead too — superseded by `charts/PpChartsPanel`.
- **C2:** `PpMetricChart` (1,549 lines) is mounted in production only by `cameras/PpCameraTiles` with `compact: true`; so `PpChartSummary` (703), `PpSegmentBrush` (364), `PpSequenceStrip` (297) have no production path. Seven tests/probes pin this stack, and `PpChartPlot` carries an "honesty contract". Medium-high confidence; **retiring it is a product decision**, not a mechanical clean-up.
- **C8:** `PpChSeqTable` and `PpSequenceStrip` share a pasted 55-line "Sequence checks" block (moot if C2 is actioned). **C4:** two curve painters (`PpChartPlot` Shape 1,007 lines vs `PpChPlot` Canvas 168 lines, shared by four Charts figures).

### 6. `ScreenResourceMonitor.qml` is mostly copy-paste of itself — *D2*
2,390 lines of which ≈700 are in-file duplicates: 4 identical underline tabs, 10 outline buttons, 2 filter boxes, 2 filter-chip rows, 2 memory tables, 6 empty-state rows; and five `Rm*Row` files re-declare the same zebra-row skeleton. It has **no QML test**, so the risk is "no safety net", not complexity. Effort M.

### 7. Calibration — *D3, D4*
`WitmotionArmRoutine` / `HackMotionArmRoutine` share ≈60 identical scaffold lines (pace merge, logging, begin/stop/reset/restore, guide-finished hook) → `ArmRoutineBase` (state machines stay separate). `ArmCalibrationStatus` carries two full copies of itself (13 local components; `PhaseText`/`HmPhaseText`, `StatusLabel`/`HmStatusLabel`…), and `ImuCalibrationFlow` repeats an 18-property `BodyVizView` block.

### 8. Business rules copied instead of called — *D1, D5, D6, D7, D8, C3, C21, C17*
- **D1** (above): device-health rules re-derived in four places with hard-coded 15°/25°; the "Error"/"Not found" label match is in three files.
- **D5:** updater status presentation exists three times (`PpAboutDialog`, `GeneralPanel`, `PpUpdateBanner`) and has **drifted** (wording; `unsupported` treated differently). **GeneralPanel** and `PpAboutDialog` carry the same 8-state update palette (B11).
- **D6:** Home's finding row (badge, headline, meter, chip, pips) built by hand in `HmFocus`, `HmSwingSummary`, `HmWorkOns`; `capitalised()`/`latestPips()` copied in `HmGoesTogether`.
- **D7:** the athlete's bag (`clubModel`) computed three times (`ScreenHome:60` and `PpClubPanel:52` byte-identical; `ScreenAthleteForm:77`); `PpChipGroup` cannot wrap, which is why club chips are hand-built.
- **D8:** the tone-name→colour switch written 5+ times (`ModelTable`, `ModelInspector`, `DiagnosticModel`, Rm cards, wizard) → `Theme.toneColor()`.
- **C3:** measured/mean/nearest-sample predicates exist as four QML copies of C++ `ChartMetrics::measuredAt`; comments and a test already say "keep them equal". **C21:** `PpSmallMultiple` rebuilds the corridor ladder from `bands[]` in QML although `session_diagnostics_model.cpp:100` says it lives in C++ — *not checked whether they agree for one-sided corridors*. **C17:** `word`/`countWord`/`cap1`/`list` in both QML and C++; `PpDiagRoles` regex-parses C++-built strings back into parts and composes English prose in QML, which the Charts stack forbids.

### 9. Typography: eyebrows, page heads, section heads — *B4, B6, B13, B14, B16, A16*
~88 inline caps-micro labels (built two ways: `capsFont` and `Theme.caps()`, in two font families) plus 6 local one-off components for the same role (`SectionHead`, `TuneHeading`, `SectionLabel`, `Eyebrow`, `Heading`, `HeadText`); the same eyebrow + `PpDisplayText` + description page head re-typed on ~16 screens; collapsible section headers written four ways (`PpDataViewer`, `PpMetricChart` inline `SectionHeader`); 14 typeset "title + quiet sub-line" heads that are variants of `PpSectionHead`; `PpCardNote`'s recipe typed inline 24× in 19 files. → use `PpMicro`/`PpCardNote`/`PpSectionHead`; one page-header component.

### 10. Popups, notices, cards, tiles — *B7, A12, D13, B10, B11, B25, B19, B20, B22, D9, A15*
- **B7/A12/D13:** 13 inline popup/dialog backgrounds next to 14 uses of `PpPopoverCard` (radius/border differ — unifying **changes how the Diagnostic Model and pairing popups look**); four modals repeat the same 8-line `Popup` preamble (`PpcpGuidedPairDialog` deliberately inverts the footer and must keep its own).
- **B10:** `PpToast` and `PpNotificationHost` are the same card; all 5 `PpToast` uses set `showUndo: false`. **B11:** ~12 hand-rolled notice boxes/status pills; `PpTypePill`'s comment says the design doc names a `PpPill`, but no such file exists.
- **B19:** avatar palette/hash/initials copied between `PpAthleteCard` and `PpAthleteRow` (the layouts themselves are a legitimate variant). **B20:** `PpTopoBackground` + hover/tap handler block pasted into 3 screens. **B22:** `CheckArmPage` and `ReadyPage` re-inline `SetupStepIntro`; six pages repeat a wrapper Column. **D9:** the athlete form's three ~45-line group cards (and a "header strip over a body" card in four other places). **A15:** two status-mark vocabularies (`PpBadge` vs `SetupStatusCircle` + three 6 px status dots).

### 11. Pickers and text inputs — *A11, B23, A13, A14, C22*
`ModelPicker` is a strict subset of `ModelCausePicker` (~90 shared lines); `ModelEnumField` and `ModelTable`'s enum popup re-implement `PpComboBox`; text inputs bypass `PpTextField` (four crop fields in `CamerasPanel`, two filter fields in the monitor) and there is no shared search field. `PpTabs`/`PpInlinePicker`/`PpChReadingPicker` are legitimately separate (typeset-styled, different jobs).

### 12. Colour spelling and one missing token — *B24, D14*
`Qt.rgba(X.r, X.g, X.b, a)` is used **128 times in 34 files** where `Qt.alpha(X, a)` exists — this is what the inventory's "hard-coded colours" column mostly counts, so it is **spelling drift, not a Theme bypass**. The deliberate literals are the video overlays (`"black"`, `#080a0c` in `CamerasPanel`/`PpCameraFrame`) and the athlete avatar palette. Real gaps: no `Theme.colorOnAccent` (the ink-on-accent colour is written three different ways at ~23 sites and visibly differs in light mode) and the `"white"` switch knobs.

### 13. 3-D scaffolding — *D10, A17*
`ArmVizView`, `BodyVizView`, `SwingViz3DView` repeat the Quick3D scaffold (environment, orbit controller, three-light rig, "Drag · Scroll" hint); `OrientationTab` is copied between the first two. The views also each guard against a Qt 6.11 debug-render lifetime issue, so unify with care.

### 14. Plot primitives — *C5, C6, C18, C19, C20*
`PpSparkline` pastes `PpZonePlot`'s wash-splitting code; the break-slash glyph is drawn in three places; the "not measured" stub rectangle in six files; median polyline and "distance to travel" dimension twice each; scrub-to-seek `MouseArea` ×3 (each coupled straight to `shotReplay`); `byKey`/`byId` ×4; dash-stroking loop ×3. **C6:** three box-plot *adapters* (`PpChBoxCell`, `PpBoxStrip`, `PpHistoryStrip`) repeat marks-and-style assembly and `PpPageKey` hand-draws a miniature box glyph — fix is a `style` property on `PpBoxPlot`.

### 15. C++ formatters — *C15, C16, C17*
True-minus number formatting written 6× in C++ with **two different missing markers** (`"-"` in `fig`/`spreadNumber`, an em dash in `fmtNumber`; `ChartMetrics::formatBare` uses an ASCII hyphen where `fig` uses U+2212); nice-tick generation ×3 with slightly different cutoffs; `shortUnit` ×3. Output strings are pinned by many tests, so pick the cutoffs deliberately.

### 16. Global-singleton coupling — *D12*  (a pattern, not a duplicate)
56 of 126 files in cluster D read a context-property global directly — 1,086 references. Worst: `PpMarkupPanel` 135, `Main` 109, `ScreenResourceMonitor` 89, `AudioPage` 79, `CoachPage` 60, `PpSessionToolbar` 59 (nine kinds). `setup/` (no direct global reads, enforced by lint rule W4) and the diagnosticmodel popups are the model to follow; a toolbar facade would come with D1. Full table: [cpp-seam.md](cpp-seam.md).

---

## 17. Merge map — which components could become one

> **Read §20 first.** This map treats `components/` as the shared base. The typeset aesthetic is the design target, and for look-bearing components (chips, pills, badges, buttons, notices, popups) §20 retargets or holds several rows here.

The themes above group findings by *topic*; this is the same material by *action*: for each target, what it would absorb. "Lines" are the agents' estimates of code deleted. Confidence is theirs. Nothing here has been tried.

### 17a. Near-identical copies — safe to collapse into one file each

| Target (new unless stated) | Absorbs | Evidence | Lines | Effort | Finding |
|---|---|---|---:|---|---|
| `PpSwitch` (promote `SetupTogglePill`) | 7 inline `TogglePill`, `SetupTogglePill`, 2 `MiniToggle`, ~15 anonymous switches | same shell, only the knob colour drifted | ≈580 | S–M | A1, B3 |
| `SettingsPanelBase` | the scroll/search-highlight skeleton in all 10 settings panels | `findChild`, `scrollToItem`, `highlightTimer` identical | ≈450 | M | B1 |
| `SettingsRow` | 61–66 hand-built "title + subtitle + control + highlight" rows | `ArchivingPanel.RowTitle` is already half of it | ≈1,100 | M–L (after B1) | A7, B2 |
| `ModelTypeAhead` | `ModelPicker` and `ModelCausePicker` | the first is a strict subset of the second | ≈120 | S–M | A11, B23 |
| `PpScopedAction` | `ScopedAction` in `PpCameraPanel` and `PpImuPanel` | verbatim, both hard-code `root.width` | ≈55 | S | A5, B21 |
| `PpViewFlick` (+ `PpNotes` as the one review foot) | `ViewFlick` ×3, `ReviewFoot` ×2 | `PpChartsRegister`, `PpDiagAnalyse`, `PpDiagReplay` | ≈100 | S–M | B17, C10 |
| `PpAvatar` | avatar palette, hash and initials in `PpAthleteCard` and `PpAthleteRow` | copied code; the two layouts stay separate | ≈50 | S | B19 |
| `VizOrientationMarker` | `OrientationTab` in `ArmVizView` and `BodyVizView` | copied | ≈40 | S | A17 |
| `PpTopoBackdrop` | the topo background + hover/tap block in 3 screens | pasted | ≈40 | S | B20 |
| `PpSequenceChecks` | pasted "Sequence checks" block in `PpChSeqTable` and `PpSequenceStrip` | ≈55 identical lines | ≈55 | S (moot if the old stack is retired) | C8 |
| `ArmRoutineBase` | scaffold in `WitmotionArmRoutine` and `HackMotionArmRoutine` | ≈60 identical lines; state machines stay separate | ≈60 | M | D3 |
| `PpUpdateStatus` | updater presentation in `PpAboutDialog`, `GeneralPanel`, `PpUpdateBanner` | three copies, already drifted | ≈100 | S (plus a wording decision) | D5 |
| `HmFindingRow` | the finding row in `HmFocus`, `HmSwingSummary`, `HmWorkOns` | same badge/headline/meter/chip/pips | ≈150 | M | D6 |
| `PpKeyFigureGrid` | `charts/PpChKeyFigures` and `diagnostics/PpKeyFigures` | identical column-wrap logic | ≈40 | S | C9 |
| `PpFactStrip` | capabilities strip ×3 (Phones, Cameras, Imus) + monitor `Reading`/`Cell` | ≈55 lines ×3 | ≈170 | S–M | B8 |
| `PpStagePanelBase` / `PpReaderPlace` | the shell of the Charts, Diagnostics and Trust panels | same save/restore algorithm, `_pickShot` character-identical | ≈250 | M (careful: the comments record real traps) | B5, C11 |

### 17b. Extend an existing component, then retire the copies

| Existing component | Add | Retires | Finding |
|---|---|---|---|
| `PpButton` | `size` (normal/compact/icon), outlined `tone`, `active` | ~87 hand-built buttons in 31 files; `TBtn`, `PipBtn`, `MlButton`, `ModelBarButton`; 17 stock `Button`s in the lab pages; ~10 outline buttons in the monitor | A2, A4, A10, D2 |
| `PpChipGroup` / `PpChip` | `wrap`; object options; a single-chip `PpChoiceChip` | 21+ hand-built choice chips (incl. 4 duplicates of `TuneChip` inside `CamerasPanel`); `PpTypePill` (a preset of `PpChip{tinted:false}`) | A3, A18, D7 |
| `PpSegmentedControl` | object options, compact size, `solid`/`wash`/`outline` | `ElementSegmented`, `PpSpeedSelector` (and possibly `PpUnitToggle`) | A9 |
| `PpComboBox` | nothing new — use `valueRole` | `ModelEnumField`, the enum popup in `ModelTable` | A13 |
| `PpTextField` | `compact`, `monospace`, `clearable`; a `PpSearchField` sibling | boxed inputs in `CamerasPanel` (4 crop fields), the monitor (2 filters), others | A14 |
| `PpToolPill` | badge kind/tone, chevron | `DevicePill` in `PpSessionToolbar` | A8 |
| `PpPopoverCard` | `plain`/border colour | 13 inline popup backgrounds; option-row delegates | B7, A12 |
| `PpMicro` / `PpCardNote` | `caps`, `body` | ~88 inline eyebrow labels, 6 local heading components, 24 inline note recipes | B4, B16, A16 |
| `PpSectionHead` (typeset) | `count`, `subStyle`, `fold`, `layout` | `PpChFoldHead`, the two `SectionHeader`s, `PpExpectations`/`FigHead`/`HalfHead` heads | B13, B14 |
| `PpBadge` | warn/error kinds | `SetupStatusCircle`; the three 6 px status dots | A15 |
| `PpStageCard` | (use it) | the self-drawn frames in `PpChartsBody` and `PpSessionDiagnosticsBody`, and the `framed`/`asideHosted` protocol | B15 |
| `PpCardShell` | (use it) | hand-rolled surface+hairline Rectangles | B25 |
| `PpBoxPlot` | `style` property; draw the page-key glyph with it | marks/style assembly in `PpChBoxCell`, `PpBoxStrip`, `PpHistoryStrip`; the glyph in `PpPageKey` | C6 |
| `PpLedgerTable` | lift column spec out | hand-laid column tables in Charts | C14 |
| `PpTypeset` | `byKey`/`byId`, `dashPath`, `wrapColumns` | 4 `byKey`/`byId` copies, 3 dash-loop copies | C19, C20, C9 |
| `charts/PpChPlot` | peak and σ-whisker layer kinds | `review/PpChartPlot` (1,007 lines) — only if the old stack stays | C4 |

### 17c. Merge only after a decision from Mark (look, behaviour, or product)

| Candidate | Why it needs a decision | Finding |
|---|---|---|
| `PpToast` + `PpNotificationHost` → one notice card | same card; all 5 `PpToast` uses set `showUndo: false`; touches the notification-centre contract | B10 |
| `PpChRunBox` + `PpRunChart` → `PpRunPlot` | heavily probed on both sides; a linear vs fault-forward scale | C7 |
| `ScreenSessionMode` into `ScreenWrist` (or delete it) | the dead fork has already diverged | B18, D11 |
| `PpDeviceCard` shell for the three settings device rows + two monitor cards | files are huge; extract the shell only | A6, B9 |
| `PpDialog` for four modals | the pairing dialog deliberately inverts the footer convention and must keep it | D13 |
| `PpPageHeader` (with `SetupStepIntro` as its setup variant) | same eyebrow + title + description on ~16 screens; small visual drift | B6, B22 |
| `VizScene` base for `ArmVizView`, `BodyVizView`, `SwingViz3DView` | each guards a Qt 6.11 debug-render lifetime problem | D10 |
| `Theme.toneColor()`, `Theme.colorOnAccent` | light-mode ink colour changes visibly at ~23 sites | D8, D14 |
| `PpGroupCard` for the athlete form's three cards (+4 other header-over-body cards) | consolidation across screens | D9 |
| Retire `review/PpMetricChart` and its dependants | product decision, 7 tests pin it | C2 |

## 18. Likely copied code that is not a component

Code (functions, formulas, small blocks) that exists in several places. Each has a single home it could move to. Those marked **C++** involve the C++ side and are pinned by tests.

| What is copied | Where | Copies | Single home | Finding |
|---|---|---:|---|---|
| `_imuCalibratedOk()` with 15°/25° hard-coded (see ⚠ above) | `PpImuPanel`, `PpSessionToolbar`, + 2 more health checks | 4 | `ImuManager` invokables (C++ already has `fullyCalibrated`) | D1 |
| `findChild()` / `scrollToItem()` / `highlightTimer` | every settings panel (12 files carry `scrollToItem`) | 10–12 | `SettingsPanelBase` | B1 |
| `_folder()` session-folder normaliser | `PpChartsPanel`, `PpTrustPanel`, `PpTrustSummaryFold`, +1 | 4 | one session-path function | C11 |
| `_pointAtSession`, `_screenActive` bindings | Charts, Trust, Diagnostics | 3 | `SessionMode` | B5, C11 |
| `_pickShot()` | Charts, Diagnostics, `PpLaunchMonitorPanel` | 3 | stage-panel base | B5 |
| reader's-place save/restore | `PpChartsPanel` (~90 lines), `PpSessionDiagnosticsPanel` (~145) | 2 | `PpReaderPlace` | B5 |
| measured / mean / nearest-sample predicates (`_meanOf`, `_hasMean`, …) | `PpChartPlot`, `PpChartSummary`, `PpMetricChart` + C++ `ChartMetrics::measuredAt` | 4 + C++ | a `.js` library, or one decorated mask | C3 |
| `byKey` / `byId` | `PpChSessionFigure`, `PpChTraceFigure`, `PpTypeset`, `PpDiagReplayState` | 4 | `PpTypeset` | C19 |
| value→pixel map (`vy`) and y-axis gutter | ~15 Charts figures / 5 gutters | 15 / 5 | `PpChScale`, `PpChYAxis` | C12 |
| dash-pattern stroke loop | three plot components | 3 | one helper | C20 |
| scrub-to-seek `MouseArea` | three Charts figures | 3 | `PpChScrubArea` | C18 |
| cast-zoom formula | `PpDiagAnalyse`, `PpChartsRegister`/Replay | 2 | `PpCastSurface` | C10 |
| "not measured" stub rectangle; break-slash glyph | six files; three files | 6 / 3 | `PpTypeset` helpers | C5 |
| `capitalised()`, `latestPips()` | `HmFocus`, `HmGoesTogether`, `HmSwingSummary` | 3 | `HmText.js` | D6 |
| `clubModel` (the athlete's bag) | `ScreenHome:60`, `PpClubPanel:52`, `ScreenAthleteForm:77` | 3 | `AthleteController::bagClubIds` | D7 |
| tone-name → colour switch | `ModelTable`, `ModelInspector`, `DiagnosticModel`, monitor cards, wizard | 5+ | `Theme.toneColor()` | D8 |
| updater state → text/colour palette (8 states) | `PpAboutDialog`, `GeneralPanel`, `PpUpdateBanner` | 3 | `UpdateStates.js` | D5, B11 |
| `Qt.rgba(X.r, X.g, X.b, a)` instead of `Qt.alpha(X, a)` | 34 files | 128 | a lint rule or one replace | B24 |
| ink-on-accent colour, written three ways | ≈23 sites | 23 | `Theme.colorOnAccent` | D14 |
| `quatMul` / `quatInv` and the world→scene basis constant | the three Quick3D views | 3 | `VizMath.js` | D10 |
| `word`, `countWord`, `cap1`, `list` | QML text helpers **and** C++ word helpers | 2 languages | C++ publishes the parts (C17) | C17 |
| corridor ladder / pass edges | `PpSmallMultiple` (QML) **and** `session_diagnostics_model.cpp` | 2 languages | C++ publishes the edge list | C21 |
| true-minus number formatting, with two different "missing" markers | **C++** ×6 (`fig`, `spreadNumber`, `fmtNumber` ×2, `formatBare`, `r2`) | 6 | `figure_format.h` | C15 |
| nice-tick generation; `shortUnit` | **C++** ×3 each | 3 | `figure_format.h` | C16 |
| `_typeName` | `MetricDetail`, `MetricLibrary`, `MetricRow` | 3 | dies with the dead chain | C1 |

## 19. Looks duplicated, is not — leave alone
- **Same-named local components with different jobs** (D15, A16): the three `Chip`s, `Cell`, `Tile`, `Switch` — do not merge or promote to files with those names (they would collide in the flat module). `PpLmGraphicsBody`'s 29 local components are one deliberate `PpTypeset` design.
- **`PpTypePill` ≠ `ModelChipRow`** (read-only label vs selectable chips); `PpLmGraphicsBody`'s `Chip` is typeset-themed and `SwingViz3DView`'s is a thin wrapper over `PpChip`; `PpTabs`, `PpInlinePicker`, `PpChReadingPicker` stay separate (A).
- **The box-plot rule holds** (C6): `PpBoxPlot` is the only drawer and `typeset/box_stats.h` the only statistics code; all quantiles forward to `quantile7`.
- **The card family is correctly layered** (B25): `PpCardShell` is the chassis; `PpCard`, `PpStageCard`, `PpPopoverCard` are legitimately distinct. The one gap: nothing for a tone-coloured notice (B11).
- **`PpAthleteCard` / `PpAthleteRow`** are different layouts (B19); only the avatar is shared code.
- **`PpcpGuidedPairDialog`** is parked on purpose (D11) — `ScreenHome` carries restore instructions and `ppcp_bootstrap_test.cpp` reads its source.

## 20. How the new (typeset) aesthetic changes the merge plan

**Short answer: yes, in three ways — it changes which side to merge *toward*, it moves the best-value work, and it puts some of §17 on hold.** I wrote §17 as if `components/` were the shared base. Measuring it says otherwise.

**What the evidence shows**
- The Charts, Session-diagnostics and Trust panels (the typeset-built ones) use **2 of the 37** `components/` files — `PpCardShell` and `PpMicro`. They bring their own text roles, marks (`PpMark`), tabs (`PpTabs`), pickers (`PpInlinePicker`), section heads, notes and plots. They are a second vocabulary, not a layer on the first. (Counted from the inventory; the Charts blocks receive a `roles` object, so by-type counts understate how typeset they are.)
- The design system (§15) says typeset *"is the target for every other stage panel and, later, for whole screens"*, and its rules reject what the old components are made of: pills, capsules, chip clouds, tone badges, frequency meters, a coloured card rule ("controls are quiet text"; colour only for "outside its corridor"). The stage-panel audit (playbook §7) lists "pills and segmented capsules are the default control" as a stage-wide fault, to be replaced by **one quiet selection control, `PpTabs`**.
- Everything outside Charts/Diagnostics/Trust — Settings (12.7k lines), Home, Athlete, Setup, Monitor, Shell, the session toolbar — is still the old vocabulary.

**1. The direction of merging flips for look-bearing components.** Where §17 says "extend `PpChip` / `PpChipGroup` / `PpSegmentedControl` / `PpBadge` / `PpTypePill` / `PpButton` / `PpMicro`", the typeset answer is different: selection controls converge on `PpTabs` (and `PpInlinePicker`), status marks on `PpMark`, eyebrows and headings on the typeset text roles / `PpSectionHead` / `PpStandfirst`, notes on `PpNotes`. Building a `PpChoiceChip`, new `PpBadge` kinds or a compact/outlined `PpButton` would add API to the idiom the design has already decided to leave.

**2. The best-value work moves into the typeset area.** The duplicates inside Charts/Diagnostics (C-series; B5, B13–B15, B17) are the code every later panel port will copy from. Duplication there multiplies with each port, and consolidating it is the one set of merges that is *on* the target path with certainty. §17b's rows for `PpSectionHead`, `PpBoxPlot` (`style`), `PpTypeset` helpers, `PpLedgerTable`, `PpStageCard`/`PpCardShell` adoption, and the panel-shell base (`PpStagePanelBase`/`PpReaderPlace`), plus `PpViewFlick`/`PpNotes`, `PpKeyFigureGrid`, `PpRunPlot` and `PpChPlot` as the curve painter (C4's direction is now settled), move to the front.

**3. A pure collapse is still worth doing for look-bearing copies — but only the collapse.** Merging the 24 switch copies into one `PpSwitch` with *today's* look and no new variants costs little and makes the later restyle a one-file change instead of 24. What to avoid is designing variants (sizes, tones, outlined, chip kinds) in the old style. The same holds for popups and notices.

**Re-labelled §17**

| §17 item | Under the typeset direction |
|---|---|
| Look-neutral structure and logic: `SettingsPanelBase` (scroll/highlight skeleton), `ModelTypeAhead`, `PpScopedAction`, `PpViewFlick`, `ArmRoutineBase`, `PpUpdateStatus` (logic), device-health rules (D1), `clubModel`, C++ formatters (C15–C17), dead-file deletion, `Qt.alpha` sweep | **Proceed as written.** Not affected by the aesthetic. |
| Inside the typeset area: C3–C22, B5/C11 panel base, B13/B14 `PpSectionHead`, B15 `PpStageCard` framing, B17/C10, C6 `PpBoxPlot style` | **Raise to top priority.** This is the reference implementation. |
| `PpSwitch`, `PpAvatar`, `PpFactStrip`, `HmFindingRow`, `PpTopoBackdrop`, `PpDeviceCard` shell, `SettingsRow` | **Collapse now, no variants; re-skin later.** `SettingsRow` as structure (id, title, subtitle, highlight, control slot) is fine; its look should come from typeset roles when Settings is ported. `HmFindingRow` and `PpFactStrip` use tone badges and meters that §15 removes — keep to a plain collapse. |
| Extend `PpButton`, `PpChipGroup`/`PpChip`/`PpChoiceChip`, `PpSegmentedControl`, `PpBadge`, `PpTypePill`→`PpChip`, `PpToolPill`, `PpMicro`/`PpCardNote` (caps, body variants) | **Retarget or hold.** Chips, capsules, segmented controls → `PpTabs`/`PpInlinePicker` (A3, A9, A18, B12). Badge kinds → `PpMark` (A15). Eyebrows/headings → typeset roles (B4, B6, B16). Buttons: no typeset button exists yet — design that first (§15: "controls are quiet text"). |
| Popup chrome (`PpPopoverCard plain`, `PpDialog`), notice card (`PpToast`+`PpNotificationHost`), `Theme.toneColor()` | **Hold.** No typeset answer; tone-coloured notices conflict with "colour means only outside its corridor". Decide the typeset equivalent, then merge into that. |
| `PpTextField`/search field, `PpComboBox` (A13, A14) | **Hold** for a typeset input design; do A13 (a wrapper over `PpComboBox`) only if `ModelEnumField` is needed meanwhile. |
| Retire the old review chart stack (C1, C2) | **Stronger case:** the old Charts panel (`PpReplayCharts`→`PpMetricChart`) is what the typeset Charts panel replaced on 14 Oct; the compact plot in the camera tiles is its last production use. Still Mark's call. |

**Gaps the typeset system has not answered yet** (these are where the old-vocabulary duplicates will otherwise keep reappearing): a switch, a button, a text/search field, a popup/dialog surface, a notice, a device row/card, and the settings-row layout. The design system lists "one quiet selection control" and Vector's selection style as proposed-not-done (§15.11). Until those exist, new screens will keep re-rolling the old pieces.

**Revised order**
1. ~~Probe D1~~ — done, confirmed, issue #50.
2. Consolidate inside the typeset area (the C-series and panel base) — the reference everything else is ported from.
3. Look-neutral structure and logic merges, dead-file deletion, `Qt.alpha`.
4. Pure collapses of look-bearing copies (`PpSwitch` etc.), no new variants.
5. Design the missing typeset controls (switch, button, field, notice, popup, device row), then port Settings/Home/Setup panels per the playbook, merging into those as each is ported. The old `components/` files then lose their users; re-run the catalogue to see which reach zero fan-in.

**Not verified:** which panels beyond Charts, Diagnostics and Trust have been ported since the 11–14 October audit (the inventory shows none, but the audit documents more are planned); whether Vector and other themes need different decisions (§15.11 says other themes take the default typeset block, unjudged).

---

## Dead, parked, test-only — verified file by file
| File | Lines | Finding | Status |
|---|---:|---|---|
| `review/MetricLibrary.qml`, `MetricDetail.qml`, `MetricRow.qml`, `NormativeBar.qml` | ≈1,316 | C1 | dead chain, superseded by the Diagnostic Model panel |
| `review/PpReplayCharts.qml` | 74 | C1 | dead; superseded by `charts/PpChartsPanel` |
| `session/ScreenSessionMode.qml` | 202 | B18, D11 | dead; stale fork of `ScreenWrist` (already lacks the focused-swing binding, 3-D slot, today-carousel sync). Swing/GRF/Coach in `Main.qml` use `ScreenPlaceholder` |
| `media/FilmPage.qml` | 597 | A4, D11 | dead (PlayPage no longer lists it). `filmController` / `media/film_controller.{h,cpp}` is still constructed and published in `main.cpp` |
| `trust/PpGateNote.qml` | 75 | C13 | unreferenced, though the same "words ›" link is hand-coded in ~15 places |
| `home/PpcpGuidedPairDialog.qml` | 377 | D11 | **parked on purpose**, not dead |
| `diagnostics/PpDiagPrint.qml`, `print/PpPdfPrinter.qml` | 292 / 152 | overview | reached only from tests/probes (the print path has no in-app entry) |
| review chart stack: `PpMetricChart`, `PpChartSummary`, `PpSegmentBrush`, `PpSequenceStrip` | ≈2,900 | C2 | production-reachable only as the compact plot in `PpCameraTiles`; Mark's call |

---

## Suggested order (first draft — superseded by §20's "Revised order")

1. **D1 is confirmed (issue #50)** — fix it with the shared health rule; check on a real wG3.
2. **Cheap, mechanical, low-visual-risk:** `PpSwitch` (2), `ScopedAction` → shared (A5/B21), `Qt.alpha` sweep (12), `Theme.toneColor()` + `Theme.colorOnAccent` (D8, D14), `clubModel` (D7), delete the verified-dead files (§ above, after Mark agrees).
3. **Highest line-count win:** `SettingsPanelBase` then `SettingsRow` (1) — ≈1,500 lines — and the monitor's in-file clean-up (6).
4. **Needs a visual decision from Mark first:** popup chrome (B7), knob colour, hover-fill dialects (B12), unifying updater wording (D5).
5. **Product decision:** retire the old review chart stack (5).
6. **Larger refactors with real test pinning:** Charts/Diagnostics shell (4), C++ formatters (15), corridor ladder (C21).

## Method and honesty notes
- The agents read the code; none built, ran or probed anything, and the repo was not modified. Their line numbers are from the working tree on 19 Oct and some are approximate (the reports mark these with `~`).
- Agents explicitly did **not** examine: the interiors of `CameraDeviceRow`/`ImuDeviceRow`, shot/home/calibration widgets (A); controls proper, media/capture/video page bodies, most of `PpLmGraphicsBody` and `diagnosticmodel/*` beyond B7/B23 (B); `PpChSessionFigure`/`PpChTraceFigure` bodies, `PpDataViewer`, `PpTransitTimeline`, `PpCauseGraph`, `PpPathChains`, `PpFigureRow`, the print pipeline, the Wrist Charts files and C++ presenter bodies (C). Sliders have no shared component and may be another gap (A). Absence from this page is therefore not evidence of no duplication in those areas.
- Effort labels (S/M/L) are the agents' estimates; confidence is theirs too (high unless stated).
