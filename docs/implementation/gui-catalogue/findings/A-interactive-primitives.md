# Cluster A — interactive primitives: duplicate-code findings

Scope: toggles, pills, chips, segmented controls, buttons, text fields, tabs/pickers, per-device row widgets.
Repo read at `/Users/markliversedge/Projects/PinPointStudio/src/Gui` (read-only; paths below are relative to `src/Gui`). Line numbers are as seen on 19 Oct 2026 and may drift while the other agent edits.

## Headline numbers

| Pattern | Copies found | Approx. lines that fold away |
|---|---|---|
| 34x18 on/off switch re-implemented | 24 implementations, about 44 use sites | about 580 |
| Hand-rolled label-sized button/chip Rectangle that has a hover fill and a Pp/Mouse area, not `PpButton`/`PpChipGroup` | about 87 across 31 files | about 1,500-2,000 |
| Stock `Button`/`TabButton` hand-styled to look like `PpButton` | 17 Buttons + 5 TabButtons in 5 files | about 330 |
| Selectable "choice chip" (accent wash + accent border + hover accentMid) | 21+ outside `PpChipGroup` | about 600 |
| Settings "title / subtitle / control" row with search-highlight overlay | 61 in 9 settings panels | about 400 |
| Local `component` duplicates that are line-for-line the same | `ScopedAction` x2, `MiniToggle` x2, `OrientationTab` x2 | about 110 |

Ranking below is by instance count x lines saved.

---

### A1. On/off switch (`TogglePill`, `MiniToggle`, and ~15 inline copies) implemented 24 times
- **Verdict:** TRUE DUPLICATE (copy-paste), with two small visual drifts
- **Where:**
  - Inline `component TogglePill`: `settings/ArchivingPanel.qml:115` (17 lines, compressed), `settings/CamerasPanel.qml:229` (27), `settings/ImusPanel.qml:33` (27), `settings/LaunchMonitorPanel.qml:90` (30, adds `enabledPill`), `settings/MicrophonesPanel.qml:124` (27), `settings/StoragePanel.qml:125` (27), `cameras/PpCameraPanel.qml:308` (23).
  - File component: `setup/parts/SetupTogglePill.qml` (header says "mirrors the TogglePill in ImusPanel.qml", ported verbatim).
  - `MiniToggle` (identical 19-line bodies): `session/PpMotionPanel.qml:514`, `session/PpViewPanel.qml:317`.
  - Anonymous inline copies, each about 25 lines of `Rectangle{ width sp(34) height sp(18) ... thumb ... MouseArea }`: `settings/GeneralPanel.qml:143` (cfrSwitch), `:410`, `:503`, `:797`, `:853`; `settings/DisplaysPanel.qml:396`, `:451`, `:723`, `:833`; `settings/AppearancePanel.qml:867`, `:922`; `imu/PpImuPanel.qml:419` (enableToggle); `shot/PpShotFilter.qml:215`; `athlete/AthleteClubsSection.qml:490` (patchPill).
  - Use sites of the named components: 20 `TogglePill {` (CamerasPanel 4, ImusPanel 5, StoragePanel 4, LaunchMonitorPanel 3, PpCameraPanel 2, Archiving 1, Microphones 1) + 1 `SetupTogglePill` + 6 `MiniToggle`.
