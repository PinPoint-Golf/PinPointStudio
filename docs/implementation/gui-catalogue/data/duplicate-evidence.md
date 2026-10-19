# Duplicate evidence — mechanical

Output of `tools/find_duplicates.py` and the inventory. These are *signals*, not verdicts: every row says only that text or names coincide. The verdicts, with a reading of the code, are in [duplicates.md](../duplicates.md).

## 1. Local (`component X:`) names declared in several files

A local component is private to its file. The same name in several files is almost always the same widget re-implemented per file.

| Local component | Files | Declared in |
|---|---:|---|
| `TogglePill` | 7 | `cameras/PpCameraPanel.qml`, `settings/ArchivingPanel.qml`, `settings/CamerasPanel.qml`, `settings/ImusPanel.qml`, `settings/LaunchMonitorPanel.qml`, `settings/MicrophonesPanel.qml`, `settings/StoragePanel.qml` |
| `ViewFlick` | 3 | `charts/PpChartsRegister.qml`, `diagnostics/PpDiagAnalyse.qml`, `diagnostics/PpDiagReplay.qml` |
| `Chip` | 3 | `diagnosticmodel/ModelCausePicker.qml`, `session/PpLmGraphicsBody.qml`, `swing3d/SwingViz3DView.qml` |
| `ScopedAction` | 2 | `cameras/PpCameraPanel.qml`, `imu/PpImuPanel.qml` |
| `Cell` | 2 | `diagnosticmodel/RingMenu.qml`, `monitor/RmPpcpCameraRow.qml` |
| `ReviewFoot` | 2 | `diagnostics/PpDiagAnalyse.qml`, `diagnostics/PpDiagReplay.qml` |
| `SectionHeader` | 2 | `review/PpDataViewer.qml`, `review/PpMetricChart.qml` |
| `MiniToggle` | 2 | `session/PpMotionPanel.qml`, `session/PpViewPanel.qml` |
| `OrientationTab` | 2 | `viz/ArmVizView.qml`, `viz/BodyVizView.qml` |

### Local components that shadow a module-level component of the same name

| Local name | Declared in | Module component |
|---|---|---|

### Local components named like a shared primitive (not identical names)

| Family | Local components |
|---|---|
| Toggle | `MiniToggle` (PpMotionPanel), `MiniToggle` (PpViewPanel), `TogglePill` (PpCameraPanel), `TogglePill` (ArchivingPanel), `TogglePill` (CamerasPanel), `TogglePill` (ImusPanel), `TogglePill` (LaunchMonitorPanel), `TogglePill` (MicrophonesPanel), `TogglePill` (StoragePanel) |
| Pill | `DevicePill` (PpSessionToolbar), `TogglePill` (PpCameraPanel), `TogglePill` (ArchivingPanel), `TogglePill` (CamerasPanel), `TogglePill` (ImusPanel), `TogglePill` (LaunchMonitorPanel), `TogglePill` (MicrophonesPanel), `TogglePill` (StoragePanel) |
| Chip | `Chip` (ModelCausePicker), `Chip` (PpLmGraphicsBody), `Chip` (SwingViz3DView), `TuneChip` (CamerasPanel) |
| Badge | `StatusBadge` (ArmCalibrationStatus) |
| Card | `ArrangeCard` (PpViewPanel), `HoverCard` (PpLmGraphicsBody), `OrientCard` (PpViewPanel), `PanelCard` (PpModeStage) |
| Section | `SectionHead` (ArchivingPanel), `SectionHeader` (PpDataViewer), `SectionHeader` (PpMetricChart), `SectionLabel` (ReadyPage) |
| Heading | `Heading` (ModelTools), `TuneHeading` (CamerasPanel) |
| Row | `CamRow` (PpCameraPanel), `CameraDeviceRow` (CamerasPanel), `CheckRow` (PpExportOptionsSheet), `CloudFallbackRow` (GeneralPanel), `DeviceRow` (MicrophonesPanel), `ElementRow` (PpMotionPanel), `Eyebrow` (MetricDetail), `ImuDeviceRow` (ImusPanel), `ImuRow` (PpImuPanel), `LedgerRow` (PpLmGraphicsBody), `PhoneDeviceRow` (PhonesPanel), `PresetRow` (PpMotionPanel), `RowTitle` (ArchivingPanel), `ValueRow` (PpFigureRow) |
| Button | `MlButton` (PpMarkupPanel) |
| Btn | `PipBtn` (PpCameraTiles), `TBtn` (PpReplayTransport) |
| Picker | `ShotPicker` (PpLmGraphicsBody) |
| Flick | `ViewFlick` (PpChartsRegister), `ViewFlick` (PpDiagAnalyse), `ViewFlick` (PpDiagReplay) |
| Foot | `ReviewFoot` (PpDiagAnalyse), `ReviewFoot` (PpDiagReplay) |

