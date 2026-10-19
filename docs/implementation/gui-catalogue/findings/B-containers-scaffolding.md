# B. Containers, cards, notices, section headers, panel scaffolding — duplicate findings

Scope: `src/Gui/{settings,components,typeset,charts,diagnostics,review,session,monitor,home,athlete,setup,shell,trust,imu,cameras,diagnosticmodel}` as read on 19 Oct 2026 (repo being edited concurrently; line numbers are as seen). Read-only. All counts below were measured by script or grep against the working tree.

Ranked by value (instances x lines saved). B1-B4 are one family (the Settings panels) and would be done together.

---

### B1. Settings panel skeleton copy-pasted into all ten panels
- **Verdict:** TRUE DUPLICATE
- **Where:** the identical block is in
  - `settings/AppearancePanel.qml:39` (lastHighlightId), `:41` findChild, `:51` scrollToItem, `:66` highlightTimer, `:75` ScrollView/contentCol
  - `settings/ArchivingPanel.qml:45/47/57/72/154`
  - `settings/CamerasPanel.qml:2884/2886/2896/2911/2935`
  - `settings/DisplaysPanel.qml:82/84/94/109/118`
  - `settings/GeneralPanel.qml:47/49/59/74/219`
  - `settings/ImusPanel.qml:1347/1349/1359/1374/1386`
  - `settings/LaunchMonitorPanel.qml:41/43/53/68/157`
  - `settings/MicrophonesPanel.qml:58/60/70/85/241`
  - `settings/PhonesPanel.qml:636/638/648/663/672`
  - `settings/StoragePanel.qml:190/192/202/217/228`
  - Caller of the contract: `settings/ScreenSettings.qml:118` (`scrollWithRetry` → `panel.scrollToItem`). Two further files implement the same contract as stubs that just `return true`: `review/MetricLibrary.qml:58`, `diagnosticmodel/DiagnosticModel.qml:699`.
- **What is duplicated:** every panel carries `property string lastHighlightId`, a recursive `findChild(parent,name)` (10 lines, byte-identical), `scrollToItem(itemId)` (14 lines: find by objectName, mapToItem, clamp contentY to `target.y - sp(24)`, set `target.searchHighlight`, start timer), a 1800 ms `highlightTimer` (8 lines) and the same `ScrollView { contentHeight: contentCol.y + contentCol.implicitHeight + sp(28); ColumnLayout { id: contentCol; x: sp(32); y: sp(28); width: parent.width - sp(64); spacing: gap(20) } }`. The ScrollView/contentCol numbers are identical in all ten. About 31 lines of logic plus ~14 of scroll scaffolding = ~45 lines x 10 = ~450 lines, all of which could be one component. The "search-scroll contract" is currently duck-typed (ScreenSettings just calls `scrollToItem`, and two other panels fake it).
- **Shared component it should fold into:** new `settings/SettingsPanelBase.qml` (or `PpSettingsPage`): root `Item` exposing `default property alias content: contentCol.data`, `function scrollToItem(id)`, `property alias scrollView`, plus the header triplet from B6 (`eyebrow`, `title`, `description`). Panels become `SettingsPanelBase { title: qsTr("Storage"); eyebrow: qsTr("Configuration"); … rows … }`. `findChild` should be a one-line `Item.findChildByObjectName` helper (or use the existing `_findNamed` in B5).
- **Effort / risk:** M. Mechanical, but 10 files and the search-jump behaviour must be re-verified with `session_setup_ui`-style probes; CamerasPanel/ImusPanel are 3k/1.8k lines so the diff is large even though each edit is small.
- **Confidence:** high

### B2. The "setting row" chrome (search highlight + title/subtitle column) hand-written 66 times
- **Verdict:** CANDIDATE UNIFICATION
- **Where:** 66 `objectName: "setting_*"` rows (Appearance 7, Archiving 6, Cameras 2, Displays 11, General 15, Imus 4, LaunchMonitor 8, Microphones 2, Phones 1, Storage 10). Each has `property bool searchHighlight: false` + a one-line highlight `Rectangle { x: -sp(6); … color: Theme.colorAccentLight; … opacity: parent.searchHighlight … z: -1 }` (60 occurrences incl. two in `GeneralPanel.qml:100,167`). Example: `settings/ArchivingPanel.qml:178-182`, `settings/StoragePanel.qml:279-…`, `settings/GeneralPanel.qml:268-300`. The label half of the row is an inline `ColumnLayout { spacing: gap(3); Text{fontSzBody colorText} Text{fontData fontSzMicro colorText3} }` in 52 places (Appearance 6, Cameras 2, Displays 10, General 10, Imus 4, LaunchMonitor 8, Mics 2, Storage 10) — only `ArchivingPanel.qml:141` has pulled it into a component (`RowTitle`, used 7x).
- **What is duplicated:** every row is `RowLayout { objectName; Layout.fillWidth; spacing: gap(16); searchHighlight; highlightRect; <title col>; <control> }`. The chrome is ~6 lines and the title column ~14 lines when written long-hand (General/Storage/Displays) — roughly 66 x 6 + 52 x 14 = ~1,100 lines of repeated structure. The panels also drift: `RowTitle` uses `fontData` for the subtitle but some inline copies use `fontBody`; `Layout.fillWidth` on the title is present in some, missing in others.
- **Shared component it should fold into:** new `settings/SettingsRow.qml`: `RowLayout` with `property string settingId` (→ objectName `"setting_"+id`), `title`, `subtitle`, `property bool searchHighlight` + built-in highlight rect, `default property alias control: …` (the control goes at the right). Variant `SettingsBlock` (ColumnLayout) for the stacked rows (`ArchivingPanel.qml:204`, `LaunchMonitor`).
- **Effort / risk:** M-L. Needs B1 first; high-volume edits in files with search indexing (`SettingsIndex.qml` keys on these objectNames, so the objectName must stay byte-identical).
- **Confidence:** high

