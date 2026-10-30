# The PinPointStudio QML module, declared ONCE and consumed TWICE.
#
# The app compiles this module into its binary; `qml_ui_test` compiles the same module into
# itself so it can press real components without building the app. Before this file existed
# the test borrowed the app's module through a staged, rewritten copy — which is why it could
# only run from an app build, and why it ran in NO release gate on any platform.
#
# ⚠ THESE THREE LISTS MOVE TOGETHER OR THE SUITE FAILS IN WAYS THAT DO NOT RESEMBLE THE CAUSE.
# That is the whole reason they are in one file rather than three places.
#
#   PP_QML_FILES        the .qml sources
#   PP_QML_SINGLETONS   which of them are `pragma Singleton`
#   PP_QML_SOURCES      the C++ that registers QML_ELEMENT types into the module
#
# The singleton list is the one that bites. Omit it and everything still builds, every C++
# type still resolves, the module still imports — and seventy tests fail with
# "Property 'sp' of object Theme is not a function", which points nowhere near the cause.
# It sat twelve lines above qt_add_qml_module in the root CMakeLists, close enough to look
# like part of it and far enough to be left behind. It was, once, and cost an afternoon.
#
# ⚠ ADDING A .qml FILE: add it to PP_QML_FILES, and if it carries `pragma Singleton` add it to
# PP_QML_SINGLETONS as well. Adding a C++ QML type: add it to PP_QML_SOURCES, and check the
# test target still links — the test compiles these sources itself and does not have the app's
# link closure behind it.
#
# Paths are relative to the repo root; each consumer prefixes them with its own.

# `pragma Singleton` files. Qt needs QT_QML_SINGLETON_TYPE on each or the type registers as
# an ordinary component and every `Theme.foo()` call becomes a property lookup on nothing.
set(PP_QML_SINGLETONS
    src/Gui/theme/Theme.qml
    src/Gui/components/ClubFormat.qml
    src/Gui/shot/DataWarningFormat.qml
    src/Gui/settings/SettingsIndex.qml
    src/Gui/session/ViewLayout.qml
    src/Gui/session/SessionMode.qml
    src/Gui/trust/TrustFocus.qml
    src/Gui/cameras/AnnotationTool.qml
    src/Gui/review/MetricRoute.qml
    src/Gui/imu/ImuMounts.qml
)