- **What is duplicated:** The same capsule switch: 34x18 track, `colorAccent` when on / `colorBg3` off, a 12 px thumb sliding 3 px in, 120 ms `Behavior on x`, `MouseArea` + pointing-hand cursor. The code is character-for-character the same in 8 files; only the id and the bound property differ. Drift: (a) thumb is `"white"` in 21 copies but `Theme.dark ? colorText : colorSurface` in `PpCameraPanel` and `PpImuPanel` (so the same switch looks different in Settings and in the toolbar popup); (b) `MiniToggle` is a second design: 14 px thumb, 2 px inset, thumb is `colorText3` when off and `colorBg`/`colorSurface` when on, duration `durationFast`; (c) `LaunchMonitorPanel` adds an `enabledPill` flag (opacity 0.4, mouse disabled); `GeneralPanel`'s `cfrSwitch` adds a 0.45 opacity "locked" state. Nothing in `components/` provides a switch, so every panel re-grew one.
- **Shared component it should fold into:** NEW `components/PpSwitch.qml` (about 35 lines): `property bool checked; property bool locked/enabled; signal toggled(bool value)` (emit `!checked`, caller owns state, matching today's contract); thumb colour a single Theme token; `Behavior` durations from Theme. `SetupTogglePill` and `MiniToggle` become `PpSwitch` (delete both). Pick one thumb style (design call) and the drift disappears.
- **Effort / risk:** S-M. Mechanical, about 580 lines deleted. Risk: visible thumb-colour change in whichever style is not chosen; tests/QML visuals that grab Settings, the View/Motion popups and the setup wizard (`session_setup_ui`, qml_ui settings probes) need a re-baseline. Tests that find these by `objectName` (`setting_*` rows) are unaffected because the toggle ids are internal.
- **Confidence:** high

### A2. Hand-rolled label-sized buttons everywhere, bypassing `PpButton` (about 87 sites, 31 files)
- **Verdict:** CANDIDATE UNIFICATION (PARALLEL IMPLEMENTATION of `PpButton`; `PpButton` itself lacks the sizes/variants people need)
- **Where (counts of `Rectangle` sized `label.implicitWidth + sp(N)` with hover fill and Pressable/MouseArea/TapHandler):** `settings/CamerasPanel.qml` 14 (e.g. `:629` mirrorChip, `:895` crop, `:963` ball, `:1048` wide, `:1335` tuning, `:2460` preset, `:2692` reset, `:2726` done, `:3081` preroll), `monitor/ScreenResourceMonitor.qml` 12 (`:224`, `:606`, `:636`, `:661`, `:1570`, `:1600`, `:1898`, `:1925`, `:2142`, `:2169` ...), `settings/ImusPanel.qml` 7 (`:503`, `:580`, `:742`, `:799`, `:1262`, `:1295`, `:1318`), `session/PpSessionToolbar.qml` 7 (`:370`, `:387`, `:404`, `:459`, `:514`, `:548`), `settings/GeneralPanel.qml` 4, `settings/MicrophonesPanel.qml` 3 (`:326`, `:470`, `:760`), `settings/StoragePanel.qml` 3, `review/MetricLibrary.qml` 3, `diagnosticmodel/ModelInspector.qml` 3, `media/FilmPage.qml` 3, `media/CoachPage.qml` 3, `shell/Main.qml:232,264`, `diagnosticmodel/ModelConfirm.qml:110,138`, `setup/pages/ImusPage.qml` 2, `review/PpSpeedSelector.qml` 2, plus 1 each in 14 more files.
- **What is duplicated:** Each is a 20-35 line block: `Rectangle { width: lbl.implicitWidth + sp(16..24); height sp(18..32); radius Theme.radius; color hover ? bg2 : transparent; border 1px borderStrong/Mid -> accentMid on hover; Behavior on color; Text{lbl}; PpPressable/MouseArea/TapHandler }`. `ScreenResourceMonitor` has 10 byte-near-identical copies (18 px high, `sp(10)` text, TapHandler+HoverHandler and its own scale logic). `Main.qml:232` and `ModelConfirm.qml:110` are the same "tone-outlined primary + grey cancel" pair as `PpSessionToolbar.qml:514/548`. `PpButton` (`components/PpButton.qml`) is the same visual language but is fixed at 34 px high, 15 px font, with no compact size and no "tone-outlined" (attention/accent outline) variant. Several comments say so outright (`ModelBarButton.qml` header, `PpMarkupPanel.qml:1102` "PpButton's look at the panel's compact size, kept local because...").
- **Shared component it should fold into:** Extend `PpButton`: `property string size: "normal" | "compact" | "mini"` (heights 34/28/18, font Body/Body2/sp(10)), `property color tone` + `outlined` (covers the attention/accent/error outlined confirm buttons), `property bool toggled/active` (held lit state, as `ModelBarButton.active` and `ImusPanel` Test button), `signal clicked` plus an opt-in `keepFocusOn: Item` (what `MlButton` needs). Then 87 blocks become one-liners.
- **Effort / risk:** M-L (large count, but each replacement is local). Risk: pixel-level differences (heights 26/28/30/32 are currently arbitrary) so every screen's QML visuals shift slightly; `ScreenResourceMonitor` is a diagnostics screen so it is the safest first batch. PpButton's `PpPressable` scale also changes hover feel on the buttons that currently do not scale.
- **Confidence:** high

### A3. Selectable "choice chips" re-implemented 21+ times beside `PpChipGroup`
- **Verdict:** PARALLEL IMPLEMENTATION (and TRUE DUPLICATE inside `CamerasPanel`, which already defines the helper and then copies it)
- **Where:**
  - `settings/CamerasPanel.qml`: `TuneChip` component `:181` (35 lines) used 6 times, yet `fpsChip :710`, `trigChip :785`, `modeChip :1252` and `expChip :1426` repeat the same body (about 35 lines each). The comment at `:179` says TuneChip is "the same look as the mode and exposure chips, without the per-group copy of the Rectangle", but the per-group copies are still there.
  - `settings/StoragePanel.qml:612`, `:692`, `:765` (resolution, codec, quality), `settings/ImusPanel.qml:497` (rateChip), `:1771` (fusionSeg), `settings/CamerasPanel.qml:2460`, `:3081`.
  - `review/MetricLibrary.qml:160`, `:192`, `:226` (capsule, 28 px, same colours but hover = `colorAccentMid` fill, a different hover from every other copy).
  - `setup/pages/ImusPage.qml:327` (mountOption), `setup/pages/GoalsPage.qml:75` (card), `session/PpClubPanel.qml:95` (row), `settings/MicrophonesPanel.qml:155` (DeviceRow, radio style), `session/PpViewPanel.qml:217`/`:271` (`ArrangeCard`, `OrientCard`, two 40-line near-copies of each other with only the bound property differing).
  - `diagnosticmodel/ModelChipRow.qml:62` (capsule 24 px), `diagnosticmodel/ModelCausePicker.qml:276` inline `Chip` (capsule 22 px).
  - Shared: `components/PpChipGroup.qml` (rectangular 28 px, 9 uses in 4 files: `GeneralPanel`, `DisplaysPanel` x4, `AppearancePanel`, `ScreenAthleteForm` x2).
- **What is duplicated:** All of them draw the same three-state surface: rest = alpha-0 fill + `borderStrong/Mid`; hover = faint fill + `colorAccentMid` border; selected = `colorAccentLight` + `colorAccent` border + accent text. The same "RGB-matched alpha-0 `Qt.rgba(colorBg2...,0)`" trick appears 27 times. They differ only in height (22/24/26/28/30), radius (capsule vs `Theme.radius`), font (`fontData` micro vs `fontBody`), hover alpha (0.4 vs 0.6 vs Bg2) and horizontal padding (18/20/24/28). That is five visual dialects of one control.
- **Shared component it should fold into:** One `PpChoiceChip` (single chip: `text, selected, enabled, signal clicked`, `size: "micro"|"normal"`, `shape: "capsule"|"rounded"`, optional `blocked` as `PpChipGroup.disabledOptions` already does) and have `PpChipGroup` become a Row of `PpChoiceChip` with `options` as strings OR `{value,label}` objects. `ArrangeCard`/`OrientCard`/`GoalsPage` card/`PpClubPanel` row can share the same surface via a `PpSelectableSurface` (Rectangle with `selected` + `hovered` that computes colour/border), since they differ only in content.
- **Effort / risk:** M. Wide but mechanical; the risky part is deciding which dialect wins. Affects `settings` and `setup` wizard visuals (`session_setup_ui`), Diagnostic Model (QML tests for `ModelChipRow`/`ModelCausePicker` that look for chips by position).
- **Confidence:** high

### A4. Stock `Button`/`TabButton` hand-styled as `PpButton` in the "play" pages
- **Verdict:** TRUE DUPLICATE of `PpButton`'s three looks (primary accent, warn/stop, outline)
- **Where:** `media/AudioPage.qml:92,114,275,298,320` (5 Buttons), `media/CoachPage.qml:312,367` (2), `media/FilmPage.qml:71,389,409,429,449,471` (6; FilmPage has no QML parent in the app, `used_by` is empty, but it is listed in `cmake/PinPointQmlModule.cmake:59`), `imu/CapturePage.qml:428,451` (2 Buttons) + 4 `TabButton`s at `:235,252,269,286`, `cameras/VideoPage.qml:145,167` (2); `shell/PlayPage.qml:30` is a fifth `TabBar`/`TabButton` copy (3 TabBar implementations in the app, none shared).
- **What is duplicated:** Each `Button` is about 20 lines of `contentItem: Text{...}` + `background: Rectangle{...}` giving one of: accent fill with `Qt.darker(colorAccent,1.1)` on press, `colorBg3` disabled; warn text on `colorWarnLight` with a 50 % warn border (Stop); transparent with `borderStrong`. The three variants repeat verbatim in all five files (for example `Start ●`/`Listen ●`/`Play`, `Stop ■`/`Stop`). `imuTabBar` TabButtons are four 17-line copies differing only in index and label. These bypass `PpPressable`, so none has the hover/press motion of the rest of the app.
- **Shared component it should fold into:** `PpButton` (`primary`, `destructive` already exist; the Stop button is `destructive`, outline is the default). For tabs: a `PpTabs`-like segmented tab strip; `PpSegmentedControl` (`solid:false`) already looks like `imuTabBar`.
- **Effort / risk:** S. Self-contained pages (PlayPage is a lab/diagnostic surface). Risk: low; there is no automated visual for these pages, so check by eye. `PpButton` has no `Keys`/`default` handling, which the Film URL field needs (`Keys.onReturnPressed: downloadBtn.clicked()` works with any item that has `clicked`).
- **Confidence:** high

### A5. `ScopedAction` is duplicated verbatim
- **Verdict:** TRUE DUPLICATE
- **Where:** `cameras/PpCameraPanel.qml:278` (29 lines) and `imu/PpImuPanel.qml:238` (35 lines, same code re-flowed). Used 3x each (`PpCameraPanel.qml:210-217`, `PpImuPanel.qml:154-167`).
- **What is duplicated:** A 50 px tall action tile (glyph over label, width = `(root.width - sp(24) - sp(16)) / 3`), amber `primary` variant using `colorAttentionLight`/`colorAttention`, outline otherwise with alpha-0 rest fill, `PpPressable`, and a `PpConnectingFrame` overlay while `connecting`. The only difference is whitespace. It even hard-codes `root.width`, so each copy silently depends on the host file's root id.
- **Shared component it should fold into:** NEW `components/PpActionTile.qml` with `glyph, label, primary, connecting, signal triggered()`, and let the width come from the parent's `Layout.fillWidth` in a 3-column row instead of `root.width`. Optionally this is `PpButton` with a `vertical` layout (glyph above label) and `attention` (PpButton already has `attention`).
- **Effort / risk:** S. About 55 lines removed; visually identical. Risk only that the panel width maths differs between the two panels (verify both).
- **Confidence:** high

### A6. Toolbar popup rows `CamRow` and `ImuRow` share one layout; settings cards share one shell
- **Verdict:** PARALLEL IMPLEMENTATION (two pairs)
- **Where:**
  - Popup rows: `cameras/PpCameraPanel.qml:332` `CamRow` (230 lines) and `imu/PpImuPanel.qml:275` `ImuRow` (170 lines).
  - Settings cards: `settings/CamerasPanel.qml:260` `CameraDeviceRow` (about 2,620 lines, ends `:2879`), `settings/ImusPanel.qml:64` `ImuDeviceRow` (about 1,280 lines, ends `:1343`), `settings/PhonesPanel.qml:175` `PhoneDeviceRow` (about 460 lines, ends `:633`). Related, differently shaped: `settings/MicrophonesPanel.qml:155` `DeviceRow` (82 lines, radio-style), `setup/parts/SetupCheckRow.qml` (the wizard's device row), `monitor/RmPpcpCameraRow.qml`, `monitor/RmPpcpPhoneCard.qml`.
- **What is duplicated:**
  - `CamRow`/`ImuRow`: same skeleton, `Item` with a hairline `Rectangle` at the bottom, `RowLayout` with 15 px side margins and 11 px spacing, `[PpBadge state mark] [Column: name, mono status text, chip row] [Enable text] [TogglePill]`, `opacity 0.45` when the device is disabled, the same width-elision traps documented twice in near-identical comments, `PpChip` for battery/torch. Differences: the badge `kind/tone` expression and the status string, which come from the device.
  - Settings cards: identical shell in all three: `Rectangle{ color: colorSurface; radius }` + `clip: true` + a second `Rectangle` "Border overlay, z:100" whose `border.color` is a state colour, then a 54 px header `RowLayout` (`margins gap(14)`, spacing `gap(10)`) with a 6 px status dot and an alias `PpTextField` ("Device alias..." placeholder in Cameras/Imus/Phones). `PhonesPanel.qml:215-221` states it copies "what CameraDeviceRow/ImuDeviceRow actually do". `Behavior on implicitHeight` + `opacity` for the excluded state are repeated as well.
- **Shared component it should fold into:** NEW `components/PpDeviceRow.qml` for the popup rows (`badgeKind, badgeTone, title, statusText, statusColor, enabledFlag, signal enableToggled, default property alias chips`), and NEW `settings/PpDeviceCard.qml` (shell + header slot: `borderColor`, `statusDotColor`, `alias`, `excluded`, `default property alias body`). The domain-specific bodies stay where they are.
- **Effort / risk:** M for the shell/popup rows, L if the 2,620-line `CameraDeviceRow` is split along with it. Highest-risk files in the cluster (live device state, ROI editor, PPCP torch, instance lifetime comments). The shell extraction itself is safe; leave bodies alone. Tests: settings device-row QML tests and the `imu`/`cameras` panel probes.
- **Confidence:** medium-high (shell and popup rows: high; whether to unify bodies: low, they are legitimately device-specific)

### A7. Settings "setting row" (title + subtitle + control + search highlight) hand-built 61 times
- **Verdict:** CANDIDATE UNIFICATION (cross-cluster; settings agent may also report it)
- **Where:** `property bool searchHighlight: false` + a one-line highlight `Rectangle { x: -sp(6) ... z: -1 }`: `settings/GeneralPanel.qml` 12, `DisplaysPanel` 10, `StoragePanel` 9, `LaunchMonitorPanel` 8, `AppearancePanel` 7, `ArchivingPanel` 6, `ImusPanel` 4, `MicrophonesPanel` 2, `CamerasPanel` 2, `PhonesPanel` 1. The 31-line `scrollToItem`/`findChild`/`lastHighlightId` clone appears in 10 panel files (`duplicates.json` clone #1).
- **What is duplicated:** Each row is `RowLayout{objectName:"setting_x"; searchHighlight; highlight Rect; ColumnLayout{ Text title (fontBody, fontSzBody, colorText); Text subtitle (fontData, micro, colorText3) }; <control> }`, about 22 lines of chrome per row, around the 6-8 control types that are already A1/A3/`PpComboBox`.
- **Shared component it should fold into:** NEW `settings/PpSettingRow.qml` (`id/objectName, title, subtitle, default property alias control, searchHighlight`) and one shared `SettingsScroll.js` or base `SettingsPanelBase.qml` for the scroll/highlight function. `ArchivingPanel.RowTitle` (`:141`) and `GeneralPanel.CloudFallbackRow` (`:88`) are already half of this.
- **Effort / risk:** M. Risk: settings search (`SettingsIndex.qml`) finds rows by `objectName` and toggles `searchHighlight`, so the component must expose both. Covered by the settings-search tests.
- **Confidence:** high

### A8. `DevicePill` (toolbar) is a copy of `PpToolPill`'s shell; PpPressable's motion is re-derived by hand three times
- **Verdict:** TRUE DUPLICATE (shell) + PARALLEL IMPLEMENTATION (motion)
- **Where:** `components/PpToolPill.qml:46-67` vs `session/PpSessionToolbar.qml:796-840` (`DevicePill`, 2 uses at `:637`, `:662`); also the `endBtn` at `session/PpSessionToolbar.qml:452-490`. `duplicates.json` already flags `PpToolPill`/`PpSessionToolbar` (8-line clone). Hand-rolled scale: `PpToolPill.qml:59-60,63`, `PpSessionToolbar.qml:821-826`, `:470-476`, and again in `monitor/ScreenResourceMonitor.qml` (12 sites).
- **What is duplicated:** Both pills are `Rectangle` 44 px high, `radius Theme.radius`, `colorBg2`/`colorBg3` hover, border accent when `active` / accentMid on hover / borderMid, a 34x34 glyph tile at left, a micro caption over a value, ~8 lines identical for the `scale: pressed ? 0.97 : (active||hover) ? 1.02 : 1` triple and the three `Behavior`s. `DevicePill` adds a status `PpBadge` on the tile and a status-tinted value; `PpToolPill` adds a `▾` and a `badge` dot. The scale logic is exactly `PpPressable { held: active }` ("mirrors the toolbar pills holding while their popup is open", `PpPressable.qml:30`), but `PpToolPill`, `DevicePill` and `endBtn` still carry their own copy.
- **Shared component it should fold into:** `PpToolPill` gets `badgeKind/badgeTone` (replacing the dot) and `showChevron`; `DevicePill` becomes `PpToolPill`; all three then use `PpPressable { held: active }` and drop the hand `scale/Behavior`.
- **Effort / risk:** S. About 55 lines. Risk: the toolbar is the app's most-looked-at strip; verify `held` timing with popups open (the comment says the held state is meaningful there).
- **Confidence:** high

### A9. Capsule segmented selectors: `PpSegmentedControl` + `ElementSegmented` + `PpSpeedSelector` (+ `PpUnitToggle`)
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `components/PpSegmentedControl.qml` (10 uses), `session/PpMotionPanel.qml:404-510` `ElementSegmented` (about 105 lines of which about 50 is the control, the rest availability logic), `review/PpSpeedSelector.qml` (about 85 lines, 1 use at `review/PpReplayTransport.qml:51`), `components/PpUnitToggle.qml` (88 lines, 3 uses: a rectangular "joined buttons" variant with per-corner radii).
- **What is duplicated:** All three capsule controls are a recessed pill-shaped track with equal child capsules, the active one tinted, hover brightens `colorBg3`, `Behavior on color`, `MouseArea{hoverEnabled; PointingHand}`. `ElementSegmented` says it exists because `PpSegmentedControl` supports neither a per-segment disabled state nor glyph!=value; `PpSpeedSelector` because values are numbers and the active test is a float proximity. All three could be `PpSegmentedControl` with `model: [{value, label, enabled}]`, `isActive(value)` and `labelFont`. `PpUnitToggle` is the same job (2-3 options) with a rectangular style; `PpChipGroup` with 2 options is another.
- **Shared component it should fold into:** `PpSegmentedControl` extended with object options (`{value, label|glyph, enabled}`), a `compact` size and `activeColor` style (`solid`/`wash`/`outline`); retire `ElementSegmented`'s drawing and `PpSpeedSelector` (keep their availability logic as data). Leave `PpUnitToggle` or give it `shape: "joined"`.
- **Effort / risk:** S-M. About 150 lines. Risk: `PpMotionPanel` elements' dim/blocked reasons are covered by Motion-panel probes; the `PpReplayTransport` speed selector is exercised by replay QML tests.
- **Confidence:** medium-high

### A10. Small icon/bar buttons defined four separate times
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `review/PpReplayTransport.qml:66` `TBtn` (18 lines, 7 uses), `cameras/PpCameraTiles.qml:522` `PipBtn` (15 lines, 3 uses), `session/PpMarkupPanel.qml:1104` `MlButton` (28 lines, 13 uses), `diagnosticmodel/ModelBarButton.qml` (file, about 90 lines, 6 uses; its own header explains it exists because `PpButton` is too big for the bar), and `diagnosticmodel/ModelTools.qml:75` `Action` (30 lines, 5 uses, a flat menu row).
- **What is duplicated:** `TBtn`, `PipBtn` and `ModelBarButton`'s glyph mode are the same widget: a glyph in `fontSymbol` centred on a small `Rectangle`, transparent until hovered (`colorBg3`, or `alpha(onScrim,0.16)` for `PipBtn`), optional lit state (`TBtn.highlight` border, `ModelBarButton.active`), `PpPressable` or `PpPressable`-like hover. `MlButton` and `ModelBarButton` are the same "28 px, label, outline until hover, `colorAccent` fill when primary" button; `MlButton` only adds `root.forceActiveFocus()` on click. All four recreate what an A2-extended `PpButton` (compact, `glyph`, `active`) would give.
- **Shared component it should fold into:** `PpButton` with `size:"compact"`/`"icon"` and `active`; keep `ModelBarButton` as the thin wrapper that sets tooltip/hint if wanted. `PipBtn` needs a `onDark`/scrim tone (a `tone` property).
- **Effort / risk:** S. About 100 lines. Risk: `MlButton`'s keyboard-focus handback (a documented behaviour for the markup P-keys) must survive as a signal hook, so do not drop it.
- **Confidence:** high

### A11. `ModelPicker` and `ModelCausePicker` are the same type-ahead popup
- **Verdict:** TRUE DUPLICATE (about 90 shared lines) / CANDIDATE UNIFICATION
- **Where:** `diagnosticmodel/ModelPicker.qml:30-170` (used from `DiagnosticModel.qml`) and `diagnosticmodel/ModelCausePicker.qml:17-343` (used from `DiagnosticModel.qml` and `ModelGraph.qml`). `ModelPolicyPicker.qml` is a third small cousin. `duplicates.json` flags `ModelCausePicker`/`ModelPicker` ("Text {" clone).
- **What is duplicated:** Both: a `Popup` (non-modal, `CloseOnEscape|CloseOnPressOutside`, `padding 0`, `onOpened` clears the field and focuses it), a caps micro title, a `PpTextField` with identical `Keys.onReturnPressed/EnterPressed/DownPressed/UpPressed` handlers (down/up clamped to `listView.count - 1` / `0`), a count line (`%n legal target(s)`), a `ListView` of 28 px rows (`colorAccentLight` current / `colorBg2` hover / transparent, label `fontSzBody2`, right-aligned elided detail with `maximumWidth` 0.36-0.4 x width), `PpPressable{hoverScale:1.0}`, and a `_take()` that picks `list[clamp(currentIndex)]`, emits `picked` and closes. `ModelCausePicker` adds "create new" rows and type chips; otherwise `ModelPicker` is a strict subset.
- **Shared component it should fold into:** One `ModelTypeAhead.qml` (props: `title`, `candidateSource`, `allowCreate`, `emptyText`, `footer` slot) with `ModelPicker` = no create; `ModelCausePicker` = `allowCreate` + type-chip footer.
- **Effort / risk:** S-M. About 120 lines. Risk: Diagnostic Model has dedicated QML tests (`candidateSource` filtering, Enter budget "click, three chars, Enter"), so re-run those; do not touch the C++ filter.
- **Confidence:** high

### A12. Plain popup chrome and "option row" repeated across 13 diagnostic/model popups
- **Verdict:** TRUE DUPLICATE (small) / LEGITIMATE VARIANT of `PpPopoverCard` (it is deliberately plain, no tone rule)
- **Where:** Identical `background: Rectangle { color: colorSurface; radius: Theme.radius; border.width: 1; border.color: colorBorderStrong }` at `home/PpcpPairDialog.qml:148`, `home/PpcpGuidedPairDialog.qml:108`, `shell/Main.qml:188`, `shell/PpAboutDialog.qml:64`, `diagnosticmodel/ModelEnumField.qml:102`, `ModelConfirm.qml:69`, `ModelUnsaved.qml:49`, `ModelMint.qml:58`, `ModelTools.qml:60`, `ModelPolicyPicker.qml:44`, `ModelPicker.qml:50`, `ModelCausePicker.qml:78`, `ModelTable.qml:511`. (`PpComboBox.qml` has it too and is the legitimate owner.) 17 popups in `session/PpSessionToolbar`, `shot/*`, `review/*` already use `PpPopoverCard`.
- **What is duplicated:** The 6-line background in 13 files, plus a 28 px "option row" (hover `colorBg2`, chosen text in accent, `PpPressable{hoverScale:1.0}`, `font.weight: fontBodyWeight`) in `ModelEnumField.qml:112-140`, `ModelTable.qml:~520-550`, `ModelPolicyPicker.qml:57-110`, `ModelPicker.qml` and `ModelCausePicker.qml` row delegates.
- **Shared component it should fold into:** `PpPopoverCard` gets `plain: true` (suppress the 3 px rule), or new `PpPopupSurface`; a shared `PpMenuRow { label, detail, chosen, current }` for the option rows.
- **Effort / risk:** S. About 100 lines. Risk: negligible visually if `plain` reproduces the current look exactly.
- **Confidence:** medium (whether to merge with `PpPopoverCard` is a design decision)

### A13. `ModelEnumField` and `ModelTable`'s enum popup re-implement `PpComboBox`
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `diagnosticmodel/ModelEnumField.qml` (about 142 lines, header: "replaces a PpSegmentedControl"), `diagnosticmodel/ModelTable.qml:500-555` (enumPopup), `components/PpComboBox.qml` (about 130 lines, 21 uses in 15 files).
- **What is duplicated:** A field box (`colorSurface`, 1 px `borderStrong`/accent when open, `⌄` caret in `fontBody`, "click opens, click picks"), a popup list of options with the chosen one in accent, and `elide`. `ModelEnumField`'s own comment says "PpComboBox uses the same" caret. Differences: `{value,label}` options and value-based selection (vs `PpComboBox`'s index model), and a `displayFn`; both are things `ComboBox` already supports via `textRole`/`valueRole`.
- **Shared component it should fold into:** `PpComboBox` with `model: [{value,label}]`, `textRole:"label"`, `valueRole:"value"`, `currentValue` / `onActivated`; `ModelEnumField` becomes a 10-line wrapper (`options`, `value`, `picked`).
- **Effort / risk:** S-M. About 110 lines. Risk: `ModelEnumField` is used in the inspector where 480 px width and 30 px height are specific; `PpComboBox` height is 34 px (add `compact`).
- **Confidence:** medium

### A14. Text inputs bypassing `PpTextField` (and a missing search-field component)
- **Verdict:** PARALLEL IMPLEMENTATION
- **Where:** `settings/ScreenSettings.qml:208` (underline-style stock `TextField`), `athlete/ScreenAthletePicker.qml:138` (boxed `TextField`, 28 px), `monitor/ScreenResourceMonitor.qml:1654` and `:2019` (boxed `TextInput` + "Filter..." placeholder `Text` + `x` clear, copied), `settings/CamerasPanel.qml:2290,2325,2378,2413` (four boxed `TextInput` crop fields X/Y/W/H, ~25 lines each, focus border via `activeFocus`), `media/FilmPage.qml:45`, `media/AudioPage.qml:69,246`, `media/CoachPage.qml:336` (stock `TextField`/`TextArea` with custom background), `charts/PpChartsReadingsView.qml:114` (typeset underline filter, legitimately typeset-styled). Compare: `components/PpTextField.qml` (34 uses in 17 files) draws the box, focus ring, commit-on-blur.
- **What is duplicated:** The focus-ring box (`colorSurface`, 1 px, accent when `activeFocus`, `borderStrong` otherwise, `radius`) is re-written in 9 places; the search/filter field with placeholder + clear affordance in 3 (`ScreenSettings`, `ScreenAthletePicker`, `ScreenResourceMonitor` x2, plus the `PpTextField`-based `MetricLibrary.qml:144`, `ModelPicker.qml:76` search boxes). The four CamerasPanel crop fields differ only in the property they read/write.
- **Shared component it should fold into:** `PpTextField` (add `compact`, `monospace`, `clearable`) and, for search, a `PpSearchField` (`placeholder, clearable, signal textEdited`, `hasIcon`). The crop X/Y/W/H group becomes a `Repeater` over four `{label, getter, setter}` in `CamerasPanel`.
- **Effort / risk:** S-M. About 200 lines. Risk: `PpTextField` commits on blur (`editingFinished` on focus loss) which would be wrong for live filters (`onTextChanged`), so a search field must not inherit that behaviour; `ScreenSettings` search is used by settings-search tests.
- **Confidence:** medium-high

### A15. Status mark vocabularies: `PpBadge` vs `SetupStatusCircle` vs 6 px status dots
- **Verdict:** CANDIDATE UNIFICATION
- **Where:** `components/PpBadge.qml` (15 uses; check/target/easing/unconfirmed, shape-per-meaning), `setup/parts/SetupStatusCircle.qml` (1 use, `SetupCheckRow.qml`; colour-only ring, tick only when ok), 6 px dots in `settings/CamerasPanel.qml:~393`, `settings/ImusPanel.qml:~168`, `settings/PhonesPanel.qml:~246` (three different colour mappings), `diagnosticmodel/ModelInspector.qml`/`GeneralPanel.qml` status badges (`StatusBadge` in `calibration/ArmCalibrationStatus.qml:191`, `GeneralPanel.qml:906/927/1044`, `PpAboutDialog.qml:149`).
- **What is duplicated:** Same job (device OK / warn / bad / unknown). `PpBadge` was built to avoid colour-only signalling (its header says so); `SetupStatusCircle` is colour-only for warn/error, so the wizard rows and the toolbar device rows describe the same IMU in two vocabularies. The three settings dots are the same 6 px circle with different mappings (Cameras: good/warn/text3; IMUs: good/warn/text3 with different conditions; Phones: good/accent/text3/warn).
- **Shared component it should fold into:** `PpBadge` (add `kind: "warn" | "error"` or reuse `target` with tone) for `SetupStatusCircle`; a `PpStatusDot { state }` for the three settings dots, or fold them into the A6 card shell.
- **Effort / risk:** S. Risk: wizard (`session_setup_ui`) checks `SetupStatusCircle` visually; swapping to `PpBadge` changes its look.
- **Confidence:** medium

### A16. Local inline components that shadow, or collide with, shared names
- **Verdict:** LEGITIMATE VARIANT (naming hazard) for `Chip`; CANDIDATE UNIFICATION for the heading components
- **Where:**
  - Three unrelated inline `Chip` components: `diagnosticmodel/ModelCausePicker.qml:276` (selectable capsule, A3), `session/PpLmGraphicsBody.qml:2528` (typeset-styled tappable control, depends on the LM `h.ts` roles; legitimate), `swing3d/SwingViz3DView.qml:188` (thin wrapper `Chip: PpChip` adding `label`/`caveat`; legitimate wrapper over `PpChip`). Two more `Switch` (`diagnosticmodel/ModelGraph.qml:2010`, a text toggle, and Qt's own `Switch` type) and `Step` live next to it.
  - Caps micro headings: `settings/CamerasPanel.qml:218` `TuneHeading` (7 lines, 8 uses), `settings/ArchivingPanel.qml:133` `SectionHead` (7 lines), `diagnosticmodel/ModelTools.qml:67` `Heading` (7 lines), versus `components/PpMicro.qml` (104 uses). `font.capitalization: Theme.capsFont` is hand-written 92 times in 28 files (CamerasPanel 17, ImusPanel 15, StoragePanel 8, GeneralPanel 6, AppearancePanel 6), mostly with `fontBody` (so not byte-equal to `PpMicro`'s `fontData`).
- **What is duplicated:** The three heading components are the same `Text` (micro size, `trackingMicro`, caps, `colorText3`); `PpMicro` is the same without `capitalization` and with `fontData`.
- **Shared component it should fold into:** `PpMicro` with `property bool caps` and `property bool body`. Rename the unrelated `Chip`s so a future reader does not assume one thing (`ModelTypeChip`, `LmChip`, `StatusChip`). Not worth merging the LM or 3D chips.
- **Effort / risk:** S. About 3 x 7 lines plus naming; the 92 hand-written headings are optional churn. Risk: `PpLmGraphicsBody` is 2,600 lines, so renaming `Chip` there touches many call sites for no gain; leave it.
- **Confidence:** medium

### A17. `OrientationTab` duplicated between `ArmVizView` and `BodyVizView`
- **Verdict:** TRUE DUPLICATE (not an interactive primitive, found in passing)
- **Where:** `viz/ArmVizView.qml:290` and `viz/BodyVizView.qml:97` (about 18 lines each; `duplicates.json`: `ArmVizView`/`BodyVizView` also share `DirectionalLight`/`PointLight` blocks and `showOrientationTabs`).
- **What is duplicated:** A `Node` with a small `#Sphere` `Model` offset along `tabDir`, `PrincipledMaterial{baseColor: tabColor; NoLighting}`. Differences: `tabScale` 0.0006 vs 0.0003 ("50 % smaller here") and a different `visible` condition (`root.showOrientationTabs` vs `... && highlightLeadArm && rightHanded`).
- **Shared component it should fold into:** NEW `viz/VizOrientationMarker.qml` with `along, tabColor, tabScale, tabDir` (the `visible` expression is passed by the caller).
- **Effort / risk:** S. Risk: none beyond Qt Quick 3D `parent` property gotchas (both files comment on id-qualifying `tabColor`).
- **Confidence:** high

### A18. `PpTypePill` and `PpChip{tinted:false}` are the same grey outline pill
- **Verdict:** CANDIDATE UNIFICATION (low value)
- **Where:** `components/PpTypePill.qml` (63 lines, 1 use: `review/MetricDetail.qml`), `components/PpChip.qml` (about 55 lines, 16 uses).
- **What is duplicated:** Both are a capsule `Rectangle` with a 1 px outline and a micro mono word. `PpTypePill`: height 22, `colorBorderMid`, `colorText3`, caps via `font.capitalization`, `Layout.alignment`. `PpChip` untinted: height 18, `colorBorderStrong`, tone text colour. The `PpTypePill` header gives a design reason (do not call it `PpPill`, it is an "outline, not filled" frame around the subject), but it is a visual preset of `PpChip`, not a different component. `PpTypePill` is NOT the same as `ModelChipRow` (selectable chips) despite the similar name.
- **Shared component it should fold into:** `PpChip { tinted: false; tone: colorText3; height: sp(22); caps: true }` or add `size` and `caps` props; keep `PpTypePill` as a 6-line preset if the name is useful for docs.
- **Effort / risk:** S. About 50 lines saved. Risk: none (1 use).
- **Confidence:** medium

### A19. Three `TabBar`/tab-strip idioms
- **Verdict:** LEGITIMATE VARIANT for `PpTabs` (typeset ink style), PARALLEL IMPLEMENTATION for the stock-`TabBar` pair
- **Where:** `typeset/PpTabs.qml` (4 uses: `PpChartsRegister`, `PpDiagReplay`, `PpDiagAnalyse`, `PpLmGraphicsBody`; underlined-in-ink tabs with counts, depends on `PpTypeset` roles; correct to keep separate), `shell/PlayPage.qml:30` (stock `TabBar` + `TabButton`, accent underline), `imu/CapturePage.qml:226` (stock `TabBar`, pill style, four copies).
- **What is duplicated:** The two stock-`TabBar` strips are each hand-styled `TabButton`s with the same `contentItem: Text` and a `background: Rectangle` keyed on `checked`; they look different from each other and from `PpTabs`.
- **Shared component it should fold into:** A small `PpTabBar` (`model: [labels]`, style `"underline"|"pill"`) for the two lab pages; do not merge with `PpTabs`.
- **Effort / risk:** S. Low value (lab pages, 5 `TabButton`s). Risk: low.
- **Confidence:** medium

---

## Components that look fine / already shared

- `components/PpPressable.qml` (62 files, 145 uses): correctly the single motion primitive; 4 named copies in A8/A2 are the exceptions.
- `components/PpButton.qml` (66 uses in 27 files): good shared button; gap is size/variants (A2), not duplication.
- `components/PpChipGroup.qml` (9 uses): good API; used wherever a settings enum is 2-6 words.
- `components/PpTextField.qml` (34 uses in 17 files), `components/PpComboBox.qml` (21 uses in 15 files): consistently used; exceptions are A13/A14.
- `components/PpChip.qml` (16 uses): good read-only tag; `SwingViz3DView`'s `Chip: PpChip` wrapper (`:188`) is a proper thin wrapper.
- `components/PpBadge.qml` (15 uses), `PpMicro.qml` (104 uses), `PpConnectingFrame.qml`, `PpCardNote.qml`.
- `components/PpUnitToggle.qml` (2 files): used for the only genuine 2-unit joined buttons; keep, but see A9 for a possible style flag.
- `typeset/PpInlinePicker.qml` (6 uses), `charts/PpChReadingPicker.qml`, `typeset/PpTabs.qml`: share the typeset roles/style on purpose; `PpInlinePicker` (vertical list in place) and `PpChReadingPicker` (families of flowing links) are different jobs, not duplicates.
- `session/PpLmGraphicsBody.qml` inline `Chip` and `ShotPicker` (2474): typeset/LM-themed, bound to `h.ts`; keep.
- `diagnosticmodel/ModelPolicyPicker.qml`: unique "named policy with hint" popup; only the chrome in A12 repeats.
- `diagnosticmodel/ModelConfirm.qml`, `ModelUnsaved.qml`, `Main.qml` close confirm: duplicate their button pair (A2) but each popup is a distinct dialog.
- `settings/MicrophonesPanel.qml` `DeviceRow` (radio row) is legitimately different from the other device rows (selectable, not an enable-toggle card); only its selected-surface code is A3.
- `monitor/RmPpcpCameraRow.qml` (`Cell`, 18 lines) and `RmPpcpPhoneCard.qml` (`Reading`, 20 lines): small read-only monitor cells, nothing shared worth extracting.

## Not examined

- `shot/PpShotFilter.qml`, `PpShotCarousel.qml`, `PpShotActionBar.qml` and `athlete/ScreenAthletePicker.qml` row widgets in depth (only their toggle/search-field code was checked; `shot/PpShotPicker` does not exist).
- `home/*` tiles and `ScreenHome.qml`'s `clubChip` (`:493`) beyond the inventory hit.
- The interior of the 2,620-line `CameraDeviceRow` and 1,280-line `ImuDeviceRow` (ROI editor, tuning panels, capability strips). They almost certainly contain further internal duplication (14 button blocks counted in `CamerasPanel`), but this pass only measured the button/chip/toggle layer.
- `calibration/ArmCalibrationStatus.qml` inline components (`StatusBadge`, `HmStepBar`, `ProgressBar`, ...), `shell/PpDetectCluster`, `shell/PpHeader` buttons, and the `Pp*Fold`/`Trust*` mouse areas (likely content/interaction areas, not primitives).
- `tests/` QML probes and the setup wizard's remaining `setup/parts/*` beyond `SetupCheckRow`/`SetupStatusCircle`/`SetupTogglePill`.
- Sliders (`AppearancePanel` x2, `MicrophonesPanel` x2, `PpMarkupPanel`, `FilmPage`): not compared with each other; there is no shared slider in `components/`, so this may be a further gap.
- Line numbers cited as `~` were read from nearby context and may be off by a few lines.
