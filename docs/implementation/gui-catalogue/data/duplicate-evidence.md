# Duplicate evidence — mechanical

Output of `tools/find_duplicates.py` and the inventory. These are *signals*, not verdicts: every row says only that text or names coincide. The verdicts, with a reading of the code, are in [duplicates.md](../duplicates.md).

## 1. Local (`component X:`) names declared in several files

A local component is private to its file. The same name in several files is almost always the same widget re-implemented per file.

| Local component | Files | Declared in |
|---|---:|---|
| `TogglePill` | 8 | `cameras/PpCameraPanel.qml`, `settings/ArchivingPanel.qml`, `settings/CamerasPanel.qml`, `settings/ImusPanel.qml`, `settings/LaunchMonitorPanel.qml`, `settings/MicrophonesPanel.qml`, `settings/StoragePanel.qml`, `settings/StudioPanel.qml` |
| `ViewFlick` | 3 | `charts/PpChartsRegister.qml`, `diagnostics/PpDiagAnalyse.qml`, `diagnostics/PpDiagReplay.qml` |
| `Chip` | 3 | `diagnosticmodel/ModelCausePicker.qml`, `session/PpLmGraphicsBody.qml`, `swing3d/SwingViz3DView.qml` |
| `ScopedAction` | 2 | `cameras/PpCameraPanel.qml`, `imu/PpImuPanel.qml` |
| `Cell` | 2 | `diagnosticmodel/RingMenu.qml`, `monitor/RmPpcpCameraRow.qml` |
| `ReviewFoot` | 2 | `diagnostics/PpDiagAnalyse.qml`, `diagnostics/PpDiagReplay.qml` |
| `Caption` | 2 | `home/HmFocus.qml`, `home/HmSwingSummary.qml` |
| `ShotTable` | 2 | `report/PpLmAppendix.qml`, `session/PpLmGraphicsBody.qml` |
| `Para` | 2 | `report/PpReportAbout.qml`, `report/PpWorkOnPage.qml` |
| `SectionHeader` | 2 | `review/PpDataViewer.qml`, `review/PpMetricChart.qml` |
| `MiniToggle` | 2 | `session/PpMotionPanel.qml`, `session/PpViewPanel.qml` |
| `SectionHead` | 2 | `settings/ArchivingPanel.qml`, `settings/StudioPanel.qml` |
| `RowTitle` | 2 | `settings/ArchivingPanel.qml`, `settings/StudioPanel.qml` |
| `OrientationTab` | 2 | `viz/ArmVizView.qml`, `viz/BodyVizView.qml` |

### Local components that shadow a module-level component of the same name

| Local name | Declared in | Module component |
|---|---|---|

### Local components named like a shared primitive (not identical names)