## 2. Identical function bodies in different files

| Function(s) | Files | Body (start) |
|---|---:|---|
| `findChild` | 10: `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MicrophonesPanel`, `PhonesPanel`, `StoragePanel` | `for (var i = 0; i < parent.children.length; i++) { var child = parent.children[i] if (chil…` |
| `scrollToItem` | 10: `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MicrophonesPanel`, `PhonesPanel`, `StoragePanel` | `if (!itemId) return true var target = findChild(contentCol, itemId) if (!target) return fa…` |
| `_folder` | 3: `PpChartsPanel`, `PpTrustPanel`, `PpTrustSummaryFold` | `return String(p // "").replace(/\\/g, "/").replace(/\/+/g, "/").replace(/\/$/, "")…` |
| `capitalised` | 3: `HmFocus`, `HmGoesTogether`, `HmSwingSummary` | `s = String(s); return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s…` |
| `_typeName` | 3: `MetricDetail`, `MetricLibrary`, `MetricRow` | `switch (t) { case "summary": return qsTr("Summary") case "pointInTime": return qsTr("Point…` |
| `_hasMean` | 3: `PpChartPlot`, `PpChartSummary`, `PpMetricChart` | `return !!(s && s.mean && s.t_us && s.mean.length === s.t_us.length)…` |
| `_meanOf` | 3: `PpChartPlot`, `PpChartSummary`, `PpMetricChart` | `return root._hasMean(s) ? s.mean : s.value…` |
| `vy` | 2: `PpChReadingTrace`, `PpChRunBox` | `return pTop + (1 - (v - lo) / Math.max(1e-9, hi - lo)) * plotH…` |
| `byKey` | 2: `PpChSessionFigure`, `PpChTraceFigure` | `for (let i = 0; i < (list // []).length; ++i) if (list[i].key === key) return list[i]; ret…` |
| `_pointAtSession` | 2: `PpChartsPanel`, `PpTrustPanel` | `if (_folder(sessionDir) !== _folder(seriesModel.sessionDir)) seriesModel.activateSession(s…` |
| `onContentYChanged` | 2: `PpChartsPanel`, `PpSessionDiagnosticsPanel` | `if (root._pendingScroll[modelData] === undefined) scrollSave.restart()…` |
| `firePulse` | 2: `PpDiagAnalyse`, `PpDiagReplay` | `const c = root.pulseCue if (!c) { br.pulseHold = false; br.pulseIds = []; _pulseToken = -1…` |
| `list, listOf` | 2: `PpDiagPrint`, `PpQuestions` | `return xs.length <= 1 ? xs.join("") : xs.slice(0, -1).join(", ") + " and " + xs[xs.length …` |
| `_missCandidates, missCandidates` | 2: `PpDiagReplayState`, `PpSessionDiagnosticsBody` | `if (source && typeof source.missCandidates === "function") return source.missCandidates() …` |
| `byId` | 2: `PpDiagReplayState`, `PpTypeset` | `for (let i = 0; i < (list // []).length; ++i) if (list[i].id === id) return list[i]; retur…` |
| `latestPips` | 2: `HmFocus`, `HmSwingSummary` | `const m = marks // [] return m.length > root.maxPips ? m.slice(m.length - root.maxPips) : …` |
| `_hasDomain` | 2: `PpChartPlot`, `PpSegmentBrush` | `return s.validFromUs !== undefined && s.validToUs !== undefined && s.validToUs > s.validFr…` |
| `_measured` | 2: `PpChartPlot`, `PpSegmentBrush` | `if (root._hasDomain(s) && (s.t_us[i] < s.validFromUs // s.t_us[i] > s.validToUs)) return f…` |
| `_nearestIndex` | 2: `PpChartPlot`, `PpMetricChart` | `var tt = s.t_us if (!tt // tt.length === 0) return -1 var best = -1, bd = Infinity for (va…` |

## 3. Same function name in 3+ files

