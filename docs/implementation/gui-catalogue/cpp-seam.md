# C++ ↔ QML seam

How GUI-layer C++ reaches QML. Two mechanisms: **context properties** (global names, set in `src/Gui/main.cpp`) and **`QML_ELEMENT` types** (declared in the module via `PP_QML_SOURCES`).

## Context properties (QML globals)

A QML file refers to these by bare name, so they are *implicit inputs* of every component that mentions them — not visible in the component's property list. `Used in` counts app QML files that mention the name.

| Global | C++ type | Set at | Used in |
|---|---|---|---:|
| `appSettings` | `AppSettings` | `src/Gui/main.cpp:847` | 42 |
| `shotReplay` | `ShotReplayController` | `src/Gui/main.cpp:1102` | 20 |
| `controller` | `TranscriptionController` | `src/Gui/main.cpp:856` | 15 |
| `athleteController` | `AthleteController` | `src/Gui/main.cpp:852` | 14 |
| `cameraManager` | `CameraManager` | `src/Gui/main.cpp:859` | 12 |
| `sessionReviewController` | `SessionReviewController` | `src/Gui/main.cpp:1098` | 11 |
| `shotProcessor` | `ShotProcessor` | `src/Gui/main.cpp:1101` | 11 |
| `imuManager` | `ImuManager` | `src/Gui/main.cpp:854` | 10 |
| `navController` | `NavigationController` | `src/Gui/main.cpp:853` | 9 |
| `ppcpHost` | `PpcpHostService` | `src/Gui/main.cpp:1095` | 8 |
| `sessionController` | `SessionController` | `src/Gui/main.cpp:864` | 6 |
| `workOns` | `WorkOnsController` | `src/Gui/main.cpp:1099` | 6 |
| `appLog` | `AppLog` | `src/Gui/main.cpp:868` | 5 |
| `shotModel` | `ShotListModel` | `src/Gui/main.cpp:1104` | 5 |
| `launchMonitor` | `LaunchMonitorController` | `src/Gui/main.cpp:855` | 4 |
| `clipboard` | `ClipboardHelper` | `src/Gui/main.cpp:1109` | 4 |
| `updateController` | `UpdateController` | `src/Gui/main.cpp:866` | 3 |
| `liveWrist` | `LiveWristAngles` | `src/Gui/main.cpp:1107` | 3 |
| `appInfo` | `AppInfo` | `src/Gui/main.cpp:850` | 2 |
| `llmController` | `LlmController` | `src/Gui/main.cpp:858` | 2 |
| `resourceMonitor` | `ResourceMonitorController` | `src/Gui/main.cpp:862` | 2 |
| `notifications` | `NotificationCenter` | `src/Gui/main.cpp:865` | 2 |
| `shotController` | `ShotController` | `src/Gui/main.cpp:1100` | 2 |
| `currentSwing` | `CurrentSwing` | `src/Gui/main.cpp:1103` | 2 |
| `reanalysisController` | `ReanalysisController` | `src/Gui/main.cpp:1105` | 2 |
| `markupController` | `MarkupController` | `src/Gui/main.cpp:1108` | 2 |
| `libraryConverter` | `LibraryConverter` | `src/Gui/main.cpp:848` | 1 |
| `archiveController` | `ArchiveController` | `src/Gui/main.cpp:849` | 1 |
| `secrets` | `SecretsBridge` | `src/Gui/main.cpp:851` | 1 |
| `ttsController` | `TtsController` | `src/Gui/main.cpp:857` | 1 |
| `filmController` | `FilmController` | `src/Gui/main.cpp:860` | 1 |
| `bufferController` | `BufferController` | `src/Gui/main.cpp:861` | 1 |
| `profiler` | `ProfilerController` | `src/Gui/main.cpp:863` | 1 |
| `cudaRuntime` | `CudaRuntimeController` | `src/Gui/main.cpp:867` | 1 |
| `devTrunkMounts` | `(installed by debug hook)` | `src/Gui/main.cpp:873` | 1 |
| `ppcpOffers` | `(installed by debug hook)` | `src/Gui/main.cpp:905` | 1 |
| `motionCaptureProbe` | `MotionCaptureProbe` | `src/Gui/main.cpp:1097` | 1 |
| `swingExporter` | `swing ZIP exporter` | `src/Gui/main.cpp:1106` | 1 |
| `ppcpImport` | `(installed by debug hook)` | `src/Gui/main.cpp:886` | 0 |