| Family | Local components |
|---|---|
| Toggle | `MiniToggle` (PpMotionPanel), `MiniToggle` (PpViewPanel), `TogglePill` (PpCameraPanel), `TogglePill` (ArchivingPanel), `TogglePill` (CamerasPanel), `TogglePill` (ImusPanel), `TogglePill` (LaunchMonitorPanel), `TogglePill` (MicrophonesPanel), `TogglePill` (StoragePanel), `TogglePill` (StudioPanel) |
| Pill | `DevicePill` (PpSessionToolbar), `TogglePill` (PpCameraPanel), `TogglePill` (ArchivingPanel), `TogglePill` (CamerasPanel), `TogglePill` (ImusPanel), `TogglePill` (LaunchMonitorPanel), `TogglePill` (MicrophonesPanel), `TogglePill` (StoragePanel), `TogglePill` (StudioPanel) |
| Chip | `Chip` (ModelCausePicker), `Chip` (PpLmGraphicsBody), `Chip` (SwingViz3DView), `TuneChip` (CamerasPanel) |
| Badge | `StatusBadge` (ArmCalibrationStatus) |
| Card | `ArrangeCard` (PpViewPanel), `CardHeader` (HmSwingSummary), `HoverCard` (PpLmGraphicsBody), `OrientCard` (PpViewPanel), `PanelCard` (PpModeStage) |
| Section | `SectionHead` (ArchivingPanel), `SectionHead` (StudioPanel), `SectionHeader` (PpDataViewer), `SectionHeader` (PpMetricChart), `SectionLabel` (ReadyPage) |
| Heading | `Heading` (ModelTools), `TuneHeading` (CamerasPanel) |
| Row | `CamRow` (PpCameraPanel), `CameraDeviceRow` (CamerasPanel), `CloudFallbackRow` (GeneralPanel), `ColsRow` (PpOrderPage), `DeviceRow` (MicrophonesPanel), `ElementRow` (PpMotionPanel), `Eyebrow` (MetricDetail), `FieldRow` (StudioPanel), `ImageRow` (StudioPanel), `ImuDeviceRow` (ImusPanel), `ImuRow` (PpImuPanel), `LedgerRow` (PpLmGraphicsBody), `PhoneDeviceRow` (PhonesPanel), `PresetRow` (PpMotionPanel), `RowTitle` (ArchivingPanel), `RowTitle` (StudioPanel), `SettingRow` (StudioPanel), `ValueRow` (PpFigureRow) |
| Button | `BarButton` (PpSessionsSelectionBar), `MlButton` (PpMarkupPanel) |
| Btn | `PipBtn` (PpCameraTiles), `TBtn` (PpReplayTransport) |
| Picker | `ShotPicker` (PpLmGraphicsBody) |
| Flick | `ViewFlick` (PpChartsRegister), `ViewFlick` (PpDiagAnalyse), `ViewFlick` (PpDiagReplay) |
| Foot | `ReviewFoot` (PpDiagAnalyse), `ReviewFoot` (PpDiagReplay) |

## 2. Identical function bodies in different files