### B3. On/off toggle re-implemented 10 times (7 inline `TogglePill`, 2 `MiniToggle`, 1 "shared" copy)
- **Verdict:** TRUE DUPLICATE
- **Where:**
  - inline `component TogglePill`: `settings/ArchivingPanel.qml:115`, `settings/ImusPanel.qml:33`, `settings/MicrophonesPanel.qml:124`, `settings/StoragePanel.qml:125`, `settings/CamerasPanel.qml:229`, `settings/LaunchMonitorPanel.qml:90` (adds `enabledPill`), `cameras/PpCameraPanel.qml:308` (knob colour differs: `Theme.dark ? colorText : colorSurface`)
  - `MiniToggle`: `session/PpMotionPanel.qml:514`, `session/PpViewPanel.qml:317` (34x18 track, 14 px knob, different knob colours, `toggled()` with no argument)
  - promoted copy: `setup/parts/SetupTogglePill.qml` ("Ported verbatim … mirrors the TogglePill in ImusPanel.qml", used once by `setup/parts/SetupCheckRow.qml:129`)
- **What is duplicated:** a 34x18 `Rectangle` with `colorAccent`/`colorBg3`, a 12 px white knob that slides, a MouseArea emitting `toggled(bool)`. 24-27 lines each; the seven Settings/camera copies are character-identical apart from `enabledPill`. Instance count: 21 (`TogglePill {` x20 + SetupCheckRow x1) plus 6 MiniToggle. Folding saves ~200 lines and removes the three subtly different knob colours (white / dark-aware / text3), which is real visual drift between Settings and the session panels.
- **Shared component it should fold into:** promote `setup/parts/SetupTogglePill.qml` to `components/PpToggle.qml` with `checked`, `enabled` (replaces `enabledPill`) and `toggled(bool)`; the session panels' `MiniToggle` map onto it with `onToggled: …`. Choose one knob colour rule (the `Theme.dark ? …` variant is the only theme-aware one).
- **Effort / risk:** S. Pure drop-in; add the new file to `cmake/PinPointQmlModule.cmake`.
- **Confidence:** high

### B4. The caps-micro "eyebrow" label re-typed ~88 times, plus six local one-off components for it
- **Verdict:** CANDIDATE UNIFICATION
- **Where:** script count of `Text{}` blocks with `fontSzMicro` + `trackingMicro` + `capsFont`: 88 (53 `fontBody`/`colorText3`, 32 `fontData`/`colorText3`, 3 state-coloured). By file: `CamerasPanel` 17, `ImusPanel` 15, `StoragePanel` 8, `GeneralPanel` 6, `AppearancePanel` 5, `PhonesPanel` 4, `MicrophonesPanel` 4, `DisplaysPanel` 4, `LaunchMonitorPanel` 3, `diagnosticmodel/ModelInspector` 4, others 1-2. Six local components exist only to name it: `settings/ArchivingPanel.qml:133 SectionHead`, `settings/CamerasPanel.qml:218 TuneHeading`, `setup/pages/ReadyPage.qml:57 SectionLabel`, `review/MetricDetail.qml:154 Eyebrow`, `diagnosticmodel/ModelTools.qml:67 Heading`, `review/PpSequenceStrip.qml:59 HeadText` (= `PpMicro {}`).
- **What is duplicated:** one role — "section eyebrow" — written five ways: body font + `font.capitalization: Theme.capsFont` (settings), data font + `Theme.caps(qsTr(…))` in the string (athlete, setup, review: 332 `Theme.caps(` calls in the tree), and `PpMicro` (mono, no caps). `PpMicro` already is "a micro heading" but lacks the body-font/caps variant, so every settings panel re-types it. The two capitalisation mechanisms and two font families make the same-looking label diverge when the aesthetic changes.
- **Shared component it should fold into:** extend `components/PpMicro.qml` with `property bool caps` / `property bool body` (or add `PpEyebrow {}` = body font, `Theme.capsFont`, `colorText3`) and delete the six local components. Decide one of `capsFont` vs `Theme.caps()`.
- **Effort / risk:** S for the six local components, M for the 88 sites (visual diff between `fontBody` and `fontData` eyebrows must be a decision, not a side effect).
- **Confidence:** medium-high

### B5. "Reader's place" persistence, session binding and shot-pick logic duplicated across Charts, Diagnostics and Trust panels
- **Verdict:** TRUE DUPLICATE (near-identical code, same author, two dialects)
- **Where:**
  - `charts/PpChartsPanel.qml:253-343` (nav save/restore, `_findNamed`, `_flick`, `_watchFlicks`, `scrollRestore`, `scrollSave`, `flickWatch`) vs `diagnostics/PpSessionDiagnosticsPanel.qml:196-340` (same functions, same Timers, same `Instantiator` of `Connections`)
  - `_pickShot`: `charts/PpChartsPanel.qml:350`, `diagnostics/PpSessionDiagnosticsPanel.qml:397`; the same promote/enterReplay branch inline in `session/PpLaunchMonitorPanel.qml:129-139` (`_focusShot`)
  - `_folder()`: `charts/PpChartsPanel.qml:91`, `diagnostics/PpSessionDiagnosticsPanel.qml:153`, `trust/PpTrustPanel.qml:44`, `trust/PpTrustSummaryFold.qml:60`
  - `sessionDir` expression (reviewActive ? activeSessionId : activeSessionDir : liveSessionDir) + `_pointAtSession` + `_screenActive`: `charts/PpChartsPanel.qml:86-92`, `trust/PpTrustPanel.qml:39-45`, `diagnostics/PpSessionDiagnosticsPanel.qml:138-165`; `_screenActive` also in `cameras/PpCameraTiles.qml:60`, `review/PpReplayCharts.qml:38`, `review/PpDataViewer.qml:38`
  - mechanical clone report: `charts/PpChartsPanel.qml` vs `diagnostics/PpSessionDiagnosticsPanel.qml`, Jaccard 0.35 / containment 0.70, 12-, 9-, 9-, 8-line shared blocks