Heaviest consumers of globals (distinct globals mentioned per file):

| Component | Globals used |
|---|---|
| `Main` (910 lines) | 12: appInfo, appSettings, athleteController, cameraManager, navController, notifications, sessionController, sessionReviewController, shotController, shotModel, shotProcessor, shotReplay |
| `PpSessionToolbar` (882 lines) | 9: appSettings, athleteController, cameraManager, imuManager, ppcpHost, sessionController, sessionReviewController, shotModel, shotProcessor |
| `ScreenWrist` (239 lines) | 8: appSettings, currentSwing, navController, sessionController, sessionReviewController, shotModel, shotProcessor, shotReplay |
| `ScreenHome` (782 lines) | 7: appSettings, athleteController, cameraManager, imuManager, ppcpHost, resourceMonitor, workOns |
| `PpCameraTiles` (778 lines) | 6: appSettings, cameraManager, navController, sessionController, shotProcessor, shotReplay |
| `PpChartsPanel` (380 lines) | 6: appSettings, navController, reanalysisController, sessionReviewController, shotProcessor, shotReplay |
| `GeneralPanel` (1162 lines) | 6: appSettings, cudaRuntime, llmController, motionCaptureProbe, secrets, updateController |
| `SetupContext` (661 lines) | 6: appSettings, athleteController, cameraManager, imuManager, launchMonitor, liveWrist |
| `PpDetectCluster` (259 lines) | 6: appSettings, cameraManager, controller, imuManager, launchMonitor, shotController |
| `PpShotCarousel` (725 lines) | 6: appSettings, reanalysisController, sessionReviewController, shotModel, shotReplay, swingExporter |
| `PpLaunchMonitorPanel` (176 lines) | 5: appSettings, athleteController, launchMonitor, sessionReviewController, shotModel |
| `ImuCalibrationFlow` (538 lines) | 4: appLog, appSettings, athleteController, imuManager |
| `DiagnosticModel` (1994 lines) | 4: appSettings, athleteController, clipboard, currentSwing |
| `PpDataViewer` (639 lines) | 4: appSettings, clipboard, navController, shotReplay |
| `ScreenSettings` (592 lines) | 4: appSettings, cameraManager, imuManager, ppcpHost |
| `PpHeader` (296 lines) | 4: appSettings, navController, sessionReviewController, shotProcessor |
| `PpTrustPanel` (197 lines) | 4: navController, sessionReviewController, shotProcessor, shotReplay |
| `PpCameraPanel` (563 lines) | 3: appSettings, cameraManager, ppcpHost |
| `PpSessionDiagnosticsPanel` (431 lines) | 3: appSettings, sessionReviewController, shotProcessor |
| `ScreenSessionMode` (202 lines) | 3: appSettings, navController, shotReplay |

## C++ classes in `src/Gui`

Classes found by scanning headers for `class X : public Base`. `Prop` = `Q_PROPERTY`, `Inv` = `Q_INVOKABLE`, counted per *file* (a file with several classes shares the counts). `QML` marks `QML_ELEMENT`/`QML_NAMED_ELEMENT`.