| Function(s) | Files | Body (start) |
|---|---:|---|
| `findChild` | 11: `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MicrophonesPanel`, `PhonesPanel`, `StoragePanel`, `StudioPanel` | `for (var i = 0; i < parent.children.length; i++) { var child = parent.children[i] if (chil…` |
| `scrollToItem` | 10: `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MicrophonesPanel`, `PhonesPanel`, `StoragePanel` | `if (!itemId) return true var target = findChild(contentCol, itemId) if (!target) return fa…` |
| `fin` | 6: `PpChPlot`, `PpChPostureAddress`, `PpChPostureProfile`, `PpPosturePage`, `PpReportProfile`, `PpWristPage` | `return v !== null && v !== undefined && isFinite(v)…` |
| `bytesWords` | 3: `ScreenSessions`, `PpSessionReportList`, `PpSessionsSelectionBar` | `if (!(b > 0)) return "0 MB" const gb = b / (1024 * 1024 * 1024) if (gb >= 1) return gb.toF…` |
| `_plural, plural` | 3: `ScreenSessions`, `PpSessionReportList`, `PpSessionsSelectionBar` | `return n === 1 ? one.arg(n) : many.arg(n)…` |
| `seekTo` | 3: `PpChDeliveryShotSection`, `PpChPostureShotSection`, `PpChWristShotSection` | `if (us >= 0 && typeof shotReplay !== "undefined" && shotReplay) shotReplay.seekToUs(us)…` |
| `_folder` | 3: `PpChartsPanel`, `PpTrustPanel`, `PpTrustSummaryFold` | `return String(p // "").replace(/\\/g, "/").replace(/\/+/g, "/").replace(/\/$/, "")…` |
| `capitalised` | 3: `HmFocus`, `HmGoesTogether`, `HmSwingSummary` | `s = String(s); return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s…` |
| `_typeName` | 3: `MetricDetail`, `MetricLibrary`, `MetricRow` | `switch (t) { case "summary": return qsTr("Summary") case "pointInTime": return qsTr("Point…` |
| `_hasMean` | 3: `PpChartPlot`, `PpChartSummary`, `PpMetricChart` | `return !!(s && s.mean && s.t_us && s.mean.length === s.t_us.length)…` |
| `_meanOf` | 3: `PpChartPlot`, `PpChartSummary`, `PpMetricChart` | `return root._hasMean(s) ? s.mean : s.value…` |
| `rowH` | 2: `PpChDeliveryPicture`, `PpChPosturePicture` | `if (!on) return 0 let a = 0 for (let i = 0; i < frames.length; ++i) a += frames[i].height …` |
| `xOf` | 2: `PpChPlaneLabels`, `PpChPlot` | `return (t - x0) / Math.max(1e-9, x1 - x0) * width…` |
| `yOf` | 2: `PpChPlaneLabels`, `PpChPlot` | `return height - (v - lo) / Math.max(1e-9, hi - lo) * height…` |
| `vy` | 2: `PpChPostureProfile`, `PpChWristProfile` | `return topY + pf.plotH * (1 - (v - lo) / Math.max(1e-9, hi - lo))…` |
| `vy` | 2: `PpChReadingTrace`, `PpChRunBox` | `return pTop + (1 - (v - lo) / Math.max(1e-9, hi - lo)) * plotH…` |
| `planeLayersOf` | 2: `PpChSessionFigure`, `PpChTraceFigure` | `return (planeLayers && key && planeLayers[key]) // []…` |
| `withPlane` | 2: `PpChSessionFigure`, `PpChTraceFigure` | `let a = lo, b = hi for (const l of (ls // [])) if (l.k === "hline" && l.v !== null && l.v …` |
| `byKey` | 2: `PpChSessionFigure`, `PpChTraceFigure` | `for (let i = 0; i < (list // []).length; ++i) if (list[i].key === key) return list[i]; ret…` |
| `_pointAtSession` | 2: `PpChartsPanel`, `PpTrustPanel` | `if (_folder(sessionDir) !== _folder(seriesModel.sessionDir)) seriesModel.activateSession(s…` |
| `onContentYChanged` | 2: `PpChartsPanel`, `PpSessionDiagnosticsPanel` | `if (root._pendingScroll[modelData] === undefined) scrollSave.restart()…` |
| `firePulse` | 2: `PpDiagAnalyse`, `PpDiagReplay` | `const c = root.pulseCue if (!c) { br.pulseHold = false; br.pulseIds = []; _pulseToken = -1…` |
| `list, listOf` | 2: `PpDiagPrint`, `PpQuestions` | `return xs.length <= 1 ? xs.join("") : xs.slice(0, -1).join(", ") + " and " + xs[xs.length …` |
| `_missCandidates, missCandidates` | 2: `PpDiagReplayState`, `PpSessionDiagnosticsBody` | `if (source && typeof source.missCandidates === "function") return source.missCandidates() …` |
| `byId` | 2: `PpDiagReplayState`, `PpTypeset` | `for (let i = 0; i < (list // []).length; ++i) if (list[i].id === id) return list[i]; retur…` |
| `faultFor` | 2: `HmFocus`, `HmSwingSummary` | `const c = root.controller if (!c // !conditionId // typeof c.faultDetail !== "function") r…` |
| `xOf` | 2: `PpLmAppendix`, `PpNumbersTable` | `let x = 0; for (let i = 0; i < c; ++i) x += widths.cols[i] + widths.gap; return x…` |
| `_hasDomain` | 2: `PpChartPlot`, `PpSegmentBrush` | `return s.validFromUs !== undefined && s.validToUs !== undefined && s.validToUs > s.validFr…` |
| `_measured` | 2: `PpChartPlot`, `PpSegmentBrush` | `if (root._hasDomain(s) && (s.t_us[i] < s.validFromUs // s.t_us[i] > s.validToUs)) return f…` |
| `_nearestIndex` | 2: `PpChartPlot`, `PpMetricChart` | `var tt = s.t_us if (!tt // tt.length === 0) return -1 var best = -1, bd = Infinity for (va…` |

## 3. Same function name in 3+ files

