# C++ ↔ QML seam

How GUI-layer C++ reaches QML. Two mechanisms: **context properties** (global names, set in `src/Gui/main.cpp`) and **`QML_ELEMENT` types** (declared in the module via `PP_QML_SOURCES`).

## Context properties (QML globals)

A QML file refers to these by bare name, so they are *implicit inputs* of every component that mentions them — not visible in the component's property list. `Used in` counts app QML files that mention the name.

| Global | C++ type | Set at | Used in |
|---|---|---|---:|
| `appSettings` | `AppSettings` | `src/Gui/main.cpp:956` | 46 |
| `shotReplay` | `ShotReplayController` | `src/Gui/main.cpp:1217` | 21 |
| `athleteController` | `AthleteController` | `src/Gui/main.cpp:961` | 17 |
| `controller` | `TranscriptionController` | `src/Gui/main.cpp:966` | 16 |
| `sessionReviewController` | `SessionReviewController` | `src/Gui/main.cpp:1203` | 13 |
| `cameraManager` | `CameraManager` | `src/Gui/main.cpp:969` | 11 |
| `shotProcessor` | `ShotProcessor` | `src/Gui/main.cpp:1216` | 11 |
| `sessionController` | `SessionController` | `src/Gui/main.cpp:974` | 10 |
| `imuManager` | `ImuManager` | `src/Gui/main.cpp:963` | 9 |
| `appLog` | `AppLog` | `src/Gui/main.cpp:978` | 8 |
| `ppcpHost` | `PpcpHostService` | `src/Gui/main.cpp:1200` | 8 |
| `workOns` | `WorkOnsController` | `src/Gui/main.cpp:1204` | 8 |
| `navController` | `NavigationController` | `src/Gui/main.cpp:962` | 7 |
| `trunkCeremony` | `TrunkCeremonyRecorder` | `src/Gui/main.cpp:964` | 5 |
| `launchMonitor` | `LaunchMonitorController` | `src/Gui/main.cpp:965` | 5 |
| `shotModel` | `ShotListModel` | `src/Gui/main.cpp:1219` | 5 |
| `ttsController` | `TtsController` | `src/Gui/main.cpp:967` | 4 |
| `clipboard` | `ClipboardHelper` | `src/Gui/main.cpp:1224` | 4 |
| `archiveController` | `ArchiveController` | `src/Gui/main.cpp:958` | 3 |
| `resourceMonitor` | `ResourceMonitorController` | `src/Gui/main.cpp:972` | 3 |
| `updateController` | `UpdateController` | `src/Gui/main.cpp:976` | 3 |
| `sessionReport` | `SessionReportModel` | `src/Gui/main.cpp:1212` | 3 |
| `sessionReportList` | `SessionReportListModel` | `src/Gui/main.cpp:1213` | 3 |
| `reportMailer` | `ReportMailer` | `src/Gui/main.cpp:1214` | 3 |
| `swingExporter` | `swing ZIP exporter` | `src/Gui/main.cpp:1221` | 3 |
| `liveWrist` | `LiveWristAngles` | `src/Gui/main.cpp:1222` | 3 |
| `appInfo` | `AppInfo` | `src/Gui/main.cpp:959` | 2 |
| `llmController` | `LlmController` | `src/Gui/main.cpp:968` | 2 |
| `notifications` | `NotificationCenter` | `src/Gui/main.cpp:975` | 2 |
| `studioProfile` | `StudioProfile` | `src/Gui/main.cpp:1211` | 2 |
| `shotController` | `ShotController` | `src/Gui/main.cpp:1215` | 2 |
| `currentSwing` | `CurrentSwing` | `src/Gui/main.cpp:1218` | 2 |
| `reanalysisController` | `ReanalysisController` | `src/Gui/main.cpp:1220` | 2 |
| `markupController` | `MarkupController` | `src/Gui/main.cpp:1223` | 2 |
| `libraryConverter` | `LibraryConverter` | `src/Gui/main.cpp:957` | 1 |
| `secrets` | `SecretsBridge` | `src/Gui/main.cpp:960` | 1 |
| `filmController` | `FilmController` | `src/Gui/main.cpp:970` | 1 |
| `bufferController` | `BufferController` | `src/Gui/main.cpp:971` | 1 |
| `profiler` | `ProfilerController` | `src/Gui/main.cpp:973` | 1 |
| `cudaRuntime` | `CudaRuntimeController` | `src/Gui/main.cpp:977` | 1 |
| `ppcpOffers` | `(installed by debug hook)` | `src/Gui/main.cpp:1010` | 1 |
| `motionCaptureProbe` | `MotionCaptureProbe` | `src/Gui/main.cpp:1202` | 1 |
| `ppcpImport` | `(installed by debug hook)` | `src/Gui/main.cpp:991` | 0 |