| Area | Class | Base | Header | Prop | Inv | QML |
|---|---|---|---|---:|---:|:--:|
| app | `AppLog` | `QObject` | `app/app_log.h` | 0 | 2 |  |
| app | `AppSettings` | `QObject` | `app/app_settings.h` | 123 | 5 |  |
| app | `ClipboardHelper` | `QObject` | `app/clipboard_helper.h` | 0 | 1 |  |
| app | `NotificationCenter` | `QAbstractListModel` | `app/notification_center.h` | 1 | 4 | ✓ |
| athlete | `AthleteController` | `QObject` | `athlete/athlete_controller.h` | 9 | 15 |  |
| cameras | `BufferController` | `QObject` | `cameras/buffer_controller.h` | 3 | 0 |  |
| cameras | `CameraInstance` | `QObject` | `cameras/camera_instance.h` | 40 | 18 | ✓ |
| cameras | `CameraManager` | `QObject` | `cameras/camera_manager.h` | 9 | 23 |  |
| cameras | `CameraRoleProbe` | `QObject` | `cameras/camera_role_probe.h` | 0 | 0 |  |
| charts | `ChartsRelationsPresenter` | `QObject` | `charts/charts_relations_presenter.h` | 22 | 5 | ✓ |
| charts | `ChartsTracesPresenter` | `QObject` | `charts/charts_traces_presenter.h` | 23 | 8 | ✓ |
| charts | `ChartsWristPresenter` | `QObject` | `charts/charts_wrist_presenter.h` | 18 | 3 | ✓ |
| charts | `SessionSeriesModel` | `QObject` | `charts/session_series_model.h` | 8 | 8 | ✓ |
| diagnosticmodel | `ModelBrowser` | `QObject` | `diagnosticmodel/model_browser.h` | 30 | 73 | ✓ |
| diagnostics | `SessionDiagnosticsModel` | `QObject` | `diagnostics/session_diagnostics_model.h` | 33 | 25 | ✓ |
| diagnostics | `WorkOnsController` | `QObject` | `diagnostics/work_ons_controller.h` | 18 | 1 |  |
| diagnostics | `WristDiagnosticsModel` | `QObject` | `diagnostics/wrist_diagnostics_model.h` | 21 | 1 | ✓ |
| imu | `HmUnit` | `QObject` | `imu/hm_instance.h` | 38 | 6 |  |
| imu | `HmInstance` | `ImuDeviceBase` | `imu/hm_instance.h` | 38 | 6 |  |
| imu | `ImuDeviceBase` | `QObject` | `imu/imu_device.h` | 0 | 0 |  |
| imu | `ImuInstance` | `ImuDeviceBase` | `imu/imu_instance.h` | 32 | 10 |  |
| imu | `ImuManager` | `QObject` | `imu/imu_manager.h` | 14 | 24 |  |
| imu | `PacedConnectQueue` | `QObject` | `imu/paced_connect_queue.h` | 1 | 0 |  |
| launchmonitor | `LaunchMonitorController` | `QObject` | `launchmonitor/launch_monitor_controller.h` | 10 | 2 |  |
| launchmonitor | `LmClubArt` | `QObject` | `launchmonitor/lm_club_art.h` | 0 | 10 | ✓ |
| launchmonitor | `LmSessionModel` | `QAbstractListModel` | `launchmonitor/lm_session_model.h` | 14 | 3 | ✓ |
| markup | `MarkupController` | `QObject` | `markup/markup_controller.h` | 46 | 26 |  |
| markup | `MarkupImageProvider` | `QQuickImageProvider` | `markup/markup_image_provider.h` | 0 | 0 |  |
| media | `FilmController` | `QObject` | `media/film_controller.h` | 18 | 16 |  |
| media | `LlmController` | `QObject` | `media/llm_controller.h` | 12 | 6 |  |
| media | `TranscriptionController` | `QObject` | `media/transcription_controller.h` | 5 | 6 |  |
| media | `TtsController` | `QObject` | `media/tts_controller.h` | 11 | 4 |  |
| monitor | `ProfilerController` | `QObject` | `monitor/profiler_controller.h` | 31 | 9 |  |
| monitor | `ResourceMonitorController` | `QObject` | `monitor/resource_monitor_controller.h` | 14 | 4 |  |
| print | `PdfDocumentWriter` | `QObject` | `print/pdf_document_writer.h` | 5 | 4 | ✓ |
| review | `ChartMetrics` | `QObject` | `review/chart_metrics.h` | 0 | 29 | ✓ |
| review | `MetricCatalog` | `QObject` | `review/metric_catalog.h` | 3 | 4 | ✓ |
| review | `SessionListModel` | `QAbstractListModel` | `review/session_list_model.h` | 0 | 0 |  |
| review | `SessionReviewController` | `QObject` | `review/session_review_controller.h` | 10 | 5 |  |
| review | `SwingCoverageModel` | `QAbstractListModel` | `review/swing_coverage_model.h` | 0 | 0 | ✓ |
| review | `SwingDataSource` | `QObject` | `review/swing_data_source.h` | 19 | 4 | ✓ |
| review | `SwingSeriesModel` | `QAbstractTableModel` | `review/swing_series_model.h` | 0 | 4 | ✓ |
| review | `TimelineLabels` | `QObject` | `review/timeline_labels.h` | 0 | 9 | ✓ |
| session | `SessionController` | `QObject` | `session/session_controller.h` | 4 | 4 | ✓ |
| settings | `ArchiveController` | `QObject` | `settings/archive_controller.h` | 10 | 6 |  |
| settings | `LibraryConverter` | `QObject` | `settings/library_converter.h` | 6 | 3 |  |
| shell | `NavigationController` | `QObject` | `shell/navigation_controller.h` | 4 | 5 |  |
| shot | `CurrentSwing` | `QObject` | `shot/current_swing.h` | 1 | 0 |  |
| shot | `DiskReplaySource` | `ReplaySource` | `shot/disk_replay_source.h` | 0 | 0 |  |
| shot | `PpcpClipFiler` | `QObject` | `shot/ppcp_clip_filer.h` | 0 | 0 |  |
| shot | `ReanalysisController` | `QObject` | `shot/reanalysis_controller.h` | 1 | 2 |  |
| shot | `ReplaySource` | `QObject` | `shot/replay_source.h` | 0 | 0 |  |
| shot | `ShotController` | `QObject` | `shot/shot_controller.h` | 1 | 6 |  |
| shot | `ShotFilterProxyModel` | `QSortFilterProxyModel` | `shot/shot_filter_proxy_model.h` | 9 | 3 | ✓ |
| shot | `ShotListModel` | `QAbstractListModel` | `shot/shot_list_model.h` | 2 | 14 |  |
| shot | `ShotProcessor` | `QObject` | `shot/shot_processor.h` | 10 | 4 |  |
| shot | `ShotReplayController` | `QObject` | `shot/shot_replay_controller.h` | 16 | 14 |  |
| swing3d | `SwingAnnotationGeometry` | `QQuick3DGeometry` | `swing3d/swing_annotation_geometry.h` | 11 | 0 | ✓ |
| swing3d | `SwingHullGeometry` | `QQuick3DGeometry` | `swing3d/swing_hull_geometry.h` | 6 | 0 | ✓ |
| swing3d | `SwingRigDriver` | `QObject` | `swing3d/swing_rig_driver.h` | 30 | 17 | ✓ |
| trust | `TrustPresenter` | `QObject` | `trust/trust_presenter.h` | 5 | 0 | ✓ |
| trust | `TrustWords` | `QObject` | `trust/trust_words.h` | 0 | 2 | ✓ |
| viz | `BodyPoseAdapter` | `QObject` | `viz/body_pose_adapter.h` | 22 | 0 | ✓ |
| viz | `LiveWristAngles` | `QObject` | `viz/live_wrist_angles.h` | 10 | 0 |  |

## C++ volume by area

| Area | Files | Lines |
|---|---:|---:|
| charts | 23 | 13,191 |
| diagnosticmodel | 2 | 9,069 |
| shot | 24 | 8,603 |
| diagnostics | 7 | 7,413 |
| review | 17 | 5,770 |
| imu | 11 | 5,710 |
| cameras | 10 | 4,848 |
| media | 8 | 2,970 |
| app | 8 | 2,677 |
| launchmonitor | 6 | 2,590 |
| markup | 5 | 1,887 |
| swing3d | 8 | 1,761 |
| monitor | 4 | 1,502 |
| main.cpp | 1 | 1,194 |
| trust | 6 | 996 |
| viz | 4 | 740 |
| athlete | 2 | 725 |
| settings | 4 | 703 |
| print | 2 | 376 |
| session | 2 | 242 |
| shell | 2 | 182 |
| typeset | 1 | 85 |