| Function | Files | In |
|---|---:|---|
| `scrollToItem` | 13 | `DiagnosticModel`, `MetricLibrary`, `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel` … |
| `findChild` | 11 | `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MicrophonesPanel` … |
| `fin` | 8 | `PpChPlot`, `PpChPostureAddress`, `PpChPostureProfile`, `PpPosturePage`, `PpReportProfile`, `PpWristPage`, `PpLmGraphicsBody`, `PpBoxPlot` |
| `xOf` | 7 | `PpChPlaneLabels`, `PpChPlot`, `PpChSeqSession`, `PpChTraceFigure`, `PpChWindowBrush`, `PpLmAppendix`, `PpNumbersTable` |
| `enter` | 7 | `WizardPage`, `BallPage`, `CalibrateArmPage`, `CalibrateTrunkPage`, `CheckArmPage`, `CheckTrunkPage`, `FramingPage` |
| `px` | 6 | `PpCameraFrame`, `PpChMovesWith`, `PpChScatter`, `RingMenu`, `PpWristPage`, `PpBoxPlot` |
| `seekTo` | 6 | `PpChDeliveryShotSection`, `PpChPostureShotSection`, `PpChReadingTrace`, `PpChSessionFigure`, `PpChTraceFigure`, `PpChWristShotSection` |
| `vy` | 6 | `PpChKindCurve`, `PpChPostureProfile`, `PpChReadingTrace`, `PpChRunBox`, `PpChWristProfile`, `PpReportProfile` |
| `leave` | 6 | `WizardPage`, `BallPage`, `CalibrateArmPage`, `CalibrateTrunkPage`, `CheckArmPage`, `FramingPage` |
| `begin` | 5 | `CameraCalibrationFlow`, `HackMotionArmRoutine`, `ImuCalibrationFlow`, `TrunkRoutine`, `WitmotionArmRoutine` |
| `reset` | 5 | `CameraCalibrationFlow`, `HackMotionArmRoutine`, `ImuCalibrationFlow`, `WitmotionArmRoutine`, `SetupDraft` |
| `_log` | 5 | `HackMotionArmRoutine`, `ImuCalibrationFlow`, `TrunkRoutine`, `WitmotionArmRoutine`, `SetupFlow` |
| `py` | 5 | `PpCameraFrame`, `PpChScatter`, `RingMenu`, `PpBoxStrip`, `PpHistoryStrip` |
| `rowH` | 4 | `PpChDeliveryPicture`, `PpChPosturePicture`, `PpChWristProfile`, `PpDotPlot` |
| `picked` | 4 | `PpChPostureShotSection`, `PpChWristShotSection`, `PpChartsSessionView`, `PpChartsShotView` |
| `applyDemo` | 4 | `PpChartsBody`, `PpChartsRegister`, `PpDiagAnalyse`, `PpDiagReplay` |
| `_folder` | 4 | `PpChartsPanel`, `PpSessionDiagnosticsPanel`, `PpTrustPanel`, `PpTrustSummaryFold` |
| `open` | 4 | `MetricRoute`, `ScreenSessionSetup`, `SetupFlow`, `TrustFocus` |
| `_meanOf` | 4 | `PpChartPlot`, `PpChartSummary`, `PpMetricChart`, `PpSegmentBrush` |
| `bytesWords` | 3 | `ScreenSessions`, `PpSessionReportList`, `PpSessionsSelectionBar` |
| `cancel` | 3 | `CalibrationGuide`, `PpPdfPrinter`, `PpSessionBulkJobs` |
| `_ms` | 3 | `HackMotionArmRoutine`, `TrunkRoutine`, `WitmotionArmRoutine` |
| `stop` | 3 | `HackMotionArmRoutine`, `TrunkRoutine`, `WitmotionArmRoutine` |
| `restore` | 3 | `HackMotionArmRoutine`, `TrunkRoutine`, `WitmotionArmRoutine` |
| `_fmt` | 3 | `TrunkRoutine`, `NormativeBar`, `PpReplayTransport` |
| `_finish` | 3 | `TrunkRoutine`, `PpPdfPrinter`, `PpSessionBulkJobs` |
| `_phaseUs` | 3 | `PpCameraFrame`, `PpChartSummary`, `PpMetricChart` |
| `pick` | 3 | `PpChDeliveryShotSection`, `ModelInspector`, `StudioPanel` |
| `cx` | 3 | `PpChPostureProfile`, `PpChWristProfile`, `PpReportProfile` |
| `sx` | 3 | `PpChRunBox`, `PpOrderPage`, `PpMarkupPanel` |
| `openCondition` | 3 | `PpChartsBody`, `PpDiagAnalyse`, `PpDiagReplay` |
| `_pointAtSession` | 3 | `PpChartsPanel`, `PpSessionDiagnosticsPanel`, `PpTrustPanel` |
| `pickView` | 3 | `PpChartsRegister`, `PpDiagAnalyse`, `PpDiagReplay` |
| `select` | 3 | `PpFilePicker`, `DiagnosticModel`, `HmHomeTabs` |
| `num` | 3 | `PpBoxStrip`, `PpLmGraphicsBody`, `PpTypeset` |
| `label` | 3 | `PpDotPlot`, `ImuMounts`, `PpLmGraphicsBody` |
| `faultFor` | 3 | `HmAreaRow`, `HmFocus`, `HmSwingSummary` |
| `capitalised` | 3 | `HmFocus`, `HmGoesTogether`, `HmSwingSummary` |
| `gap` | 3 | `PpOrderPage`, `PpWorkOnPage`, `Theme` |
| `_typeName` | 3 | `MetricDetail`, `MetricLibrary`, `MetricRow` |
| `_hasMean` | 3 | `PpChartPlot`, `PpChartSummary`, `PpMetricChart` |
| `recalibrate` | 3 | `CalibrateTrunkPage`, `CheckArmPage`, `CheckTrunkPage` |