- **What is duplicated:** ~90 lines of nav/scroll-restore in Charts and ~145 (with comments) in Diagnostics are the same algorithm: stamp the place with the session folder, restore flick `contentY` by retrying on a 50 ms timer until content is tall enough, save on a 300 ms debounce, give up if the user moves. The only differences are the flick objectNames (`chShotFlick…` vs `sdCardsFlick…`) and what else is saved. `_pickShot` is character-identical except for whitespace (24 lines each) and a third copy of its branch is in the Launch Monitor panel. Folding saves ~200 lines and — more importantly — means a scroll-restore bug is fixed once.
- **Shared component it should fold into:** new `components/PpStagePanelBase.qml` (or `StageSession.qml` QtObject) providing `sessionDir`, `folder()`, `screenActive(sessionType)`, `pickShot(swingDir)`, plus `PpScrollMemory { target: body; names: [...]; place: …; onSave }` for the flick save/restore. `sessionDir`/`_folder` could also be one `SessionMode` singleton function.
- **Effort / risk:** M. The comments in the Diagnostics panel record real traps (restore ordering, `_navReady`, save on teardown); keep the behaviour and move the comments with the code. Probe with the existing `diag_nav_survives_mode_switch.qml`.
- **Confidence:** high

### B6. Page header (eyebrow + display title + description) re-typed on ~16 screens
- **Verdict:** CANDIDATE UNIFICATION
- **Where:** the 10 settings panels (`AppearancePanel.qml:90-110`, `ArchivingPanel.qml:167-175`, `DisplaysPanel.qml:134-150`, `GeneralPanel.qml:235-255`, `LaunchMonitorPanel.qml:173-190`, plus Cameras `:2958`, Imus `:1409`, Mics `:263`, Phones `:694`, Storage `:252`), `athlete/ScreenAthletePicker.qml:60-70`, `athlete/ScreenAthleteForm.qml:192-215`, `review/MetricLibrary.qml:121-139`, `review/MetricDetail.qml:228` (title only), `setup/parts/SetupStepIntro.qml` (the setup flavour), `setup/pages/CheckArmPage.qml:62-90` and `setup/pages/ReadyPage.qml:69-104` (inline re-implementations of `SetupStepIntro`, see B22).
- **What is duplicated:** `PpDisplayText` is used as a one-line component at 22 call sites, always preceded by an eyebrow `Text` and usually followed by a description `Text { fontSzBody2 colorText3 WordWrap }`. The eyebrow differs per file (settings: `fontBody` + `capsFont`; athlete/setup: `fontData` + `Theme.caps()`; Appearance/General/Displays say "Configuration", Archiving says "Library", Launch Monitor "Devices"), and the bottom padding after it is 10 px in one place and a spacer `Item` in another. ~15-20 lines each.
- **Shared component it should fold into:** new `components/PpPageHeader.qml { eyebrow; title; description }`; `SettingsPanelBase` (B1) hosts one; `SetupStepIntro` already is that component plus a hairline and could be its setup variant.
- **Effort / risk:** S-M.
- **Confidence:** medium-high

### B7. Popup/dialog backgrounds rebuilt inline 13 times next to `PpPopoverCard`
- **Verdict:** CANDIDATE UNIFICATION
- **Where:** inline `background: Rectangle { color: Theme.colorSurface; radius; border.width: 1; border.color: … }` at `diagnosticmodel/ModelCausePicker.qml:78`, `ModelConfirm.qml:69`, `ModelMint.qml:58`, `ModelPicker.qml:50`, `ModelPolicyPicker.qml:44`, `ModelTable.qml:511`, `ModelTools.qml:60`, `ModelUnsaved.qml:49`, `ModelEnumField.qml:102`, `home/PpcpPairDialog.qml:148`, `home/PpcpGuidedPairDialog.qml:108`, `shell/PpAboutDialog.qml:64`, `shell/Main.qml:188`. Versus `background: PpPopoverCard {}` at 14 sites in 8 files (`review/PpDataViewer.qml:624`, `session/PpSessionToolbar.qml:359/503/721/734/747/762/780`, `shot/PpShotCarousel.qml:527/566/598`, `shot/PpExportOptionsSheet.qml:76`, `session/PpSessionDrawer.qml:291`, `shot/PpShotActionBar.qml:297`).
- **What is duplicated:** the same floating surface in two vocabularies. 9 of the 13 use `Theme.radius` + `colorBorderStrong`; 3 use `radiusLg` + `colorBorderStrong`; two others tint the border (`colorAccent`, `colorAttention`). `PpPopoverCard` (=`PpCardShell { floating: true }`) draws `radiusLg`, the hairline and the tone rule, so the Diagnostic Model and pairing dialogs look like a different design system from the session popups.
- **Shared component it should fold into:** `PpPopoverCard` (add `property color border` or use `tone` for the attention/accent borders). `PpCardShell` already supports `tone`.
- **Effort / risk:** S per file; visible change (corner radius and top rule) so it needs Mark's judgement via before/after sheets.
- **Confidence:** high that it is duplication; medium on whether the radius difference in `diagnosticmodel/` is deliberate.

### B8. "Key over value" reading cell and the capabilities strip re-implemented in 5 places
- **Verdict:** TRUE DUPLICATE (3 settings strips) + PARALLEL IMPLEMENTATION (2 monitor cells)
- **Where:** `settings/PhonesPanel.qml:547-615`, `settings/CamerasPanel.qml:1680-1735`, `settings/ImusPanel.qml:620-675` (three ~55-line strips), `monitor/RmPpcpPhoneCard.qml:118` (`component Reading`), `monitor/RmPpcpCameraRow.qml:45` (`component Cell`). Mechanical clones: `CamerasPanel/ImusPanel/PhonesPanel` 11-, 8-, 8-line shared blocks; `CamerasPanel/PhonesPanel` 9+9-line blocks.
- **What is duplicated:** each strip is a `RowLayout { Repeater { model: [{key,val,…}] delegate: Rectangle { colorBg2; separator; ColumnLayout{ micro caps key; fontBody2 value (elide) } } } }`. The three Settings copies differ only in the value colour rule (Imus: accent / battery bands; Phones: `tint: "error"|"warn"`; Cameras: none) and the separator `visible` test (`index < 4`, `< 5`, `< count - 1`, hard-coded to the model length). `Reading`/`Cell` are the same label-over-value pair with `sp(8)` type, 96 px vs 48 px wide, and differ from the Settings ones in font size (`sp(8)` hard-coded vs `fontSzMicro`) — drift from the type scale.
- **Shared component it should fold into:** new `components/PpFactStrip.qml { model: [{key, val, tint}] }` (tint ∈ "", "good", "warn", "error", "accent"); `PpFact` is the *label-beside-value* variant and is used once (`home/HmWorkOns.qml`), so it is not a fit for the stacked cell. `RmPpcp*` could use the same cell with `compact: true`.
- **Effort / risk:** S-M.
- **Confidence:** high