set(PP_QML_FILES
    src/Gui/theme/Theme.qml
    src/Gui/components/ClubFormat.qml
    src/Gui/shot/DataWarningFormat.qml
    src/Gui/session/ViewLayout.qml
    src/Gui/session/SessionMode.qml
    src/Gui/trust/TrustFocus.qml
    src/Gui/shell/Main.qml
    src/Gui/media/AudioPage.qml
    src/Gui/imu/CapturePage.qml
    src/Gui/viz/ImuVizView.qml
    src/Gui/viz/ArmVizView.qml
    src/Gui/viz/BodyVizView.qml
    src/Gui/viz/View3DKeepAlive.qml
    src/Gui/swing3d/SwingViz3DView.qml
    src/Gui/swing3d/SwingViz3DHost.qml
    src/Gui/media/FilmPage.qml
    src/Gui/cameras/VideoPage.qml
    src/Gui/cameras/PpCameraFrame.qml
    src/Gui/cameras/AutoAnnotations.js
    src/Gui/cameras/AnnotationTool.qml
    src/Gui/cameras/PpAnnotationIcon.qml
    src/Gui/cameras/PpAnnotationLayer.qml
    src/Gui/cameras/PpAnnotationToolbar.qml
    src/Gui/components/PpSegmentedControl.qml
    src/Gui/session/PpStagePanel.qml
    src/Gui/session/PpViewPanel.qml
    src/Gui/session/PpMotionPanel.qml
    src/Gui/session/PpClubPanel.qml
    src/Gui/cameras/PpCameraTiles.qml
    src/Gui/session/PpModeStage.qml
    src/Gui/review/PpReplayCharts.qml
    src/Gui/review/PpTransitTimeline.qml
    src/Gui/review/PpReplayTransport.qml
    src/Gui/shell/PpRail.qml
    src/Gui/shell/PpRailButton.qml
    src/Gui/components/PpDivider.qml
    # The coaching-card pieces (design system §13), shared by home and the session panels.
    src/Gui/components/PpCardShell.qml
    src/Gui/components/PpCard.qml
    src/Gui/components/PpTableRow.qml
    src/Gui/components/PpRowAction.qml
    src/Gui/components/PpConfirm.qml
    src/Gui/components/PpCheckRow.qml
    src/Gui/components/PpExpandRow.qml
    src/Gui/components/PpBandBar.qml
    src/Gui/components/PpStageCard.qml
    src/Gui/components/PpPopoverCard.qml
    src/Gui/components/PpMicro.qml
    src/Gui/components/PpBadge.qml
    src/Gui/components/PpMeter.qml
    src/Gui/components/PpPip.qml
    src/Gui/components/PpPips.qml
    src/Gui/components/PpChip.qml
    src/Gui/components/PpDeviceFilterBar.qml
    src/Gui/components/PpDeviceTags.qml
    src/Gui/components/PpStrengthMark.qml
    src/Gui/components/PpSwingTimeline.qml
    src/Gui/components/PpInset.qml
    src/Gui/components/PpLink.qml
    src/Gui/components/PpCardNote.qml
    src/Gui/components/PpFact.qml
    src/Gui/shell/PpHeader.qml
    src/Gui/shell/PpUpdateBanner.qml
    src/Gui/shell/PpDetectCluster.qml
    src/Gui/shell/PpAnalysingBadge.qml
    src/Gui/shell/PlayPage.qml
    src/Gui/athlete/ScreenAthleteForm.qml
    src/Gui/athlete/ScreenSessions.qml
    src/Gui/athlete/AthleteClubsSection.qml
    src/Gui/athlete/ScreenAthletePicker.qml
    src/Gui/components/PpTextField.qml
    src/Gui/components/PpComboBox.qml
    src/Gui/components/PpChipGroup.qml
    src/Gui/components/PpMetricSwatches.qml
    src/Gui/components/PpUnitToggle.qml
    src/Gui/components/PpButton.qml
    src/Gui/components/PpConnectingFrame.qml
    src/Gui/components/PpPressable.qml
    src/Gui/components/PpDisplayText.qml
    src/Gui/athlete/PpAthleteCard.qml
    src/Gui/athlete/PpAthleteRow.qml
    src/Gui/monitor/ScreenResourceMonitor.qml
    src/Gui/monitor/RmDeviceCard.qml
    src/Gui/monitor/RmSourceRow.qml
    src/Gui/monitor/RmProfilerRow.qml
    src/Gui/monitor/RmStatRow.qml
    src/Gui/monitor/RmAnalysisRunRow.qml
    src/Gui/monitor/RmTimelineChart.qml
    src/Gui/monitor/RmWarningNotice.qml
    # H15 (CR-02) — the PINPOINTCAPTURE tab's two delegates.  They read only
    # what is handed to them, so the offscreen QML suite can stand them up
    # without `ppcpHost` existing at all.
    src/Gui/monitor/RmPpcpPhoneCard.qml
    src/Gui/monitor/RmPpcpCameraRow.qml
    src/Gui/home/ScreenHome.qml
    # H5 — the sessions a connected capture device offers, in the DEVICES area
    # of the home screen.  This is where H3's deleted "File -> Import Session…"
    # menu item went: no menus, no dialogs, and the user never picks a file.
    src/Gui/home/PpcpOfferList.qml
    # The athlete's swing in plain words, led by one focus (home_themes_design.md); a fault opens
    # its detail in place (HmFaultDetail), on Coaching and on Your swing.
    src/Gui/home/HmFaultDetail.qml
    src/Gui/home/HmSwingSummary.qml
    src/Gui/home/HmFocus.qml
    # Home's tabs and the area table (four-modes refactor, Phase 4).
    src/Gui/home/HmHomeTabs.qml
    src/Gui/home/HmSwingAreas.qml
    src/Gui/home/HmAreaRow.qml
    src/Gui/home/HmRigCapability.qml
    # Home's Matchups tab: what travels together.
    src/Gui/home/HmGoesTogether.qml
    # H6 — "Pair to my phone": the PPCP-RV pairing code as a QR, in a modal
    # opened by the one button in the DEVICES heading.  It replaced
    # PpcpPairPanel.qml, which was an always-visible inline section; the
    # remembered pairings it also carried are now Settings -> Phones, which is
    # where RV 7.4b's "visible and individually revocable" is satisfied.
    src/Gui/home/PpcpPairDialog.qml
    # H10 — RV-6 guided pairing: six digits compared on two screens, with no
    # code carried between them.  ⛔ Its footer deliberately INVERTS this
    # application's Cancel-then-primary-affirmative convention, because 11.7d
    # forbids the affirmative being the default or where a stray tap lands.
    # See the file header before changing anything in it.
    src/Gui/home/PpcpGuidedPairDialog.qml
    src/Gui/home/PpQrGlyph.qml
    src/Gui/settings/ScreenSettings.qml
    src/Gui/settings/AppearancePanel.qml
    src/Gui/settings/GeneralPanel.qml
    src/Gui/settings/DisplaysPanel.qml
    src/Gui/settings/CamerasPanel.qml
    src/Gui/settings/ImusPanel.qml
    src/Gui/settings/PhonesPanel.qml
    src/Gui/settings/MicrophonesPanel.qml
    src/Gui/settings/StoragePanel.qml
    src/Gui/settings/ArchivingPanel.qml
    src/Gui/settings/LaunchMonitorPanel.qml
    src/Gui/settings/SettingsIndex.qml
    src/Gui/media/CoachPage.qml
    src/Gui/session/PpSessionToolbar.qml
    src/Gui/cameras/PpCameraPanel.qml
    src/Gui/imu/PpImuPanel.qml
    # The mounts by name, and which of them the pickers offer (design §4.12–4.13).
    src/Gui/imu/ImuMounts.qml
    src/Gui/session/ScreenWrist.qml
    src/Gui/calibration/ImuCalibrationFlow.qml
    src/Gui/calibration/CalibrationGuide.qml
    src/Gui/calibration/WitmotionArmRoutine.qml
    src/Gui/calibration/HackMotionArmRoutine.qml
    src/Gui/calibration/TrunkRoutine.qml
    # The routines' spoken instructions (.pragma library, imported relatively by TrunkRoutine and
    # SetupSteps; the ONE place their words live — prepared in advance by the setup shell).
    src/Gui/calibration/spoken_instructions.js
    src/Gui/calibration/ArmCalibrationStatus.qml
    src/Gui/calibration/CameraCalibrationFlow.qml
    # Session setup's engine (design §4.2–4.8): registry, flow, hardware context, draft, the page
    # contract, and the flow's pure decisions (.pragma library, imported relatively by SetupFlow).
    src/Gui/setup/setup_flow.js
    # The closing page's "What this session will record" wording (.pragma library, imported
    # relatively by SetupContext; the ONE place that wording lives).
    src/Gui/setup/setup_capability_rows.js
    src/Gui/setup/capability_facts.js
    src/Gui/setup/StepDescriptor.qml
    src/Gui/setup/WizardPage.qml
    src/Gui/setup/SetupSteps.qml
    src/Gui/setup/SetupContext.qml
    src/Gui/setup/SetupDraft.qml
    src/Gui/setup/SetupFlow.qml
    # Session setup's shell, pages and the parts they share (design §4.2–4.3, §4.9; the shell
    # replaced ScreenSessionWizard.qml, deleted at Stage 5c). Page type names are unique in the
    # module; the parts are Setup…-prefixed because other files still declare inline CheckRow /
    # StepIntro / TogglePill components of their own.
    src/Gui/setup/ScreenSessionSetup.qml
    src/Gui/setup/pages/IntentPage.qml
    src/Gui/setup/pages/CamerasPage.qml
    src/Gui/setup/pages/FramingPage.qml
    src/Gui/setup/pages/TriangulatePage.qml
    src/Gui/setup/pages/BallPage.qml
    src/Gui/setup/pages/ImusPage.qml
    src/Gui/setup/pages/CalibrateArmPage.qml
    src/Gui/setup/pages/CheckArmPage.qml
    src/Gui/setup/pages/CalibrateTrunkPage.qml
    src/Gui/setup/pages/CheckTrunkPage.qml
    src/Gui/setup/pages/ReadyPage.qml
    src/Gui/setup/parts/SetupStepIntro.qml
    src/Gui/setup/parts/SetupCheckRow.qml
    src/Gui/setup/parts/SetupStatusCircle.qml
    src/Gui/setup/parts/SetupTogglePill.qml
    src/Gui/setup/parts/SetupSummaryRow.qml
    src/Gui/components/PpFlowIndicator.qml
    src/Gui/shot/PpShotCarousel.qml
    src/Gui/shot/PpShotActionBar.qml
    src/Gui/shot/PpExportOptionsSheet.qml
    src/Gui/shot/PpShotCard.qml
    src/Gui/shot/PpShotPipRow.qml
    src/Gui/session/PpSessionDrawer.qml
    src/Gui/shot/PpShotFilter.qml
    src/Gui/shot/PpSwingEditPanel.qml
    src/Gui/diagnostics/PpSessionDiagnosticsPanel.qml
    src/Gui/diagnostics/PpSessionDiagnosticsBody.qml
    # The typeset system (design system §15): the generic helper and blocks in src/Gui/typeset/,
    # the Session diagnostics vocabulary and its blocks in src/Gui/diagnostics/. None is a singleton:
    # PpTypeset / PpDiagRoles are per-panel instances (the medium belongs to the instance).
    src/Gui/typeset/PpTypeset.qml
    src/Gui/typeset/PpBrush.qml
    src/Gui/typeset/PpFigureScale.qml
    src/Gui/typeset/PpFolio.qml
    src/Gui/typeset/PpInlinePicker.qml
    src/Gui/typeset/PpMark.qml
    src/Gui/typeset/PpNotes.qml
    src/Gui/typeset/PpPageKey.qml
    src/Gui/typeset/PpPager.qml
    src/Gui/typeset/PpPulseWash.qml
    src/Gui/typeset/PpRunningHead.qml
    src/Gui/typeset/PpSectionHead.qml
    src/Gui/typeset/PpShotStrip.qml
    src/Gui/typeset/PpStandfirst.qml
    src/Gui/typeset/PpTabs.qml
    src/Gui/typeset/PpZonePlot.qml
    src/Gui/diagnostics/PpDiagRoles.qml
    src/Gui/diagnostics/PpBarcode.qml
    src/Gui/diagnostics/PpCauseGraph.qml
    src/Gui/diagnostics/PpDetailBar.qml
    src/Gui/diagnostics/PpWhatChanged.qml
    src/Gui/diagnostics/PpDotPlot.qml
    src/Gui/diagnostics/PpEntryFlow.qml
    src/Gui/diagnostics/PpExpectations.qml
    src/Gui/diagnostics/PpFeatureHead.qml
    src/Gui/diagnostics/PpFigureRow.qml
    src/Gui/diagnostics/PpHistoryStrip.qml
    src/Gui/diagnostics/PpIndexList.qml
    src/Gui/diagnostics/PpKeyFigures.qml
    src/Gui/diagnostics/PpLedgerTable.qml
    src/Gui/diagnostics/PpMasthead.qml
    src/Gui/diagnostics/PpPathChains.qml
    src/Gui/diagnostics/PpQuestions.qml
    src/Gui/diagnostics/PpRunChart.qml
    src/Gui/diagnostics/PpShotRuler.qml
    src/Gui/diagnostics/PpSmallMultiple.qml
    src/Gui/diagnostics/PpSmallMultiples.qml
    src/Gui/diagnostics/PpSparkHead.qml
    src/Gui/diagnostics/PpSparkline.qml
    src/Gui/diagnostics/PpSparkRow.qml
    src/Gui/typeset/PpBoxReading.qml
    src/Gui/typeset/PpBoxPlot.qml
    src/Gui/diagnostics/PpBoxStrip.qml
    # The Session diagnostics registers (§15.6), hosted by PpSessionDiagnosticsBody: the glance
    # (Replay, Capture, the cast), the read (Analyse), the report (Print, through PpPdfPrinter).
    src/Gui/diagnostics/PpDiagReplayState.qml
    src/Gui/diagnostics/PpDiagReplayCold.qml
    src/Gui/diagnostics/PpDiagReplaySessionRows.qml
    src/Gui/diagnostics/PpDiagReplay.qml
    src/Gui/diagnostics/PpDiagAnalyse.qml
    src/Gui/diagnostics/PpDiagPrint.qml
    # The Charts panel (docs/design/charts_panel_traces_relations.md; build/charts-build/STEP2B.md): the
    # wiring (PpChartsPanel, over SessionSeriesModel and the two presenters), the body, the registers
    # and views, and its blocks. All in src/Gui/charts/ for now; the generic ones move to src/Gui/typeset/
    # later. None is a singleton.
    src/Gui/charts/PpChartsPanel.qml
    src/Gui/charts/PpChartsBody.qml
    src/Gui/charts/PpChartsRegister.qml
    src/Gui/charts/PpChartsReplay.qml
    src/Gui/charts/PpChartsAnalyse.qml
    src/Gui/charts/PpChartsShotView.qml
    src/Gui/charts/PpChartsSessionView.qml
    src/Gui/charts/PpChartsRelationsView.qml
    src/Gui/charts/PpChartsKindsView.qml
    src/Gui/charts/PpChartsReadingsView.qml
    src/Gui/charts/PpChRoles.qml
    src/Gui/charts/PpChText.qml
    src/Gui/charts/PpChChoices.qml
    src/Gui/charts/PpChFoldHead.qml
    src/Gui/charts/PpChPlot.qml
    src/Gui/charts/PpChTraceFigure.qml
    src/Gui/charts/PpChSessionFigure.qml
    src/Gui/charts/PpChWindowBrush.qml
    src/Gui/charts/PpChSeqChain.qml
    src/Gui/charts/PpChSeqTable.qml
    src/Gui/charts/PpChSummary.qml
    src/Gui/charts/PpChKeyFigures.qml
    src/Gui/charts/PpChQuestions.qml
    src/Gui/charts/PpChBoxCell.qml
    src/Gui/charts/PpChProfile.qml
    src/Gui/charts/PpChReadingTrace.qml
    src/Gui/charts/PpChRunBox.qml
    src/Gui/charts/PpChScatter.qml
    src/Gui/charts/PpChMovesWith.qml
    src/Gui/charts/PpChWristShotSection.qml
    src/Gui/charts/PpChWristProfile.qml
    src/Gui/charts/PpChWristLedger.qml
    src/Gui/charts/PpChWristSession.qml
    src/Gui/charts/PpChPostureShotSection.qml
    src/Gui/charts/PpChPostureProfile.qml
    src/Gui/charts/PpChPostureLedger.qml
    src/Gui/charts/PpChPostureAddress.qml
    src/Gui/charts/PpChPostureMoved.qml
    src/Gui/charts/PpChPostureTrust.qml
    src/Gui/charts/PpChTrustFold.qml
    src/Gui/charts/PpChPostureSession.qml
    src/Gui/charts/PpChPosturePicture.qml
    src/Gui/charts/PpChPostureFrame.qml
    # the Club delivery sections (docs/design/club_delivery_chart_design.md §10 stage 3)
    src/Gui/charts/PpChDeliveryShotSection.qml
    src/Gui/charts/PpChDeliveryPicture.qml
    src/Gui/charts/PpChDeliverySession.qml
    src/Gui/charts/PpChDeliveryFlight.qml
    src/Gui/charts/PpChShotShapeGrid.qml
    src/Gui/charts/PpChPlaneLabels.qml
    src/Gui/charts/PpChMatrix.qml
    src/Gui/charts/PpChReadingPicker.qml
    src/Gui/charts/PpChSeqSession.qml
    src/Gui/charts/PpChReplaySession.qml
    src/Gui/charts/PpChKindTable.qml
    src/Gui/charts/PpChKindCurve.qml
    src/Gui/session/PpLaunchMonitorPanel.qml
    src/Gui/trust/PpTrustPanel.qml
    src/Gui/trust/PpTrustReading.qml
    src/Gui/trust/PpTrustInline.qml
    src/Gui/trust/PpTrustInlineFor.qml
    src/Gui/trust/PpTrustSummaryFold.qml
    src/Gui/trust/PpGateNote.qml
    src/Gui/session/PpLmGraphicsBody.qml
    src/Gui/shot/PpSessionDiagnosticsWindow.qml
    src/Gui/components/PpQualityPill.qml
    src/Gui/components/PpStarRating.qml
    src/Gui/components/PpTrace.qml
    src/Gui/components/PpToolPill.qml
    src/Gui/components/PpTypePill.qml
    src/Gui/components/PpTopoBackground.qml
    src/Gui/review/PpMetricChart.qml
    src/Gui/review/PpChartPlot.qml
    src/Gui/review/PpSegmentBrush.qml
    src/Gui/review/PpChartSummary.qml
    src/Gui/review/PpSequenceStrip.qml
    src/Gui/review/PpSpeedSelector.qml
    src/Gui/components/PpToast.qml
    src/Gui/components/PpNotificationHost.qml
    src/Gui/review/PpDataViewer.qml
    src/Gui/review/PpCoverageStrip.qml
    src/Gui/review/PpPropertiesPanel.qml
    src/Gui/review/MetricRow.qml
    src/Gui/review/NormativeBar.qml
    src/Gui/review/MetricDetail.qml
    src/Gui/review/MetricLibrary.qml
    src/Gui/review/MetricRoute.qml
    src/Gui/diagnosticmodel/DiagnosticModel.qml
    src/Gui/diagnosticmodel/ModelTypeRail.qml
    src/Gui/diagnosticmodel/ModelTable.qml
    src/Gui/diagnosticmodel/ModelInspector.qml
    src/Gui/diagnosticmodel/ModelTrail.qml
    src/Gui/diagnosticmodel/ModelGraph.qml
    src/Gui/diagnosticmodel/RingMenu.qml
    src/Gui/diagnosticmodel/ModelCausePicker.qml
    src/Gui/diagnosticmodel/ModelPicker.qml
    src/Gui/diagnosticmodel/ModelEdits.qml
    src/Gui/diagnosticmodel/ModelMint.qml
    src/Gui/diagnosticmodel/ModelChipRow.qml
    src/Gui/diagnosticmodel/ModelTools.qml
    src/Gui/diagnosticmodel/ModelBarButton.qml
    src/Gui/diagnosticmodel/ModelEnumField.qml
    src/Gui/diagnosticmodel/ModelPolicyPicker.qml
    src/Gui/diagnosticmodel/ModelUnsaved.qml
    src/Gui/diagnosticmodel/ModelCorridorPlot.qml
    src/Gui/session/PpMarkupPanel.qml
    src/Gui/shell/PpAboutDialog.qml
    src/Gui/shell/MacAboutMenu.qml
    # Print is to PDF: steps a paged register's printPage and feeds PdfDocumentWriter (below).
    # No control opens it yet.
    src/Gui/print/PpPdfPrinter.qml
    # The session report (docs/design/session_report_design.md): the register and its blocks, laid out
    # on PpPager; the print host that lays it on a hidden A4 sheet; the session list with its report
    # links, and the sheet a report is made from.
    src/Gui/report/PpSessionReport.qml
    src/Gui/report/PpReportCover.qml
    src/Gui/report/PpSwingGrid.qml
    src/Gui/report/PpOrderPage.qml
    src/Gui/report/PpWorkOnPage.qml
    src/Gui/report/PpDrillCard.qml
    src/Gui/report/PpStill.qml
    src/Gui/report/PpBallTiles.qml
    src/Gui/report/PpNumbersTable.qml
    src/Gui/report/PpLmAppendix.qml
    src/Gui/report/PpPosturePage.qml
    src/Gui/report/PpReportProfile.qml
    src/Gui/report/PpWristPage.qml
    src/Gui/report/PpDeliveryPage.qml
    src/Gui/report/PpReportAbout.qml
    src/Gui/report/PpReportPrintHost.qml
    src/Gui/report/PpSessionReportList.qml
    src/Gui/report/PpSessionsTableRow.qml
    src/Gui/report/PpSessionRowMenu.qml
    src/Gui/report/PpSessionsSelectionBar.qml
    src/Gui/report/PpSessionBulkJobs.qml
    src/Gui/report/PpReportSheet.qml
    # Settings → Studio (the studio and its one coach) and the in-app file picker it chooses a logo with.
    src/Gui/settings/StudioPanel.qml
    src/Gui/components/PpFilePicker.qml
)