| Function | Files | In |
|---|---:|---|
| `scrollToItem` | 12 | `DiagnosticModel`, `MetricLibrary`, `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel` … |
| `findChild` | 10 | `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MicrophonesPanel` … |
| `reset` | 5 | `CameraCalibrationFlow`, `HackMotionArmRoutine`, `ImuCalibrationFlow`, `WitmotionArmRoutine`, `SetupDraft` |
| `px` | 5 | `PpCameraFrame`, `PpChMovesWith`, `PpChScatter`, `RingMenu`, `PpBoxPlot` |
| `py` | 5 | `PpCameraFrame`, `PpChScatter`, `RingMenu`, `PpBoxStrip`, `PpHistoryStrip` |
| `enter` | 5 | `WizardPage`, `BallPage`, `CalibrateArmPage`, `CheckArmPage`, `FramingPage` |
| `begin` | 4 | `CameraCalibrationFlow`, `HackMotionArmRoutine`, `ImuCalibrationFlow`, `WitmotionArmRoutine` |
| `_log` | 4 | `HackMotionArmRoutine`, `ImuCalibrationFlow`, `WitmotionArmRoutine`, `SetupFlow` |
| `vy` | 4 | `PpChKindCurve`, `PpChReadingTrace`, `PpChRunBox`, `PpChWristProfile` |
| `xOf` | 4 | `PpChPlot`, `PpChSeqSession`, `PpChTraceFigure`, `PpChWindowBrush` |
| `seekTo` | 4 | `PpChReadingTrace`, `PpChSessionFigure`, `PpChTraceFigure`, `PpChWristShotSection` |
| `applyDemo` | 4 | `PpChartsBody`, `PpChartsRegister`, `PpDiagAnalyse`, `PpDiagReplay` |
| `_folder` | 4 | `PpChartsPanel`, `PpSessionDiagnosticsPanel`, `PpTrustPanel`, `PpTrustSummaryFold` |
| `open` | 4 | `MetricRoute`, `ScreenSessionSetup`, `SetupFlow`, `TrustFocus` |
| `_meanOf` | 4 | `PpChartPlot`, `PpChartSummary`, `PpMetricChart`, `PpSegmentBrush` |
| `leave` | 4 | `WizardPage`, `BallPage`, `CheckArmPage`, `FramingPage` |
| `_phaseUs` | 3 | `PpCameraFrame`, `PpChartSummary`, `PpMetricChart` |
| `fin` | 3 | `PpChPlot`, `PpLmGraphicsBody`, `PpBoxPlot` |
| `picked` | 3 | `PpChWristShotSection`, `PpChartsSessionView`, `PpChartsShotView` |
| `_pointAtSession` | 3 | `PpChartsPanel`, `PpSessionDiagnosticsPanel`, `PpTrustPanel` |
| `pickView` | 3 | `PpChartsRegister`, `PpDiagAnalyse`, `PpDiagReplay` |
| `num` | 3 | `PpBoxStrip`, `PpLmGraphicsBody`, `PpTypeset` |
| `label` | 3 | `PpDotPlot`, `ImuMounts`, `PpLmGraphicsBody` |
| `capitalised` | 3 | `HmFocus`, `HmGoesTogether`, `HmSwingSummary` |
| `_typeName` | 3 | `MetricDetail`, `MetricLibrary`, `MetricRow` |
| `_hasMean` | 3 | `PpChartPlot`, `PpChartSummary`, `PpMetricChart` |

## 4. Whole-file similarity

Jaccard similarity of 4-token shingles after stripping comments/strings/numbers; *containment* = share of the smaller file found in the other. A tiny file fully contained in a large one (e.g. `SetupTogglePill`) is a *copy of a primitive*, not a copy of the large file.