Heaviest consumers of globals (distinct globals mentioned per file):

| Component | Globals used |
|---|---|
| `Main` (908 lines) | 12: appInfo, appSettings, athleteController, cameraManager, navController, notifications, sessionController, sessionReviewController, shotController, shotModel, shotProcessor, shotReplay |
| `PpSessionReportList` (782 lines) | 9: appSettings, archiveController, athleteController, reportMailer, sessionReport, sessionReportList, sessionReviewController, studioProfile, swingExporter |
| `PpSessionToolbar` (879 lines) | 9: appSettings, athleteController, cameraManager, imuManager, ppcpHost, sessionController, sessionReviewController, shotModel, shotProcessor |
| `ScreenWrist` (236 lines) | 8: appSettings, currentSwing, navController, sessionController, sessionReviewController, shotModel, shotProcessor, shotReplay |
| `SetupContext` (640 lines) | 8: appSettings, athleteController, cameraManager, imuManager, launchMonitor, liveWrist, trunkCeremony, workOns |
| `PpCameraTiles` (778 lines) | 6: appSettings, cameraManager, navController, sessionController, shotProcessor, shotReplay |
| `PpChartsPanel` (424 lines) | 6: appSettings, navController, reanalysisController, sessionReviewController, shotProcessor, shotReplay |
| `ScreenHome` (595 lines) | 6: appSettings, athleteController, resourceMonitor, sessionController, sessionReportList, workOns |
| `GeneralPanel` (1222 lines) | 6: appSettings, cudaRuntime, llmController, motionCaptureProbe, secrets, updateController |
| `PpDetectCluster` (259 lines) | 6: appSettings, cameraManager, controller, imuManager, launchMonitor, shotController |
| `PpShotCarousel` (806 lines) | 6: appSettings, reanalysisController, sessionReviewController, shotModel, shotReplay, swingExporter |
| `PpSessionBulkJobs` (308 lines) | 6: archiveController, reportMailer, sessionReport, sessionReportList, sessionReviewController, swingExporter |
| `PpLaunchMonitorPanel` (177 lines) | 5: appSettings, athleteController, launchMonitor, sessionReviewController, shotModel |
| `ImuCalibrationFlow` (545 lines) | 4: appLog, appSettings, athleteController, imuManager |
| `DiagnosticModel` (2031 lines) | 4: appSettings, athleteController, clipboard, currentSwing |
| `PpSessionDiagnosticsPanel` (487 lines) | 4: appSettings, sessionController, sessionReviewController, shotProcessor |
| `HmHomeTabs` (156 lines) | 4: appSettings, athleteController, controller, workOns |
| `HmRigCapability` (233 lines) | 4: appSettings, launchMonitor, ppcpHost, resourceMonitor |
| `HmSwingAreas` (182 lines) | 4: appSettings, athleteController, controller, workOns |
| `PpDataViewer` (639 lines) | 4: appSettings, clipboard, navController, shotReplay |

## C++ classes in `src/Gui`

Classes found by scanning headers for `class X : public Base`. `Prop` = `Q_PROPERTY`, `Inv` = `Q_INVOKABLE`, counted per *file* (a file with several classes shares the counts). `QML` marks `QML_ELEMENT`/`QML_NAMED_ELEMENT`.