### B9. Device-card chrome (surface + z:100 border overlay + status-dot header) copied across settings and monitor
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `settings/PhonesPanel.qml:224-250 + headerRow:254`, `settings/ImusPanel.qml:141-170 + :166`, `settings/CamerasPanel.qml:330-358 + :354`; `monitor/RmDeviceCard.qml:37-141` and `monitor/RmPpcpPhoneCard.qml:139-232` (header strip: `colorBg2` band, hairline, "bottom corners square" overlay, status dot, name + subtitle, status pill — ~95 lines each; the 8-line header clones are visible in the clone report).
- **What is duplicated:** Settings device rows draw "Background fill" then a transparent "Border overlay — z:100 so content never occludes it" then a header row with a status dot (3 near-identical colour rules: connected→good, revoked→text3, available→accent/warn). The Resource Monitor cards redo it with a different header strip. `PpCardShell` would give the surface + hairline for free (standard shell, `tone` = connection colour, `floating: false`).
- **Shared component it should fold into:** `PpCardShell` for the chrome (as `HmFocus`/`PpCard` do) and a new `PpDeviceHeader { name; subtitle; statusTone; statusText }` used by the 5 call sites. A common `statusTone(status)` lookup in `Theme` would also remove the three copies of the colour switch.
- **Effort / risk:** M. The three Settings rows are huge (`CamerasPanel`'s row animates its implicitHeight), so only the chrome should move.
- **Confidence:** medium

### B10. Toast / notification card: `PpToast` and `PpNotificationHost` are the same card, and `PpToast` is only ever used as a plain notice
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `components/PpToast.qml` (155 lines) vs the delegate in `components/PpNotificationHost.qml:75-150`. `PpToast` hosts: `settings/CamerasPanel.qml:2923`, `monitor/ScreenResourceMonitor.qml:2381`, `review/PpDataViewer.qml:630`, `shot/PpShotCarousel.qml:718`, `diagnosticmodel/DiagnosticModel.qml:1940`. Global host mounted once in `shell/Main.qml:822` (`notifications`).
- **What is duplicated:** both draw `radiusLg` + `colorBg3` + 1 px severity-tinted border, a glyph in `fontSymbol`, a body line, an 18 px hairline then an accent-caps action (`Undo` vs `actionLabel`). Every one of the five `PpToast` instances sets `showUndo: false`, so the "snackbar with UNDO" half of `PpToast` is unused; the only distinguishing feature is `copyText` (copy-path button, used by the export toasts). The five hosts also place it three different ways (`anchors.bottom` 24 / 44, `y: -height - 14`, `y: parent.height - height - 16`) and the Cameras one needs `z: 1000`.
- **Shared component it should fold into:** one `PpNoticeCard` (visual) used by `PpNotificationHost` and by `PpToast`, or route the five hosts through the `notifications` centre (needs an "action = copy to clipboard" kind). Pick one placement rule in the host so the five anchor blocks go.
- **Effort / risk:** M. Touches the NotificationCenter C++ contract if the toasts are routed.
- **Confidence:** medium-high

### B11. Tone-coloured notice boxes and status pills hand-rolled instead of one shared notice / pill
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where (notice boxes):** `monitor/RmWarningNotice.qml` (1 use, `home/ScreenHome.qml:771`), `settings/StoragePanel.qml:878-896` (raw-frames warning strip), `setup/pages/ReadyPage.qml:321-343` (ready/not-ready notice), `diagnosticmodel/ModelGraph.qml:1895-1920` and `:1930` (two drag-reason bubbles), `diagnosticmodel/DiagnosticModel.qml:852-880` (validation chip), `shell/PpUpdateBanner.qml:57` (update card).
  **Where (status pills):** `monitor/RmDeviceCard.qml:103-141`, `monitor/RmPpcpPhoneCard.qml:205-232`, `settings/GeneralPanel.qml:880-935` (update-state badge) and `:1040` ("Enabled"), `shell/PpAboutDialog.qml:49-62` (the same 8-state palette switch, "mirrors GeneralPanel.qml's version row" and re-states every case; 10-line clone), `settings/DisplaysPanel.qml:188`.
- **What is duplicated:** `Rectangle { color: Theme.colorXLight; border.color: colorX (or Qt.rgba(colorX…, 0.3-0.4)); radius: Theme.radius; Text { color: colorX } }` with the text sized by hand, at least 12 times, with three different border-alpha conventions (full, 0.3, 0.35/0.4). `PpChip` already is a tone-tinted pill (`Qt.alpha(tone, …)` fill, tone text) and is used 16 times, but none of these use it. `PpTypePill`'s own header comment notes that the design system names a status pill `PpPill` (§7.8 of `docs/design/pinpoint_qml_design_system.md`); the repo has no file of that name, so each status pill is inlined.
- **Shared component it should fold into:** the status case → `PpChip` with `tone` (extend with `filled: bool` for the `colorXLight` fill) or the documented `PpPill`; the paragraph case → a new `PpNotice { tone: "warn"|"good"|"error"; title; text }` used by RmWarningNotice, StoragePanel, ReadyPage. The update-state palette (`pal`) should be a single `UpdateStatus` helper used by GeneralPanel and PpAboutDialog.
- **Effort / risk:** M (8-12 sites, small each).
- **Confidence:** medium-high

### B12. Selectable-tile chrome (accent wash when selected, `accentMid` hover border) hand-written ~12 times
- **Verdict:** CANDIDATE UNIFICATION
- **Where:** `session/PpViewPanel.qml:217` (`ArrangeCard`) and `:271` (`OrientCard`) — the two are the same 36-line component with a different glyph and click action (byte-identical colour/border/Behaviors/comment); `settings/CamerasPanel.qml:181` (`TuneChip`, 6 uses); `settings/MicrophonesPanel.qml:155` (`DeviceRow`); `home/HmTypeCard.qml:70-82`; `athlete/PpAthleteCard.qml:30-40` (the same `Qt.tint(colorSurface, colorAccentLight)` + "no bright-then-dim flash" comment as `HmTypeCard`); `shot/PpShotCard`, `setup/pages/GoalsPage`, `settings/AppearancePanel.qml` (theme cards), `settings/StoragePanel/ImusPanel/GeneralPanel` chips. Script: 15 files combine `colorAccentMid` with a selected `colorAccentLight` background.
- **What is duplicated:** `color: selected ? accentLight : hover ? (bg2 or alpha tint) : transparent; border.color: selected ? accent : hover ? accentMid : borderMid/Strong; Behavior on color; Behavior on border.color; PpPressable`. Three different hover fills are in use (`Qt.rgba(bg2, 0)`, `Qt.rgba(accentLight, 0.4)`, `Qt.tint(surface, accentLight)`), so identical-looking tiles animate differently. The `Qt.rgba(Theme.colorBg2.r, g, b, 0)` idiom is a workaround to avoid transparent-black interpolation; it appears 27 times in 14 files (10 in `ScreenResourceMonitor.qml` alone).
- **Shared component it should fold into:** new `components/PpChoiceTile.qml { selected; hovered; radius; default content }` (surface + border + animation) with `PpPressable` inside; `ArrangeCard`/`OrientCard`/`TuneChip`/`DeviceRow`/athlete+type cards become thin content. `PpChip`/`PpChipGroup` remain for pills.
- **Effort / risk:** M. Several tiles intentionally differ (HmTypeCard lifts and scales); only the colour/border logic should be shared.
- **Confidence:** medium

### B13. Collapsible section header written four ways
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `review/PpDataViewer.qml:73` (`SectionHeader`, 17 lines, glyph swap ▸/▾, hover text colour, 3 uses) and `review/PpMetricChart.qml:937` (`SectionHeader`, 28 lines, rotated caret, hover wash, trailing hairline, 3 uses); `review/PpSequenceStrip.qml:90-98` (caret + `HeadText` + verdict row, rotation); `charts/PpChFoldHead.qml` (typeset-roles version; 59 lines); `trust/PpTrustSummaryFold.qml:80`, `trust/PpTrustReading.qml:53`, `settings/CamerasPanel.qml:1066,1353`, `monitor/RmAnalysisRunRow.qml:92` (inline caret toggles).
- **What is duplicated:** "caret + micro title, click to toggle, optionally ending in a hairline". `PpMetricChart`'s comment says outright "same idiom as PpDataViewer's SectionHeader". The two inline components even share a name. The caret is drawn two ways (swap glyph vs `rotation: collapsed ? 0 : 90` with a reduce-motion-aware `Behavior`), and hover feedback differs (text colour vs background wash).
- **Shared component it should fold into:** `components/PpFoldHead.qml { title; collapsed; rule: bool; toggled() }` with the rotating caret and `Theme.reduceMotion`; `PpChFoldHead` is the typeset-roles skin of the same thing (B14).
- **Effort / risk:** S. Two files, six call sites, plus optional adoption in the other five.
- **Confidence:** high

### B14. Typeset "title + quiet sub-line" heads: 8+ variants, each `Flow { Text(fSection) Text(fSmall ink3) }`
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `typeset/PpSectionHead.qml:40-52` (number, title, sub, optional rule; 6 uses), `charts/PpChFoldHead.qml:44-56` (same Flow + caret + folded `state_`), `diagnostics/PpExpectations.qml:44-53`, `diagnostics/PpIndexList.qml:66-82` (title + count + legend), `charts/PpChWristLedger.qml:44-49` (title + count), `session/PpLmGraphicsBody.qml:803-818` (`FigHead`: number word + title + sub, with figure rule), `:1220-1231` (`HalfHead`: title + sub on a baseline, underline rule), `:1511` (`LedgerHead`), `diagnostics/PpDiagReplay.qml:162` (`Head`: title + sub, wraps under when it does not fit beside), `diagnostics/PpDiagAnalyse.qml:272` (`Crosshead`), `diagnostics/PpLedgerTable.qml:286`, `trust/PpTrustPanel.qml:148,169`, `diagnostics/PpCauseGraph.qml:205`, `trust/PpTrustInline.qml:46`. Totals: 14 sites using `roles.fSection`.
- **What is duplicated:** every one is "section-font title, optional number/count in `fFigS`, sub-line in `fSmall`/`ink3`, vertically aligned to the title, optionally a hairline `roles.rule` above or below" — `PpSectionHead` has `number`, `title`, `sub`, `rule`, `keepNext`/`atom` for the pager. Each variant re-derives the `height: st.height; verticalAlignment: AlignVCenter` trick for aligning the small text to the title. `Head`'s "sub beside title if it fits, else under" is the only real behaviour difference.
- **Shared component it should fold into:** `typeset/PpSectionHead` with optional `count`, `subStyle` (italic for PpExpectations), `fold` (absorbing `PpChFoldHead`), `layout: "flow"|"stack"` (for `Head`), and `underline`. `FigHead`/`HalfHead` should take a `PpTypeset` (they take `h.ts` already).
- **Effort / risk:** M. The register code relies on `atom`/`keepNext` for pagination, which must survive.
- **Confidence:** medium

### B15. Three panel bodies draw their own "framed card" that `PpStageCard` already draws
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `charts/PpChartsBody.qml:282-312` (`framed`, `_drawFrame`, `PpCardShell`, title `PpMicro`, aside with ▾ and a MouseArea), `diagnostics/PpSessionDiagnosticsBody.qml:254-330` (same, with a three-chip aside), `trust/PpTrustPanel.qml:37` (`property bool framed`); host side `session/PpModeStage.qml:31,120-121` (`_unframe()` sets `framed = false` and `asideHosted = true` on whatever loads) and `session/PpModeStage.qml:126` (`PanelCard: PpStageCard`). Shell: `components/PpStageCard.qml` (title row + aside + ▾ + `asideActive`/`asideClicked`).
- **What is duplicated:** `PpStageCard` was built to own the heading row (title, aside, clickable aside with ▾); `PpChartsBody` re-draws exactly that (frameHead at x/y = 14 px, aside `PpMicro` with `trackingData`, hover-to-`colorText`, MouseArea margins −4) for the case where the body is shown outside the stage (the cast / probes), and `PpSessionDiagnosticsBody` does it again with a different aside. The protocol `framed`/`asideHosted`/`cardAside` that `PpModeStage` pokes into loaded panels is the cost of having both. The comment at `PpModeStage.qml:31` says as much ("a panel also used outside the stage carries `property bool framed`").
- **Shared component it should fold into:** make the standalone case wrap the body in `PpStageCard` instead of drawing a second frame (`PpStageCard { title; aside; PpChartsBody { framed: false } }`), then delete `framed`, `_drawFrame`, `_framePad`, `frameHead` from both bodies and `_unframe()` from the stage.
- **Effort / risk:** M. Both register layouts offset their first child by `_framePad` and the cast passes `surface: "cast"`; probes (`typeset_*`, `charts_*`) render the standalone form.
- **Confidence:** medium-high

### B16. `PpCardNote`'s recipe re-typed inline 24 times
- **Verdict:** CANDIDATE UNIFICATION
- **Where:** script: 24 `Text{}` blocks with `fontBody`, `fontSzBody2`, `fontBodyWeight`, `colorText3`, `wrapMode: WordWrap`, in 19 files — `settings/PhonesPanel.qml` 3, `GeneralPanel` 2, `ImusPanel` 2, `home/HmSwingSummary.qml` 2, and one each in Archiving (`:169`), Appearance (`:103`), Cameras, Displays, LaunchMonitor, Microphones, Storage, `home/ScreenHome.qml`. `components/PpCardNote.qml` (31 lines, 19 uses) is exactly that Text (+ `lineHeight: 1.3`).
- **What is duplicated:** 7-line Text block, usually used as the page's description line under the header (B6). The name "CardNote" ("A card with nothing in it says so in one quiet line") hides that it is the app's generic quiet body role.
- **Shared component it should fold into:** use `PpCardNote` (or rename/alias to `PpQuietText`) for these 24; the description line becomes `PpPageHeader.description` (B6).
- **Effort / risk:** S.
- **Confidence:** medium (lineHeight differs: 1.3 vs unset/1.5 in the inline copies)

### B17. Per-view `ViewFlick` and review-foot components duplicated across the three registers
- **Verdict:** TRUE DUPLICATE (ViewFlick) + PARALLEL IMPLEMENTATION (ReviewFoot)
- **Where:** `charts/PpChartsRegister.qml:107`, `diagnostics/PpDiagAnalyse.qml:255`, `diagnostics/PpDiagReplay.qml:185` (`ViewFlick`, ~14 lines each); `diagnostics/PpDiagAnalyse.qml:285` (`ReviewFoot: PpNotes`) vs `diagnostics/PpDiagReplay.qml:176` (`ReviewFoot: Text`); `Head` `PpDiagReplay.qml:162` vs `Crosshead` `PpDiagAnalyse.qml:272`. Mechanical clones: Analyse vs Replay 12-, 10-, 9-line blocks.
- **What is duplicated:** `Flickable { key; view: Component; on: root.shown === key; built-on-first-show; visible: on; anchors{top: under.bottom…}; contentWidth; contentHeight: loader.height + u(16..24); clip; StopAtBounds; ScrollBar.vertical AsNeeded; Loader { active: built; width: width - u(8) } }`. Differences: the `root.shown`/`root.view` property name, the anchor targets, the topMargin (`u(10)`, `u(12)`, `u(2)`), `interactive` rule and an exported `content` alias. `ReviewFoot` computes the same visibility predicate (`!!header && reviewNote != "" && (!ro.shows("reviewNote", width) || ss.missShown)`) twice with different base types.
- **Shared component it should fold into:** `typeset/PpViewFlick.qml { key; shown; view; top; margin; interactive }` used by all three; `PpNotes` as the one `ReviewFoot`.
- **Effort / risk:** S-M. Named-object lookups (`sdShotCardsFlick`, `chShotFlick`) must keep their objectNames for B5.
- **Confidence:** high

### B18. `session/ScreenSessionMode.qml` is an unreferenced stale fork of `session/ScreenWrist.qml`
- **Verdict:** TRUE DUPLICATE (dead copy)
- **Where:** `session/ScreenSessionMode.qml` (202 lines; `used_by: []`; listed in `cmake/PinPointQmlModule.cmake:176`; only mentioned in comments) vs `session/ScreenWrist.qml` (239 lines; instantiated in `shell/Main.qml:703`). Jaccard 0.45 / containment 0.65; clone blocks of 21, 14, 8 lines.
- **What is duplicated:** `ScreenWrist.qml`'s header says "Kept in lockstep with ScreenSessionMode"; a diff shows they already differ: ScreenWrist has `_focusedSwingDir`, the app-wide `currentSwing` Binding, `_syncTodayCarousel`, the 3-D swing slot + `SwingViz3DHost`, and `defaultPreset: "Wrist & forearm"`; ScreenSessionMode has none of them and still carries the `PpTopoBackground` hover/tap block and a `sessionType` property. `Main.qml:702-705` shows Swing/GRF/Coach are `ScreenPlaceholder`s, so the generic screen has no instantiation.
- **Shared component it should fold into:** delete `ScreenSessionMode.qml` (and its cmake line) or turn `ScreenWrist` into the generic screen with `sessionType`/`defaultPreset`/`metricKeys` properties and have `Main.qml` instantiate it per type. Either way remove the "lockstep" comment.
- **Effort / risk:** S to delete; M to generalise.
- **Confidence:** high (that it is dead and drifted); the delete/generalise choice is Mark's.

### B19. Avatar palette, hash and initials circle copied between athlete card and row (and the last-seen formatter drifted)
- **Verdict:** TRUE DUPLICATE (avatar) + LEGITIMATE VARIANT (the layouts)
- **Where:** `athlete/PpAthleteCard.qml:42-52,60-75` and `athlete/PpAthleteRow.qml:41-51,63-78` (avatarColors, avatarColor by `uuid.charCodeAt(0)`, 28 px circle with initials, `Qt.darker` border/text; 9-line clone); last-seen text `PpAthleteCard.qml:104-111` vs `PpAthleteRow.qml:131-139`; handedness `(handedness || "R")[0] + "H"` in the card, `|| "Right"` in the row. Jaccard 0.46 / containment 0.69.
- **What is duplicated:** ~25 lines of avatar + two formatters each. The card (grid tile, 116 px, hover wash) and the row (list line, 44 px, zebra) are legitimately different layouts, so they stay two files. The 6 hard-coded avatar hex colours (`#A8C4E0`, `#7EBFAA`, …) are deliberate identity colours, but they exist twice.
- **Shared component it should fold into:** new `components/PpAvatar.qml { uuid; initials; size }`; `Theme.formatLastSeen(ts, short)` next to `Theme.formatHandicap`.
- **Effort / risk:** S.
- **Confidence:** high

### B20. Screen background topo + hover/tap handlers pasted into 3 screens
- **Verdict:** TRUE DUPLICATE
- **Where:** `home/ScreenHome.qml:114-150`, `shell/ScreenPlaceholder.qml:34-62`, `session/ScreenSessionMode.qml:57-85` (the last is dead, B18). 10-line clone `HoverHandler { id: topoHover … }` across all three; the `PpTopoBackground` header comment contains the snippet "drive it from passive handlers at the SCREEN ROOT instead" and each screen copies it.
- **What is duplicated:** `PpTopoBackground { colorLow/Mid/High from Theme.gradient*; accentColor; animated: !reduceMotion; hoverPoint: topoHover.hovered ? Qt.point(…) : Qt.point(-1,-1) } + HoverHandler + TapHandler → topo.ripple(...)` ~28 lines each.
- **Shared component it should fold into:** `components/PpTopoBackdrop.qml` (PpTopoBackground + its own handlers, `enabled` flag) so a screen writes `PpTopoBackdrop { anchors.fill: parent; z: -1 }`.
- **Effort / risk:** S. The only wrinkle is `ScreenPlaceholder`'s `ambientBackground` gate and `ScreenHome`'s `#FFFFFF` use.
- **Confidence:** high

### B21. `ScopedAction` duplicated verbatim between the IMU and camera toolbar panels
- **Verdict:** TRUE DUPLICATE
- **Where:** `imu/PpImuPanel.qml:238` and `cameras/PpCameraPanel.qml:278` (about 35 lines each; differ only in line-wrapping; used 6 times between them: `PpCameraPanel.qml:210,211,217`). `PpCameraPanel.qml:308` also carries the `TogglePill` from B3 and `ImuRow` / `CamRow` follow the same shape.
- **What is duplicated:** the "scoped action" button (glyph + label, `primary` = attention tone, `connecting` state with `PpConnectingFrame`).
- **Shared component it should fold into:** `components/PpScopedAction.qml` (or `PpPanelAction`).
- **Effort / risk:** S.
- **Confidence:** high

### B22. Setup pages bypass `SetupStepIntro` and re-state the page scaffold
- **Verdict:** TRUE DUPLICATE (2 inline re-implementations) + CANDIDATE UNIFICATION (the Column wrapper)
- **Where:** `setup/parts/SetupStepIntro.qml` (eyebrow + heading + body + hairline) is used by BallPage, CamerasPage, FramingPage, GoalsPage, ImusPage, TriangulatePage; `setup/pages/CheckArmPage.qml:62-90` and `setup/pages/ReadyPage.qml:69-104` write the same eyebrow/heading/body by hand (clone report: `CheckArmPage` vs `SetupStepIntro` containment 0.63; `ReadyPage` vs `SetupStepIntro` 0.70). `setup/pages/ReadyPage.qml:57` also declares `SectionLabel` (B4). Six pages start with the identical `Column { id; anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.gap(32) }; spacing: Theme.gap(16) }`: `BallPage:55`, `CamerasPage:72`, `FramingPage:163`, `GoalsPage:42`, `ImusPage:117`, `TriangulatePage:48`.
- **What is duplicated:** the two hand-written intros omit the hairline (and `ReadyPage` swaps the heading colour to `colorWarn`, which `SetupStepIntro` has no property for); the six Column wrappers are 3 lines each.
- **Shared component it should fold into:** `SetupStepIntro` with an optional `headingColor` and `rule: bool`; `WizardPage` gets a `default property alias content` + `readingColumn` so the pages stop restating the 32 px anchor block.
- **Effort / risk:** S.
- **Confidence:** high

### B23. Typeahead popup duplicated: `ModelPicker` is a subset of `ModelCausePicker`
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `diagnosticmodel/ModelPicker.qml` (169 lines) vs `diagnosticmodel/ModelCausePicker.qml` (342 lines): Jaccard 0.35 / containment 0.78; `ModelPolicyPicker.qml` (118 lines) vs `ModelTools.qml` 0.31/0.64.
- **What is duplicated:** both are `Popup { candidateSource(searchText) → rows; title eyebrow; PpTextField with Down/Up/Enter; ListView; _take() }` with the same `closePolicy`, `padding: 0`, 360/380 px width, filter-in-C++ comment and background (B7). `ModelCausePicker` adds a "create new" row and a type selector. This is the diagnostic-model area, so it likely belongs to another cluster's report; included because it is popup scaffolding.
- **Shared component it should fold into:** `ModelPicker` with `allowCreate`/`createTypes`, replacing `ModelCausePicker` (or `ModelCausePicker` extends `ModelPicker`).
- **Effort / risk:** M.
- **Confidence:** medium

### B24. `Qt.rgba(X.r, X.g, X.b, a)` is used 128 times where `Qt.alpha(X, a)` exists
- **Verdict:** CANDIDATE UNIFICATION (style drift; not a hard-colour bypass)
- **Where:** script count in 34 files: `monitor/ScreenResourceMonitor.qml` 22, `cameras/PpCameraFrame.qml` 14, `settings/CamerasPanel.qml` 13, `monitor/RmAnalysisRunRow.qml` 9, `settings/ImusPanel.qml` 7, `review/NormativeBar.qml` 7, `media/FilmPage.qml` 6, `media/AudioPage.qml` 6, `media/CoachPage.qml` 6, `settings/MicrophonesPanel.qml` 4, … versus `Qt.alpha(` 92 times in 34 files (the newer `components/`, `home/`, `typeset/`, `charts/`).
- **What is duplicated / why it matters:** the inventory's `n_hard_colors` for these files is almost entirely this idiom, not literal colours: they are theme-aware, so they are drift in spelling, not a Theme bypass. Of the 22 in `ScreenResourceMonitor`, 10 are `Qt.rgba(Theme.colorBg2.r, …, 0)` ("transparent but same hue", to avoid animating through black; 27 sites in 14 files) and the rest are `Qt.rgba(Theme.colorAccent…, 0.15/0.4)`-style tints. `Qt.alpha(Theme.colorBg2, 0)` is shorter and identical. Real literal colours: `settings/CamerasPanel.qml` (`"black"` x8, `"#080a0c"`, `Qt.rgba(1,1,1,0.3)`) and `cameras/PpCameraFrame.qml` are video surfaces and overlays (deliberate); `athlete/PpAthlete{Card,Row}.qml` avatar hex palette is deliberate identity colour (B19); `home/ScreenHome.qml` two `#FFFFFF`, and `"white"` knobs in `ImusPanel`/`MicrophonesPanel` (B3) are the only ones worth moving to Theme. `theme/Theme.qml` itself holds 651 literals as tokens.
- **Shared component it should fold into:** n/a — a lint rule ("Qt.rgba(X.r, X.g, X.b, a) → Qt.alpha(X, a)") or one search-and-replace.
- **Effort / risk:** S (mechanical); no visual change.
- **Confidence:** high

### B25. Card family: what is shared and what is not (reference finding)
- **Verdict:** LEGITIMATE VARIANT (with one gap)
- **Where:** `components/PpCardShell.qml` (surface, hairline, tone rule; hero/floating) is the one chassis, and is used by `PpCard`, `PpStageCard`, `PpPopoverCard`, `charts/PpChartsBody`, `diagnostics/PpSessionDiagnosticsBody`, `home/HmFocus`, `home/HmWorkOns`. `PpCard` has only two users (`home/HmGoesTogether.qml`, `home/HmSwingSummary.qml`; both also declare `readonly property int cardPad: Theme.gap(20)`, a 9-line clone — `PpCard.pad` default already is `Theme.gap(20)`). `PpStageCard` has one user (`session/PpModeStage.qml:126 PanelCard`).
- **What is duplicated:** nothing among those three; they differ in heading (title+aside on baseline vs top-aligned with `heading` Component), body (auto-height Column vs clipped fill Item) and padding (20 vs 14). The gaps are the hand-rolled cards that do not use the shell: `monitor/RmDeviceCard.qml:24-29` and `monitor/RmPpcpPhoneCard.qml` (surface+`radiusLg`+`colorBorderMid`, i.e. a `PpCardShell` without the rule — B9), `monitor/ScreenResourceMonitor.qml` (10 sites with `color: Theme.colorSurface`), `athlete/PpAthleteCard.qml`, `home/HmTypeCard.qml` (selectable tiles, B12), `shell/PpUpdateBanner.qml:57` (B11).
- **Shared component it should fold into:** keep `PpCard`/`PpStageCard`/`PpPopoverCard` as-is; point the hand-rolled surface+hairline Rectangles at `PpCardShell`. Theme `cardRule`/`heroRule`/`heroTinted` already let Folio flatten the rule, so adoption would not change Folio's look.
- **Effort / risk:** S per file.
- **Confidence:** medium

---

## Already shared / fine
- `PpCardShell` as the single card chassis; `PpPopoverCard` (14 uses in 8 files); `PpInset` (4 uses); `PpCardNote` (19 uses); `PpMicro` (37 uses); `PpDivider` (17 uses); `PpBadge` (12 uses); `PpChip` (16 uses); `PpPressable` as the one hover/press motion.
- `PpCard` / `PpStageCard` / `PpModeStage.PanelCard`: thin and distinct (B25); `PanelCard: PpStageCard` is a correct specialisation.
- `PpChRoles` / `PpDiagRoles` extending `PpTypeset`: genuine layering (the comments say "it IS a PpTypeset"), not duplication.
- `PpSectionHead` is the right shared typeset head; its variants (B14) are the problem, not the component.
- `setup/parts/{SetupStepIntro,SetupSummaryRow,SetupCheckRow,SetupStatusCircle}` and `WizardPage` are a good page-scaffold pattern (6 of 9 pages use `SetupStepIntro`); B22 is only the two stragglers.
- Hard-coded colours that are deliberate: video/preview overlays (`"black"`, `#080a0c`, `Qt.rgba(1,1,1,0.3)` in `settings/CamerasPanel.qml`, `cameras/PpCameraFrame.qml`), athlete avatar palette, `PpQualityPill` `_ink`.
- Orphans that are not duplicates: `trust/PpGateNote.qml` (no QML users; the C++ comment `diagnostics/session_diagnostics_model.cpp:178` names its shape) and `media/FilmPage.qml` (not loaded by `shell/PlayPage.qml`; its StackLayout lists Capture/Audio/Coach/Video only). Noted for the catalogue.

## Not examined
- Controls proper (`PpButton`, `PpComboBox`, `PpTextField`, `PpSegmentedControl`, `PpChipGroup`, `PpToolPill`, `PpTypePill` internals) and the `media/*Page` / `imu/CapturePage` / `cameras/VideoPage` bodies (hand-drawn `Button` contentItems; other agent's cluster). `TuneChip` is only touched via B12.
- Inside `session/PpLmGraphicsBody.qml` (2.6k lines, 40 inline components): only its head components were read.
- `diagnosticmodel/*` beyond popup backgrounds and the two pickers (B7, B23); `review/PpMetricChart.qml` and `PpDataViewer.qml` beyond `SectionHeader`.
- `shell/PpUpdateBanner.qml` was read for the card shape only; `shell/PpHeader.qml`, `PpRail*.qml`, `PpDetectCluster.qml`, `calibration/*`, `viz/*`, `swing3d/*`, `markup/*`, `print/*` untouched.
- Whether `PpCardShell`'s rule and `radiusLg` would change how Folio renders the popups in B7 (needs a render, which a read-only pass cannot do).