| File A | File B | Jaccard | Containment | Lines A / B |
|---|---|---:|---:|---|
| `athlete/PpAthleteCard.qml` | `athlete/PpAthleteRow.qml` | 0.462 | 0.693 | 91 / 134 |
| `session/ScreenSessionMode.qml` | `session/ScreenWrist.qml` | 0.447 | 0.648 | 118 / 124 |
| `charts/PpChartsPanel.qml` | `diagnostics/PpSessionDiagnosticsPanel.qml` | 0.352 | 0.702 | 287 / 210 |
| `diagnosticmodel/ModelCausePicker.qml` | `diagnosticmodel/ModelPicker.qml` | 0.351 | 0.781 | 235 / 112 |
| `settings/ArchivingPanel.qml` | `settings/LaunchMonitorPanel.qml` | 0.342 | 0.522 | 282 / 445 |
| `diagnosticmodel/ModelPolicyPicker.qml` | `diagnosticmodel/ModelTools.qml` | 0.307 | 0.639 | 71 / 158 |
| `settings/LaunchMonitorPanel.qml` | `settings/StoragePanel.qml` | 0.289 | 0.606 | 445 / 871 |
| `components/PpTypePill.qml` | `diagnosticmodel/ModelChipRow.qml` | 0.221 | 0.64 | 21 / 55 |
| `setup/pages/CheckArmPage.qml` | `setup/parts/SetupStepIntro.qml` | 0.176 | 0.634 | 129 / 33 |
| `setup/pages/ReadyPage.qml` | `setup/parts/SetupSummaryRow.qml` | 0.123 | 0.609 | 266 / 32 |
| `components/PpToolPill.qml` | `session/PpSessionToolbar.qml` | 0.118 | 0.743 | 75 / 645 |
| `setup/pages/ReadyPage.qml` | `setup/parts/SetupStepIntro.qml` | 0.112 | 0.695 | 266 / 33 |
| `diagnosticmodel/ModelEdits.qml` | `diagnosticmodel/ModelInspector.qml` | 0.111 | 0.605 | 119 / 809 |
| `settings/LaunchMonitorPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.099 | 0.942 | 445 / 24 |
| `settings/ArchivingPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.094 | 0.942 | 282 / 24 |
| `home/ScreenHome.qml` | `shell/ScreenPlaceholder.qml` | 0.093 | 0.733 | 550 / 53 |
| `session/PpViewPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.086 | 0.653 | 264 / 24 |
| `diagnosticmodel/ModelChipRow.qml` | `diagnosticmodel/ModelInspector.qml` | 0.071 | 0.617 | 55 / 809 |
| `athlete/AthleteClubsSection.qml` | `setup/parts/SetupTogglePill.qml` | 0.064 | 0.769 | 440 / 24 |
| `settings/MicrophonesPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.063 | 1.0 | 634 / 24 |
| `setup/parts/SetupTogglePill.qml` | `shot/PpShotFilter.qml` | 0.063 | 0.669 | 24 / 312 |
| `settings/StoragePanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.062 | 1.0 | 871 / 24 |
| `settings/DisplaysPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.058 | 0.727 | 705 / 24 |
| `setup/ScreenSessionSetup.qml` | `setup/parts/SetupStepIntro.qml` | 0.056 | 0.626 | 267 / 33 |
| `cameras/PpCameraPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.052 | 0.81 | 346 / 24 |
| `session/PpMotionPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.048 | 0.645 | 393 / 24 |
| `monitor/RmWarningNotice.qml` | `monitor/ScreenResourceMonitor.qml` | 0.043 | 0.66 | 43 / 2016 |
| `settings/GeneralPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.042 | 0.744 | 943 / 24 |
| `monitor/RmWarningNotice.qml` | `settings/ImusPanel.qml` | 0.041 | 0.66 | 43 / 1395 |
| `settings/AppearancePanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.04 | 0.702 | 830 / 24 |

## 5. Shared code blocks (≥ 7 normalised lines)

Runs of identical code (after normalising strings/numbers/whitespace) shared by the same set of files. `Runs` = distinct runs found for that file set; `Longest` = the longest run in lines.