# The C++ types the module registers (QML_ELEMENT). Headers are listed alongside their .cpp
# so AUTOMOC sees them.
set(PP_QML_SOURCES
    # The screen enum (header-only): every screen index is named from it.
    src/Gui/shell/app_screen.h
    src/Audio/ting_player.h
    src/Audio/ting_player.cpp
    src/Video/bayer_video_item.h
    src/Video/bayer_video_item.cpp
    src/Gui/viz/body_pose_adapter.h
    src/Gui/viz/body_pose_adapter.cpp
    src/Gui/swing3d/swing_rig_driver.h
    src/Gui/swing3d/swing_rig_driver.cpp
    src/Gui/swing3d/swing_hull_geometry.h
    src/Gui/swing3d/swing_hull_geometry.cpp
    src/Gui/swing3d/swing_annotation_mesh.h
    src/Gui/swing3d/swing_annotation_mesh.cpp
    src/Gui/swing3d/swing_annotation_geometry.h
    src/Gui/swing3d/swing_annotation_geometry.cpp
    src/Gui/shot/shot_filter_proxy_model.h
    src/Gui/shot/shot_filter_proxy_model.cpp
    src/Gui/review/swing_data_source.h
    src/Gui/review/swing_data_source.cpp
    src/Gui/review/swing_coverage_model.h
    src/Gui/review/swing_coverage_model.cpp
    src/Gui/review/swing_series_model.h
    src/Gui/review/swing_series_model.cpp
    src/Gui/review/timeline_labels.h
    src/Gui/review/timeline_labels.cpp
    src/Gui/review/chart_metrics.h
    src/Gui/review/chart_metrics.cpp
    # The Charts panel's session half (docs/design/charts_panel_traces_relations.md §6.1): the
    # snapshot types and the model that fills them off the GUI thread.
    src/Gui/charts/session_series_types.h
    src/Gui/charts/session_series_model.h
    src/Gui/charts/session_series_model.cpp
    # The session maths (§4, §6.2): pure C++ over the snapshot, P-aligned.
    src/Gui/charts/session_curves.h
    src/Gui/charts/session_curves.cpp
    src/Gui/charts/session_warp.h
    src/Gui/charts/session_warp.cpp
    src/Gui/charts/session_traces.h
    src/Gui/charts/session_traces.cpp
    src/Gui/charts/session_relations.h
    src/Gui/charts/session_relations.cpp
    src/Gui/charts/session_sequence.h
    src/Gui/charts/session_sequence.cpp
    src/Gui/charts/session_bridge.h
    src/Gui/charts/session_bridge.cpp
    src/Gui/charts/charts_words.h
    src/Gui/charts/charts_traces_presenter.h
    src/Gui/charts/charts_traces_presenter.cpp
    src/Gui/charts/charts_relations_presenter.h
    src/Gui/charts/charts_relations_presenter.cpp
    src/Gui/charts/session_wrist.h
    src/Gui/charts/session_wrist.cpp
    # The Posture section's core (posture_chart_design.md stage 2a): pure C++ over the snapshot.
    src/Gui/charts/session_posture.h
    src/Gui/charts/session_posture.cpp
    src/Gui/charts/charts_wrist_presenter.h
    src/Gui/charts/charts_wrist_presenter.cpp
    # The Posture section's presenter (posture_chart_design.md stage 2b): the core above, serialised.
    src/Gui/charts/charts_posture_presenter.h
    src/Gui/charts/charts_posture_presenter.cpp
    # The Club delivery section (club_delivery_chart_design.md stage 2): its pure C++ core over the snapshot, and the
    # presenter that serialises it (with the plot's lines over the swing plane curve).
    src/Gui/charts/session_delivery.h
    src/Gui/charts/session_delivery.cpp
    src/Gui/charts/charts_delivery_presenter.h
    src/Gui/charts/charts_delivery_presenter.cpp
    # The Club delivery picture strip (club_delivery_chart_design.md stage 6): pure C++ over the swing document, the
    # Posture picture's DTL reading (below) with the address line and the shoulder plane frozen at address.
    src/Gui/charts/delivery_picture.h
    src/Gui/charts/delivery_picture.cpp
    # The Posture picture strip (posture_chart_design.md §5.4, stage 5): pure C++ over the swing document —
    # the frames, the frozen coach lines and the band offsets in each frame's pixels. Its closure: the
    # report stills' pure half (which frame, which crop; no decoder), the markup stream rules it picks the
    # camera's video by, the DTL overlay translator (the DTL ball and club at address) and the head track
    # (the face-on inter-ear ruler headLift is measured with). The app lists these too; a target dedups.
    src/Gui/charts/posture_picture.h
    src/Gui/charts/posture_picture.cpp
    src/Gui/report/report_stills_pure.h
    src/Gui/report/report_stills_pure.cpp
    src/Gui/markup/markup_truth.h
    src/Gui/markup/markup_truth.cpp
    src/Gui/shot/dtl_overlay_payload.h
    src/Gui/shot/dtl_overlay_payload.cpp
    src/Body/head_track.h
    src/Body/head_track.cpp
    src/Gui/trust/trust_readings.h
    src/Gui/trust/trust_readings.cpp
    src/Gui/trust/trust_presenter.h
    src/Gui/trust/trust_presenter.cpp
    src/Gui/trust/trust_words.h
    src/Gui/trust/trust_words.cpp
    src/Gui/review/metric_catalog.h
    src/Gui/review/metric_catalog.cpp
    # metric_catalog.cpp's setupSummary() — listed here, not beside the other Metrics sources, so
    # every consumer of this module (the app and both QML suites) gets it with one line.
    src/Metrics/setup_capability.h
    src/Metrics/setup_capability.cpp
    src/Gui/launchmonitor/lm_session_model.h
    src/Gui/launchmonitor/lm_session_model.cpp
    # The panel's vendored art (qrc:/lm/…, src/Gui/resources/lm/) — a C++ QML singleton.
    src/Gui/launchmonitor/lm_club_art.h
    src/Gui/launchmonitor/lm_club_art.cpp
    src/Gui/diagnosticmodel/model_browser.h
    src/Gui/diagnosticmodel/model_browser.cpp
    src/Diagnostics/measures/anatomy_vocabulary.h
    src/Diagnostics/measures/anatomy_vocabulary.cpp
    src/Diagnostics/measures/measure_facets.h
    src/Diagnostics/measures/measure_facets.cpp
    src/Diagnostics/measures/measure_vocabulary.h
    src/Diagnostics/engine/characteristic.h
    src/Diagnostics/engine/characteristic.cpp
    src/Diagnostics/engine/pack_io.h
    src/Diagnostics/engine/pack_io.cpp
    src/Diagnostics/packs/characteristic_pack.h
    src/Diagnostics/packs/characteristic_pack.cpp
    src/Diagnostics/packs/pack_provider.h
    src/Diagnostics/packs/resource_pack_provider.cpp
    src/Diagnostics/packs/memory_pack_provider.cpp
    src/Diagnostics/norms/memory_norm_provider.cpp
    src/Diagnostics/norms/corridor_plot.h
    src/Diagnostics/norms/corridor_plot.cpp
    src/Diagnostics/packs/file_pack_provider.cpp
    src/Diagnostics/packs/merged_pack_provider.cpp
    src/Diagnostics/engine/characteristic_engine.h
    src/Diagnostics/engine/characteristic_engine.cpp
    src/Diagnostics/engine/relation_resolver.h
    src/Diagnostics/engine/relation_resolver.cpp
    src/Diagnostics/engine/dag_layout.h
    src/Diagnostics/engine/dag_layout.cpp
    src/Diagnostics/norms/norm.h
    src/Diagnostics/norms/norm.cpp
    src/Diagnostics/norms/context_tree.h
    src/Diagnostics/norms/context_tree.cpp
    src/Diagnostics/norms/norm_pack.h
    src/Diagnostics/norms/norm_pack.cpp
    src/Diagnostics/norms/norm_provider.h
    src/Diagnostics/norms/norm_measure_source.h
    src/Diagnostics/norms/metric_corridor.h
    src/Diagnostics/engine/diagnostics_health.h
    src/Diagnostics/engine/diagnostics_health.cpp
    src/Diagnostics/packs/screen_pack.h
    src/Diagnostics/packs/screen_pack.cpp
    src/Diagnostics/packs/drill_pack.h
    src/Diagnostics/packs/drill_pack.cpp
    src/Diagnostics/packs/gate_words_pack.h
    src/Diagnostics/packs/gate_words_pack.cpp
    src/Diagnostics/packs/reference_pack.cpp
    src/Diagnostics/measures/measure_sample.h
    src/Diagnostics/measures/measure_sample.cpp
    src/Diagnostics/measures/live_measure_source.h
    src/Diagnostics/measures/live_measure_source.cpp
    src/Diagnostics/norms/resource_norm_provider.cpp
    src/Diagnostics/norms/file_norm_provider.cpp
    src/Diagnostics/norms/merged_norm_provider.cpp
    src/Gui/diagnostics/wrist_diagnostics_model.h
    src/Gui/diagnostics/wrist_diagnostics_model.cpp
    src/Gui/diagnostics/session_diagnostics_model.h
    src/Gui/diagnostics/session_spread.h
    src/Gui/diagnostics/session_diagnostics_model.cpp
    src/Coaching/work_ons.h
    src/Gui/diagnostics/work_ons_controller.h
    src/Gui/diagnostics/work_ons_controller.cpp
    src/Wrist/reference_bands.h
    src/Wrist/reference_bands.cpp
    src/Wrist/wrist_assessment_engine.h
    src/Wrist/wrist_assessment_engine.cpp
    src/Wrist/wrist_analysis_adapter.h
    src/Wrist/wrist_analysis_adapter.cpp
    src/Wrist/assessment_rule.h
    src/Wrist/assessment_rules.h
    src/Wrist/assessment_rules.cpp
    # Page images -> one A4 PDF (QPdfWriter, Qt Gui only). Its directory must be on every
    # consumer's include path: the generated registration includes it as <pdf_document_writer.h>
    # behind __has_include, so a missing path drops the type silently rather than failing the build.
    src/Gui/print/pdf_document_writer.h
    src/Gui/print/pdf_document_writer.cpp
    # The report's print sheet in a window of its own (QQuickRenderControl), so a print does not stall
    # while the app is out of sight; same include-path rule as the writer above.
    src/Gui/print/offscreen_sheet.h
    src/Gui/print/offscreen_sheet.cpp
    src/Gui/print/busy_activity.h
)
# App Nap is macOS's; elsewhere BusyActivity is inline and empty. Foundation comes with Qt6::Gui (AppKit).
if(APPLE)
    list(APPEND PP_QML_SOURCES src/Gui/print/busy_activity_mac.mm)
endif()