| Area | Class | Base | Header | Prop | Inv | QML |
|---|---|---|---|---:|---:|:--:|
| app | `AppLog` | `QObject` | `app/app_log.h` | 0 | 2 |  |
| app | `AppSettings` | `QObject` | `app/app_settings.h` | 121 | 5 |  |
| app | `ClipboardHelper` | `QObject` | `app/clipboard_helper.h` | 0 | 1 |  |
| app | `NotificationCenter` | `QAbstractListModel` | `app/notification_center.h` | 1 | 4 | ✓ |
| athlete | `AthleteController` | `QObject` | `athlete/athlete_controller.h` | 14 | 19 |  |
| cameras | `BufferController` | `QObject` | `cameras/buffer_controller.h` | 3 | 0 |  |
| cameras | `CameraInstance` | `QObject` | `cameras/camera_instance.h` | 40 | 18 | ✓ |
| cameras | `CameraManager` | `QObject` | `cameras/camera_manager.h` | 9 | 23 |  |
| cameras | `CameraRoleProbe` | `QObject` | `cameras/camera_role_probe.h` | 0 | 0 |  |
| charts | `ChartsDeliveryPresenter` | `QObject` | `charts/charts_delivery_presenter.h` | 14 | 4 | ✓ |
| charts | `ChartsPosturePresenter` | `QObject` | `charts/charts_posture_presenter.h` | 16 | 4 | ✓ |
| charts | `ChartsRelationsPresenter` | `QObject` | `charts/charts_relations_presenter.h` | 22 | 5 | ✓ |
| charts | `ChartsTracesPresenter` | `QObject` | `charts/charts_traces_presenter.h` | 24 | 8 | ✓ |
| charts | `ChartsWristPresenter` | `QObject` | `charts/charts_wrist_presenter.h` | 18 | 3 | ✓ |
| charts | `SessionSeriesModel` | `QObject` | `charts/session_series_model.h` | 8 | 9 | ✓ |
| diagnosticmodel | `ModelBrowser` | `QObject` | `diagnosticmodel/model_browser.h` | 30 | 75 | ✓ |
| diagnostics | `SessionDiagnosticsModel` | `QObject` | `diagnostics/session_diagnostics_model.h` | 33 | 27 | ✓ |
| diagnostics | `WorkOnsController` | `QObject` | `diagnostics/work_ons_controller.h` | 21 | 3 |  |
| diagnostics | `WristDiagnosticsModel` | `QObject` | `diagnostics/wrist_diagnostics_model.h` | 21 | 1 | ✓ |
| imu | `HmUnit` | `QObject` | `imu/hm_instance.h` | 39 | 6 |  |
| imu | `HmInstance` | `ImuDeviceBase` | `imu/hm_instance.h` | 39 | 6 |  |
| imu | `ImuDeviceBase` | `QObject` | `imu/imu_device.h` | 0 | 0 |  |
| imu | `ImuInstance` | `ImuDeviceBase` | `imu/imu_instance.h` | 36 | 11 |  |
| imu | `ImuManager` | `QObject` | `imu/imu_manager.h` | 14 | 24 |  |
| imu | `PacedConnectQueue` | `QObject` | `imu/paced_connect_queue.h` | 1 | 0 |  |
| imu | `TrunkCeremonyRecorder` | `QObject` | `imu/trunk_ceremony_recorder.h` | 4 | 8 |  |
| launchmonitor | `LaunchMonitorController` | `QObject` | `launchmonitor/launch_monitor_controller.h` | 10 | 2 |  |
| launchmonitor | `LmClubArt` | `QObject` | `launchmonitor/lm_club_art.h` | 0 | 10 | ✓ |
| launchmonitor | `LmSessionModel` | `QAbstractListModel` | `launchmonitor/lm_session_model.h` | 15 | 3 | ✓ |
| markup | `MarkupController` | `QObject` | `markup/markup_controller.h` | 46 | 26 |  |
| markup | `MarkupImageProvider` | `QQuickImageProvider` | `markup/markup_image_provider.h` | 0 | 0 |  |
| media | `FilmController` | `QObject` | `media/film_controller.h` | 18 | 16 |  |
| media | `LlmController` | `QObject` | `media/llm_controller.h` | 12 | 6 |  |
| media | `TranscriptionController` | `QObject` | `media/transcription_controller.h` | 5 | 6 |  |
| media | `TtsController` | `QObject` | `media/tts_controller.h` | 11 | 6 |  |
| monitor | `ProfilerController` | `QObject` | `monitor/profiler_controller.h` | 31 | 9 |  |
| monitor | `ResourceMonitorController` | `QObject` | `monitor/resource_monitor_controller.h` | 14 | 4 |  |
| print | `OffscreenSheet` | `QObject` | `print/offscreen_sheet.h` | 2 | 3 | ✓ |
| print | `PdfDocumentWriter` | `QObject` | `print/pdf_document_writer.h` | 7 | 5 | ✓ |
| report | `ReportMailer` | `QObject` | `report/report_mailer.h` | 12 | 11 |  |
| report | `ReportStills` | `QObject` | `report/report_stills.h` | 0 | 0 |  |
| report | `SessionReportListModel` | `DeviceFilterModel` | `report/session_report_list_model.h` | 12 | 2 |  |
| report | `SessionReportModel` | `QObject` | `report/session_report_model.h` | 6 | 7 |  |
| report | `SmtpClient` | `QObject` | `report/smtp_client.h` | 0 | 0 |  |
| review | `ChartMetrics` | `QObject` | `review/chart_metrics.h` | 0 | 29 | ✓ |
| review | `DeviceFilterModel` | `QAbstractListModel` | `review/device_filter_model.h` | 4 | 2 |  |
| review | `MetricCatalog` | `QObject` | `review/metric_catalog.h` | 3 | 4 | ✓ |
| review | `SessionListModel` | `DeviceFilterModel` | `review/session_list_model.h` | 0 | 0 |  |
| review | `SessionReviewController` | `QObject` | `review/session_review_controller.h` | 10 | 5 |  |
| review | `SwingCoverageModel` | `QAbstractListModel` | `review/swing_coverage_model.h` | 0 | 0 | ✓ |
| review | `SwingDataSource` | `QObject` | `review/swing_data_source.h` | 19 | 4 | ✓ |
| review | `SwingSeriesModel` | `QAbstractTableModel` | `review/swing_series_model.h` | 0 | 4 | ✓ |
| review | `TimelineLabels` | `QObject` | `review/timeline_labels.h` | 0 | 9 | ✓ |
| session | `SessionController` | `QObject` | `session/session_controller.h` | 7 | 4 | ✓ |
| settings | `ArchiveController` | `QObject` | `settings/archive_controller.h` | 10 | 6 |  |
| settings | `LibraryConverter` | `QObject` | `settings/library_converter.h` | 6 | 3 |  |
| shell | `AppScreen` | `QObject` | `shell/app_screen.h` | 0 | 0 | ✓ |
| shell | `NavigationController` | `QObject` | `shell/navigation_controller.h` | 3 | 5 |  |
| shot | `CurrentSwing` | `QObject` | `shot/current_swing.h` | 1 | 0 |  |
| shot | `DiskReplaySource` | `ReplaySource` | `shot/disk_replay_source.h` | 0 | 0 |  |
| shot | `PpcpClipFiler` | `QObject` | `shot/ppcp_clip_filer.h` | 0 | 0 |  |
| shot | `ReanalysisController` | `QObject` | `shot/reanalysis_controller.h` | 1 | 2 |  |
| shot | `ReplaySource` | `QObject` | `shot/replay_source.h` | 0 | 0 |  |
| shot | `ShotController` | `QObject` | `shot/shot_controller.h` | 1 | 6 |  |
| shot | `ShotFilterProxyModel` | `QSortFilterProxyModel` | `shot/shot_filter_proxy_model.h` | 9 | 3 | ✓ |
| shot | `ShotListModel` | `QAbstractListModel` | `shot/shot_list_model.h` | 2 | 15 |  |
| shot | `ShotProcessor` | `QObject` | `shot/shot_processor.h` | 10 | 4 |  |
| shot | `ShotReplayController` | `QObject` | `shot/shot_replay_controller.h` | 16 | 13 |  |
| swing3d | `SwingAnnotationGeometry` | `QQuick3DGeometry` | `swing3d/swing_annotation_geometry.h` | 11 | 0 | ✓ |
| swing3d | `SwingHullGeometry` | `QQuick3DGeometry` | `swing3d/swing_hull_geometry.h` | 6 | 0 | ✓ |
| swing3d | `SwingRigDriver` | `QObject` | `swing3d/swing_rig_driver.h` | 30 | 17 | ✓ |
| trust | `TrustPresenter` | `QObject` | `trust/trust_presenter.h` | 5 | 0 | ✓ |
| trust | `TrustWords` | `QObject` | `trust/trust_words.h` | 0 | 2 | ✓ |
| viz | `BodyPoseAdapter` | `QObject` | `viz/body_pose_adapter.h` | 22 | 2 | ✓ |
| viz | `LiveWristAngles` | `QObject` | `viz/live_wrist_angles.h` | 10 | 0 |  |

## C++ volume by area

| Area | Files | Lines |
|---|---:|---:|
| charts | 37 | 23,327 |
| diagnosticmodel | 2 | 9,836 |
| report | 25 | 9,114 |
| shot | 24 | 8,735 |
| diagnostics | 7 | 8,362 |
| imu | 13 | 6,792 |
| review | 19 | 6,068 |
| cameras | 10 | 4,958 |
| media | 9 | 3,411 |
| launchmonitor | 6 | 2,694 |
| app | 8 | 2,671 |
| markup | 5 | 1,887 |
| swing3d | 8 | 1,761 |
| print | 5 | 1,710 |
| monitor | 4 | 1,502 |
| main.cpp | 1 | 1,309 |
| trust | 6 | 1,016 |
| athlete | 2 | 888 |
| viz | 4 | 772 |
| settings | 4 | 746 |
| session | 2 | 291 |
| shell | 3 | 206 |
| typeset | 1 | 85 |