| Files | Runs | Longest | Sample (first lines of longest) |
|---|---:|---:|---|
| `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel` (+4) | 2 | 31 | `property string lastHighlightId: "S" ⏎ function findChild(parent, name) { ⏎ for (var i = N; i < parent.children.length; i++) {` |
| `AppearancePanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel` (+1) | 7 | 13 | `RowLayout { ⏎ objectName: "S" ⏎ Layout.fillWidth: true` |
| `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel` (+2) | 5 | 8 | `} ⏎ } ⏎ RowLayout {` |
| `AppearancePanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel` (+3) | 1 | 7 | `ColumnLayout { ⏎ id: contentCol ⏎ x: Theme.sp(N)` |
| `ScreenSessionMode`, `ScreenWrist` | 3 | 21 | `visible: Layout.preferredHeight > N ⏎ active: visible && root._screenActive ⏎ clip: true` |
| `PpChWristShotSection`, `PpChartsKindsView`, `PpChartsReadingsView`, `PpChartsRelationsView`, `PpChartsSessionView`, `PpChartsShotView` | 1 | 7 | `objectName: "S" ⏎ property QtObject roles: null ⏎ property var brush: null` |
| `BallPage`, `FramingPage`, `GoalsPage`, `ImusPage`, `TriangulatePage` | 1 | 8 | `anchors { left: parent.left; right: parent.right; top: paren ⏎ spacing: Theme.gap(N) ⏎ SetupStepIntro {` |
| `PpChartsKindsView`, `PpChartsReadingsView`, `PpChartsRelationsView`, `PpChartsSessionView`, `PpChartsShotView` | 1 | 7 | `property QtObject roles: null ⏎ property var brush: null ⏎ property var body: null` |
| `AppearancePanel`, `CamerasPanel`, `DisplaysPanel`, `ImusPanel`, `PhonesPanel` | 1 | 7 | `} ⏎ property string lastHighlightId: "S" ⏎ function findChild(parent, name) {` |
| `AppearancePanel`, `DisplaysPanel`, `ImusPanel`, `PhonesPanel`, `StoragePanel` | 1 | 7 | `interval: N ⏎ onTriggered: { ⏎ var target = findChild(contentCol, lastHighlightId)` |
| `CamerasPanel`, `ImusPanel`, `MicrophonesPanel`, `StoragePanel`, `SetupTogglePill` | 1 | 7 | `x: pill.checked ? parent.width - width - Theme.sp(N) : Theme ⏎ Behavior on x { NumberAnimation { duration: N } } ⏎ }` |
| `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `StoragePanel` | 4 | 7 | `Layout.alignment: Qt.AlignVCenter ⏎ } ⏎ }` |
| `PpCameraFrame`, `CamerasPanel` | 1 | 17 | `var h = hr ⏎ if (Math.abs(mx - rx) < h && Math.abs(my - ry) < h) return " ⏎ if (Math.abs(mx - (rx+rw)) < h && Math.abs(my - ry) < h) ret` |
| `CamerasPanel`, `ImusPanel`, `PhonesPanel` | 3 | 11 | `anchors { ⏎ left: parent.left ⏎ right: parent.right` |
| `ScreenHome`, `ScreenSessionMode`, `ScreenPlaceholder` | 1 | 10 | `HoverHandler { ⏎ id: topoHover ⏎ enabled: topo.animated` |
| `ModelEdits`, `ModelUnsaved`, `CamerasPanel`, `ImusPanel` | 1 | 7 | `RowLayout { ⏎ Layout.fillWidth: true ⏎ Layout.leftMargin: Theme.gap(N)` |
| `ModelInspector`, `ModelUnsaved`, `CamerasPanel`, `ImusPanel` | 1 | 7 | `} ⏎ RowLayout { ⏎ Layout.fillWidth: true` |
| `MetricDetail`, `MetricLibrary`, `MetricRow` | 1 | 9 | `function _typeName(t) { ⏎ switch (t) { ⏎ case "S": return qsTr("S")` |
| `PpChartPlot`, `PpSegmentBrush` | 3 | 13 | `var i0 = N ⏎ while (i0 < solid.length) { ⏎ var i1 = i0` |
| `PpChartsPanel`, `PpSessionDiagnosticsPanel` | 5 | 12 | `_pendingScroll = Object.assign({}, nav.scroll // {}) ⏎ _scrollTries = {} ⏎ if (Object.keys(_pendingScroll).length > N) scrollRestore.st` |
| `PpDiagAnalyse`, `PpDiagReplay` | 4 | 12 | `} ⏎ property bool missOpen: false ⏎ property int _pulseToken: -N` |
| `CamerasPanel`, `LaunchMonitorPanel`, `StoragePanel` | 1 | 8 | `property bool searchHighlight: false ⏎ Rectangle { x: -Theme.sp(N); y: -Theme.sp(N); width: parent. ⏎ ColumnLayout {` |
| `PpChartPlot`, `PpMetricChart` | 1 | 11 | `} ⏎ function _nearestIndex(s, t) { ⏎ var tt = s.t_us` |
| `ModelEdits`, `ModelTypeRail`, `ImusPanel` | 1 | 7 | `spacing: N ⏎ RowLayout { ⏎ Layout.fillWidth: true` |
| `ModelEdits`, `ModelUnsaved`, `ImusPanel` | 1 | 7 | `Layout.fillWidth: true ⏎ Layout.leftMargin: Theme.gap(N) ⏎ Layout.rightMargin: Theme.gap(N)` |
| `ModelInspector`, `ModelUnsaved`, `ImusPanel` | 1 | 7 | `opacity: Theme.borderOpacityNormal ⏎ } ⏎ RowLayout {` |
| `ModelPolicyPicker`, `ModelTools`, `ModelUnsaved` | 1 | 7 | `padding: N ⏎ closePolicy: Popup.CloseOnEscape / Popup.CloseOnPressOutside ⏎ background: Rectangle {` |
| `PpcpGuidedPairDialog`, `PpcpPairDialog`, `PpAboutDialog` | 1 | 7 | `parent: Overlay.overlay ⏎ anchors.centerIn: parent ⏎ modal: true` |
| `CamerasPanel`, `MicrophonesPanel`, `PpAboutDialog` | 1 | 7 | `} ⏎ } ⏎ ColumnLayout {` |
| `CamerasPanel`, `ImusPanel`, `MicrophonesPanel` | 1 | 7 | `} ⏎ } ⏎ ColumnLayout {` |