## 4. Whole-file similarity

Jaccard similarity of 4-token shingles after stripping comments/strings/numbers; *containment* = share of the smaller file found in the other. A tiny file fully contained in a large one (e.g. `SetupTogglePill`) is a *copy of a primitive*, not a copy of the large file.

| File A | File B | Jaccard | Containment | Lines A / B |
|---|---|---:|---:|---|
| `charts/PpChDeliveryPicture.qml` | `charts/PpChPosturePicture.qml` | 0.619 | 0.807 | 66 / 68 |
| `charts/PpChPostureLedger.qml` | `charts/PpChWristLedger.qml` | 0.617 | 0.809 | 70 / 88 |
| `athlete/PpAthleteCard.qml` | `athlete/PpAthleteRow.qml` | 0.462 | 0.693 | 91 / 134 |
| `charts/PpChPostureProfile.qml` | `charts/PpChWristProfile.qml` | 0.451 | 0.636 | 253 / 249 |
| `diagnosticmodel/ModelCausePicker.qml` | `diagnosticmodel/ModelPicker.qml` | 0.351 | 0.781 | 235 / 112 |
| `settings/ArchivingPanel.qml` | `settings/LaunchMonitorPanel.qml` | 0.342 | 0.522 | 282 / 445 |
| `charts/PpChartsPanel.qml` | `diagnostics/PpSessionDiagnosticsPanel.qml` | 0.329 | 0.633 | 320 / 236 |
| `setup/pages/CheckArmPage.qml` | `setup/pages/CheckTrunkPage.qml` | 0.329 | 0.672 | 129 / 148 |
| `diagnosticmodel/ModelPolicyPicker.qml` | `diagnosticmodel/ModelTools.qml` | 0.307 | 0.639 | 71 / 158 |
| `cameras/PpCameraPanel.qml` | `imu/PpImuPanel.qml` | 0.304 | 0.512 | 346 / 296 |
| `settings/LaunchMonitorPanel.qml` | `settings/StoragePanel.qml` | 0.289 | 0.606 | 445 / 871 |
| `components/PpTypePill.qml` | `diagnosticmodel/ModelChipRow.qml` | 0.221 | 0.64 | 21 / 55 |
| `setup/pages/CheckArmPage.qml` | `setup/parts/SetupStepIntro.qml` | 0.176 | 0.634 | 129 / 33 |
| `charts/PpChTrustFold.qml` | `trust/PpTrustSummaryFold.qml` | 0.171 | 0.647 | 28 / 82 |
| `setup/pages/ReadyPage.qml` | `setup/parts/SetupSummaryRow.qml` | 0.123 | 0.609 | 266 / 32 |
| `components/PpToolPill.qml` | `session/PpSessionToolbar.qml` | 0.12 | 0.743 | 75 / 641 |
| `setup/pages/ReadyPage.qml` | `setup/parts/SetupStepIntro.qml` | 0.112 | 0.695 | 266 / 33 |
| `setup/pages/CheckTrunkPage.qml` | `setup/parts/SetupStepIntro.qml` | 0.107 | 0.634 | 148 / 33 |
| `diagnosticmodel/ModelEdits.qml` | `diagnosticmodel/ModelInspector.qml` | 0.103 | 0.605 | 119 / 855 |
| `settings/LaunchMonitorPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.099 | 0.942 | 445 / 24 |
| `settings/ArchivingPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.094 | 0.942 | 282 / 24 |
| `session/PpViewPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.081 | 0.653 | 283 / 24 |
| `diagnosticmodel/ModelChipRow.qml` | `diagnosticmodel/ModelInspector.qml` | 0.066 | 0.617 | 55 / 855 |
| `athlete/AthleteClubsSection.qml` | `setup/parts/SetupTogglePill.qml` | 0.064 | 0.769 | 440 / 24 |
| `settings/MicrophonesPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.063 | 1.0 | 634 / 24 |
| `settings/StoragePanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.062 | 1.0 | 871 / 24 |
| `settings/DisplaysPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.058 | 0.727 | 705 / 24 |
| `cameras/PpCameraPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.052 | 0.81 | 346 / 24 |
| `session/PpMotionPanel.qml` | `setup/parts/SetupTogglePill.qml` | 0.048 | 0.645 | 393 / 24 |
| `monitor/RmWarningNotice.qml` | `settings/ImusPanel.qml` | 0.047 | 0.66 | 43 / 1329 |

## 5. Shared code blocks (≥ 7 normalised lines)

Runs of identical code (after normalising strings/numbers/whitespace) shared by the same set of files. `Runs` = distinct runs found for that file set; `Longest` = the longest run in lines.

| Files | Runs | Longest | Sample (first lines of longest) |
|---|---:|---:|---|
| `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel` (+5) | 3 | 14 | `property string lastHighlightId: "S" ⏎ function findChild(parent, name) { ⏎ for (var i = N; i < parent.children.length; i++) {` |
| `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel` (+4) | 3 | 12 | `return null ⏎ } ⏎ function scrollToItem(itemId) {` |
| `AppearancePanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel` (+1) | 7 | 13 | `RowLayout { ⏎ objectName: "S" ⏎ Layout.fillWidth: true` |
| `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel` (+2) | 5 | 8 | `} ⏎ } ⏎ RowLayout {` |
| `AppearancePanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel` (+3) | 1 | 7 | `ColumnLayout { ⏎ id: contentCol ⏎ x: Theme.sp(N)` |
| `PpChDeliveryShotSection`, `PpChPostureShotSection`, `PpChWristShotSection`, `PpChartsKindsView`, `PpChartsReadingsView`, `PpChartsRelationsView` (+2) | 1 | 7 | `objectName: "S" ⏎ property QtObject roles: null ⏎ property var brush: null` |
| `BallPage`, `FramingPage`, `ImusPage`, `IntentPage`, `TriangulatePage` | 1 | 8 | `anchors { left: parent.left; right: parent.right; top: paren ⏎ spacing: Theme.gap(N) ⏎ SetupStepIntro {` |
| `PpChartsKindsView`, `PpChartsReadingsView`, `PpChartsRelationsView`, `PpChartsSessionView`, `PpChartsShotView` | 1 | 7 | `property QtObject roles: null ⏎ property var brush: null ⏎ property var body: null` |
| `AppearancePanel`, `CamerasPanel`, `DisplaysPanel`, `ImusPanel`, `PhonesPanel` | 1 | 7 | `} ⏎ property string lastHighlightId: "S" ⏎ function findChild(parent, name) {` |
| `AppearancePanel`, `DisplaysPanel`, `ImusPanel`, `PhonesPanel`, `StoragePanel` | 1 | 7 | `interval: N ⏎ onTriggered: { ⏎ var target = findChild(contentCol, lastHighlightId)` |
| `CamerasPanel`, `ImusPanel`, `MicrophonesPanel`, `StoragePanel`, `SetupTogglePill` | 1 | 7 | `x: pill.checked ? parent.width - width - Theme.sp(N) : Theme ⏎ Behavior on x { NumberAnimation { duration: N } } ⏎ }` |
| `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `StoragePanel` | 4 | 7 | `Layout.alignment: Qt.AlignVCenter ⏎ } ⏎ }` |
| `PpCameraFrame`, `CamerasPanel` | 1 | 17 | `var h = hr ⏎ if (Math.abs(mx - rx) < h && Math.abs(my - ry) < h) return " ⏎ if (Math.abs(mx - (rx+rw)) < h && Math.abs(my - ry) < h) ret` |
| `CamerasPanel`, `ImusPanel`, `PhonesPanel` | 3 | 11 | `anchors { ⏎ left: parent.left ⏎ right: parent.right` |
| `HackMotionArmRoutine`, `TrunkRoutine`, `WitmotionArmRoutine` | 1 | 10 | `scale: N ⏎ }) ⏎ property var pace: defaultPace` |
| `ModelEdits`, `ModelUnsaved`, `CamerasPanel`, `ImusPanel` | 1 | 7 | `RowLayout { ⏎ Layout.fillWidth: true ⏎ Layout.leftMargin: Theme.gap(N)` |
| `ModelInspector`, `ModelUnsaved`, `CamerasPanel`, `ImusPanel` | 1 | 7 | `} ⏎ RowLayout { ⏎ Layout.fillWidth: true` |
| `MetricDetail`, `MetricLibrary`, `MetricRow` | 1 | 9 | `function _typeName(t) { ⏎ switch (t) { ⏎ case "S": return qsTr("S")` |
| `PpChartPlot`, `PpSegmentBrush` | 3 | 13 | `var i0 = N ⏎ while (i0 < solid.length) { ⏎ var i1 = i0` |
| `PpChPostureProfile`, `PpChWristProfile` | 5 | 12 | `Item { ⏎ id: pf ⏎ objectName: "S"` |
| `PpChartsPanel`, `PpSessionDiagnosticsPanel` | 5 | 12 | `_pendingScroll = Object.assign({}, nav.scroll // {}) ⏎ _scrollTries = {} ⏎ if (Object.keys(_pendingScroll).length > N) scrollRestore.st` |
| `PpDiagAnalyse`, `PpDiagReplay` | 4 | 12 | `} ⏎ property bool missOpen: false ⏎ property int _pulseToken: -N` |
| `ArchivingPanel`, `StudioPanel` | 1 | 12 | `color: Theme.colorText3 ⏎ } ⏎ component RowTitle: ColumnLayout {` |
| `CheckArmPage`, `CheckTrunkPage` | 2 | 12 | `ColumnLayout { ⏎ anchors.fill: parent ⏎ anchors.margins: Theme.gap(N)` |
| `PpChDeliveryShotSection`, `PpChPostureShotSection`, `PpChWristShotSection` | 1 | 8 | `property QtObject roles: null ⏎ property var brush: null ⏎ property var body: null` |
| `CamerasPanel`, `LaunchMonitorPanel`, `StoragePanel` | 1 | 8 | `property bool searchHighlight: false ⏎ Rectangle { x: -Theme.sp(N); y: -Theme.sp(N); width: parent. ⏎ ColumnLayout {` |
| `PpChartPlot`, `PpMetricChart` | 1 | 11 | `} ⏎ function _nearestIndex(s, t) { ⏎ var tt = s.t_us` |
| `ModelEdits`, `ModelTypeRail`, `ImusPanel` | 1 | 7 | `spacing: N ⏎ RowLayout { ⏎ Layout.fillWidth: true` |
| `ModelEdits`, `ModelUnsaved`, `ImusPanel` | 1 | 7 | `Layout.fillWidth: true ⏎ Layout.leftMargin: Theme.gap(N) ⏎ Layout.rightMargin: Theme.gap(N)` |
| `ModelInspector`, `ModelUnsaved`, `ImusPanel` | 1 | 7 | `opacity: Theme.borderOpacityNormal ⏎ } ⏎ RowLayout {` |
