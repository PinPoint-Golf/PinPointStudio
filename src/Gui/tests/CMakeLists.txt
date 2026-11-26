# CTest suite for src/Gui model/helper logic — TimelineLabels, SwingSeriesModel,
# ShotListModel and ReanalysisController. Self-contained — NOT part of the app
# build. Runs standalone or under the tests/ umbrella; shared infra lives in
# tests/cmake/PinPointTests.cmake.
#
#   cmake -S src/Gui/tests -B build/gui-tests
#   cmake --build build/gui-tests -j && ctest --test-dir build/gui-tests --output-on-failure
#
# Qt6::Qml is required because the Gui headers use QML_ELEMENT (qqmlregistration.h);
# the QML_ELEMENT classes carry Q_OBJECT, so every target enables AUTOMOC.
#
# ALSO here, at the bottom: the QML UI suite in qml/, which loads real components offscreen and
# presses them. It used to live in the app build because it borrowed the app's QML module through a
# staged, rewritten copy — so running it meant building whisper, FFmpeg, OpenCV and espeak first,
# and it therefore ran in NO release gate on any platform. It now declares the SAME module itself
# from cmake/PinPointQmlModule.cmake and depends on no app target. See the block at the end.

if(NOT COMMAND pp_add_test)
    cmake_minimum_required(VERSION 3.21)
    project(pinpoint_gui_tests LANGUAGES CXX)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../../tests/cmake)
    include(PinPointTests)
    enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Qml Concurrent Test)

if(MSVC)
    # As in the app and the Analysis tests: the charts presenters pass the Debug section limit (C1128,
    # found by the Windows test lane, platform refactor B0.2). Every target below, the QML UI suite included.
    add_compile_options(/bigobj)
endif()

set(GUI ${CMAKE_CURRENT_SOURCE_DIR}/..)   # src/Gui
set(SRC ${GUI}/..)                         # src

# RuleData and its store (refactor step 3.2: the packs and norms under one owner; D5, DEC-170): every
# target that reads the packs, the norms or the content sets through RuleData's snapshot, or builds a
# provider from a file by path, links these. With what they read: the registries, the pack and norm
# providers and the pack loader. RuleData is a QObject, so such a target is AUTOMOC.
set(RULE_DATA_SOURCES
    ${SRC}/DomainModel/rule_data.cpp ${SRC}/Stores/rule_data_store.cpp
    ${SRC}/Diagnostics/packs/drill_pack.cpp ${SRC}/Diagnostics/packs/screen_pack.cpp
    ${SRC}/Diagnostics/packs/reference_pack.cpp ${SRC}/Diagnostics/packs/gate_words_pack.cpp
    ${SRC}/Diagnostics/packs/resource_pack_provider.cpp ${SRC}/Diagnostics/packs/file_pack_provider.cpp
    ${SRC}/Diagnostics/packs/merged_pack_provider.cpp ${SRC}/Diagnostics/packs/memory_pack_provider.cpp
    ${SRC}/Diagnostics/norms/resource_norm_provider.cpp ${SRC}/Diagnostics/norms/file_norm_provider.cpp ${SRC}/Stores/rule_data_reads.cpp
    ${SRC}/Diagnostics/norms/merged_norm_provider.cpp ${SRC}/Diagnostics/norms/memory_norm_provider.cpp
    ${SRC}/Diagnostics/norms/norm.cpp ${SRC}/Diagnostics/norms/norm_pack.cpp ${SRC}/Diagnostics/norms/context_tree.cpp
    ${SRC}/Diagnostics/engine/pack_io.cpp ${SRC}/Diagnostics/packs/characteristic_pack.cpp
    ${SRC}/Diagnostics/engine/characteristic.cpp ${SRC}/Diagnostics/measures/measure_facets.cpp
    ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp
    ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp)

# TimelineLabels — 1-D label distribution solver + nearest/active searches + phase names.
pp_add_test(timeline_labels_test
    SOURCES timeline_labels_test.cpp ${GUI}/review/timeline_labels.cpp
    INCLUDE ${GUI} ${GUI}/review ${SRC} ${SRC}/Core
    LINK    Qt6::Qml  AUTOMOC)

# ChartMetrics::seriesGroups — the chart's METRICS presets, which are the metric catalogue's
# `.group` field joined against the series a swing actually produced. It links the REAL manifest
# (not a fixture) on purpose: the whole design is that the chart and the Metric Library share one
# grouping vocabulary, so a descriptor changing group has to be able to fail this test.
#
# The norm stack comes along because metric_providers.cpp -> reference_bands.cpp pulls it in, the
# same chain metric_catalogue_test links; pp_norm_env's JSON is not read by anything here, so the
# shipped resources are not needed and the env is left unset.
#
# NO ENTRY FOR series_reduce (C8, Phase 2), and that is not an omission: summaryMasked delegates
# every reduction to src/Vocabulary/Metrics/series_reduce.h, which is HEADER-ONLY — the reductions run on
# borrowed C arrays and only viewOf() touches MetricSeries, so there is nothing to compile or link
# here or in any other consumer target. Its own suite is series_reduce_test in src/Analysis/tests.
pp_add_test(chart_metrics_test
    SOURCES chart_metrics_test.cpp
            ${GUI}/review/chart_metrics.cpp
            # segments()/nearestPhase() ask TimelineLabels which phases are P-positions, so
            # the chips can never be labelled "→P2" from an untagged endpoint.
            ${GUI}/review/timeline_labels.cpp
            ${SRC}/Vocabulary/Metrics/metric_catalogue.cpp
            ${SRC}/Vocabulary/Metrics/metric_resolver.cpp
            ${SRC}/Vocabulary/Metrics/metric_catalogue_manifest.cpp
            ${SRC}/Vocabulary/Metrics/metric_providers.cpp
            ${SRC}/Wrist/reference_bands.cpp
            ${SRC}/Diagnostics/norms/norm.cpp
            ${SRC}/Diagnostics/norms/norm_pack.cpp ${SRC}/Diagnostics/norms/context_tree.cpp
            ${SRC}/Diagnostics/engine/pack_io.cpp   # the registries' shared vocabulary and LoadResult<T>
            ${SRC}/Diagnostics/packs/characteristic_pack.cpp ${SRC}/Diagnostics/engine/characteristic.cpp
            ${SRC}/Diagnostics/packs/reference_pack.cpp
            ${SRC}/Diagnostics/measures/measure_facets.cpp ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp
            ${SRC}/Diagnostics/norms/resource_norm_provider.cpp
            ${SRC}/Diagnostics/norms/file_norm_provider.cpp ${SRC}/Stores/rule_data_reads.cpp
            ${SRC}/Diagnostics/norms/merged_norm_provider.cpp
    INCLUDE ${GUI} ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/Vocabulary/Metrics ${SRC}/Analysis ${SRC}/Diagnostics
    LINK    Qt6::Qml  AUTOMOC)

# The Charts panel's session half (src/Gui/charts/): the session series model and the session maths.
include(${CMAKE_CURRENT_LIST_DIR}/folder_series_tests.cmake OPTIONAL)
include(${CMAKE_CURRENT_LIST_DIR}/charts_maths_tests.cmake OPTIONAL)
include(${CMAKE_CURRENT_LIST_DIR}/charts_traces_tests.cmake OPTIONAL)
include(${CMAKE_CURRENT_LIST_DIR}/charts_relations_tests.cmake OPTIONAL)
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_tests.cmake OPTIONAL)
include(${CMAKE_CURRENT_LIST_DIR}/charts_posture_tests.cmake OPTIONAL)
include(${CMAKE_CURRENT_LIST_DIR}/charts_delivery_tests.cmake OPTIONAL)

# SwingRigDriver (swing3d/) — the 3-D swing panel's C++ half against a hand-built skeleton3d block
# written by the production writer: availability and its reason, rest round trip, playhead
# interpolation, the scene frame, tiers. Header-only rig/JSON; the store reads the document.
# The rig words its refused skeleton and plane through the trust readings (refactor 5.8a: typed, out of the QML), and
# names the review views' door (header-only).
set(_rig_gate_sources ${GUI}/trust/trust_types.h ${GUI}/trust/trust_readings.cpp ${SRC}/Diagnostics/derived/series/session_sequence.cpp
                      ${GUI}/charts/session_sequence_words.cpp
                      ${SRC}/Diagnostics/derived/series/session_curves.cpp ${GUI}/review/shot_data_door.h)
pp_add_test(swing_rig_driver_test
    SOURCES swing_rig_driver_test.cpp ${GUI}/swing3d/swing_rig_driver.cpp ${GUI}/swing3d/swing_annotation_mesh.cpp
            ${SRC}/Stores/swing_paths.cpp ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp   # the session's swings by SwingPaths, keyed by swing number (step 2.7b)
            ${_rig_gate_sources}
    INCLUDE ${GUI} ${GUI}/swing3d ${SRC} ${SRC}/Core ${SRC}/Stores ${SRC}/Analysis ${SRC}/Vocabulary/Metrics
    LINK    Qt6::Qml  AUTOMOC)

# The 3-D swing panel's motion annotations as meshes (swing_annotation_mesh.h) — trace growth and
# its exact end at the playhead, gaps, the swing window, the fan's ≤ 24 frames on the tiles' ramp,
# lead side, trace targets, the plane quad. No Quick 3D: the mesh is pure arithmetic.
pp_add_test(swing_annotation_geometry_test
    SOURCES swing_annotation_geometry_test.cpp ${GUI}/swing3d/swing_annotation_mesh.cpp
            ${GUI}/swing3d/swing_rig_driver.cpp
            ${SRC}/Stores/swing_paths.cpp ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp   # the rig driver's session listing (step 2.7b)
            ${_rig_gate_sources}
    INCLUDE ${GUI} ${GUI}/swing3d ${SRC} ${SRC}/Core ${SRC}/Stores ${SRC}/Analysis ${SRC}/Vocabulary/Metrics
    LINK    Qt6::Qml  AUTOMOC)

# SwingSeriesModel — playhead→row mapping (rowForTimeUs) for the table cross-highlight.
pp_add_test(swing_series_model_test
    SOURCES swing_series_model_test.cpp ${GUI}/review/swing_series_model.cpp
    INCLUDE ${GUI} ${GUI}/review ${SRC} ${SRC}/Core
    LINK    Qt6::Qml  AUTOMOC)

# ChartsLane (charts/charts_work.h; refactor 5.5a, re-pointed from presenter_pool_test) — the charts' one threading
# idiom: a lane per owner on the one WorkPool, results through the one WorkReceiver, so destroying a presenter (every mode
# switch rebuilds the panels) never waits for its job (#173), and only a lane's latest ask is applied.
pp_add_test(charts_work_test
    SOURCES charts_work_test.cpp ${GUI}/charts/charts_work.cpp ${SRC}/Core/work_pool.h ${SRC}/Core/work_pool.cpp
            ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core
    LINK    Qt6::Qml
    AUTOMOC)

# ShotListModel — the carousel as a view of one Session (refactor step 5.2; R-QML-5), over a test double of its port
# (fake_shot_source.h): its rows follow the Session's events, and shotSummary, the focused-shot map for PpShotActionBar.
# Links the model + the swing.json writer it references + the application log
# (pp_log_stream.cpp + PpMessageLog.cpp — the primitive no longer arrives with
# pp_debug.cpp's whisper/ggml installer behind it, so this logs for real).
pp_add_test(shot_list_model_test
    SOURCES shot_list_model_test.cpp
            ${GUI}/shot/shot_list_model.cpp
            ${SRC}/Stores/swing_doc_writer.cpp ${SRC}/Stores/swing_store_repository.cpp ${SRC}/Core/folder_lock.cpp
            ${SRC}/Stores/swing_doc_reader.cpp
            ${SRC}/Stores/swing_doc_analysis.cpp
            ${SRC}/Stores/recording_header.cpp ${SRC}/Analysis/effective_context.cpp
            ${SRC}/Stores/swing_paths.cpp
            ${SRC}/Vocabulary/LaunchMonitor/launch_monitor_reading.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor ${SRC}/Stores ${SRC}/Stores/tests
    LINK    Qt6::Qml Qt6::Test  AUTOMOC)

# ReanalysisController — the single re-analyse seam the carousel forwards to.
# The controller's worker body (reanalyzeShot, through the shot's RecordingSource) is satisfied
# by reanalysis_stubs.cpp so the funnel test stays free of the analysis pipeline.
pp_add_test(reanalysis_controller_test
    SOURCES reanalysis_controller_test.cpp
            reanalysis_stubs.cpp
            ${GUI}/shot/reanalysis_controller.cpp
            ${SRC}/Analysis/reanalyse_and_persist.cpp   # the one write-back (step 1.4)
            ${SRC}/Core/pp_os_metrics.cpp   # reanalysis_controller.cpp scopes its worker thread
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor
    LINK    Qt6::Qml Qt6::Concurrent  AUTOMOC)

# SessionController — the live session clock and the active session type.
# The class states its own invariants (single active session; sessionStarted means
# "a session began"; the type is a QML index), and none of them had a test. The
# athlete's preferred-club seed is stubbed (session_clock_stubs.cpp) so the suite
# does not drag in the athlete store and the Diagnostics norm stack behind it.
# Qt6::Test is for QSignalSpy; the clock cases spin a real event loop for ~2.5 s.
pp_add_test(session_clock_test
    SOURCES session_clock_test.cpp
            session_clock_stubs.cpp
            ${GUI}/session/session_controller.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/athlete ${GUI}/session ${SRC} ${SRC}/Core
    LINK    Qt6::Qml Qt6::Test  AUTOMOC)

# NavigationController — screen history (back/forward, the Home reset). There is no session lock.
# app_screen.h is header-only and listed so AUTOMOC sees it. Qt6::Test is for QSignalSpy.
pp_add_test(navigation_controller_test
    SOURCES navigation_controller_test.cpp
            ${GUI}/shell/app_screen.h
            ${GUI}/shell/navigation_controller.cpp
    INCLUDE ${GUI} ${GUI}/shell ${SRC}
    LINK    Qt6::Qml Qt6::Test  AUTOMOC)

# The variability façade (variability/variability_facade.h, docs/design/variability-mark/spec.md §4.1, §8):
# the `variabilityResult` value type QML is handed — every field mapped, the token spellings, a NaN ρ — and
# the `Variability` singleton's §8.10 constants against variability_constants.h. Hand-built results; the
# grading itself is variability_grader_test's. variability_result.cpp builds the §7.5 basis strings; the
# typicality payloads take theirs from variability_typicality.cpp, which needs the grader's spread helpers.
# Also the typicality-band and trend-direction tokens, the cell grade's view and the typicality QVariant shapes.
pp_add_test(variability_facade_test
    SOURCES variability_facade_test.cpp
            ${GUI}/variability/variability_facade.cpp
            ${SRC}/Vocabulary/Metrics/variability/variability_result.cpp
            ${SRC}/Vocabulary/Metrics/variability/variability_grader.cpp
            ${SRC}/Vocabulary/Metrics/variability/variability_typicality.cpp
    INCLUDE ${GUI} ${GUI}/variability ${SRC}
    LINK    Qt6::Qml Qt6::Test  AUTOMOC)

# The variability geometry provider (variability/variability_geometry.h, spec §4.4 – §4.7, §11): the §4
# tables spot-checked, the grade ladder (spread never grows, grade 5 converges, grade 0 is the neutral
# element alone), clamping, the stroke widths and small-size radii, and the `VariabilityGeometry`
# singleton's one-object-per-key cache, then its path call fed to a real PathPolyline (hence Quick and
# offscreen; no window is shown).
find_package(Qt6 REQUIRED COMPONENTS Quick)
pp_add_test(variability_geometry_test
    SOURCES variability_geometry_test.cpp
            ${GUI}/variability/variability_geometry.cpp
    INCLUDE ${GUI} ${GUI}/variability ${SRC}
    LINK    Qt6::Qml Qt6::Quick Qt6::Test  AUTOMOC)
set_tests_properties(variability_geometry_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The per-instance variability geometry (DataVariabilityGeometry in variability/variability_geometry.h,
# spec §6.1, §6.2, §11): the spread funnel's half-width, breaks, fewer-than-3 state and 12-session cap;
# Your group's ring, clamp and chevron, zones, deterministic ghost jitter and corridor bracket; the trace
# envelope and its vertical scale; then the singleton's per-instance calls as plain JS objects and
# polygon() fed to a real PathPolyline (hence Quick and offscreen; no window is shown).
pp_add_test(variability_data_geometry_test
    SOURCES variability_data_geometry_test.cpp
            ${GUI}/variability/variability_geometry.cpp
    INCLUDE ${GUI} ${GUI}/variability ${SRC}
    LINK    Qt6::Qml Qt6::Quick Qt6::Test  AUTOMOC)
set_tests_properties(variability_data_geometry_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Role-keyed IMU placement (session_wizard_refactor_design.md §4.13) — the pure half
# ImuManager and AppSettings delegate to (imu_role_map): the imu/placement → imu/roles
# migration and its marker, the derived legacy view, the owner ladder, one claim per role,
# rolesInSession. ImuManager itself is not linked (BLE/device stack); its device facts are
# a table here. The REAL AppSettings is, against a scratch QSettings path, because the
# marker and "imu/placement is never written" are properties of what lands on disk.
pp_add_test(imu_role_map_test
    SOURCES imu_role_map_test.cpp
            ${GUI}/imu/imu_role_map.cpp
            ${GUI}/app/app_settings.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/app ${SRC} ${SRC}/Core ${SRC}/IMU
    LINK    Qt6::Qml Qt6::Concurrent Qt6::Test  AUTOMOC)

# The camera crop is per role (cameras/camera_roi.h, flir_camera_settings.md §2.1): the
# impact strip's own key, the recommended-mode fallback, and AppSettings' one-time
# migration of an existing impact camera's strip — the REAL AppSettings against a scratch
# QSettings path, as imu_role_map_test does, because the migration is what lands on disk.
pp_add_test(camera_roi_test
    SOURCES camera_roi_test.cpp
            ${GUI}/imu/imu_role_map.cpp
            ${GUI}/app/app_settings.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/app ${SRC} ${SRC}/Core ${SRC}/IMU ${SRC}/Video
    LINK    Qt6::Qml Qt6::Concurrent Qt6::Test  AUTOMOC)

# The device keys (src/Vocabulary/Rig/device_key.h; rig_design.md §4.6; IM §5.2 ID-DEVICE-KEY,
# ID-PLACEMENT-KEY): DeviceKey and PlacementKey spell every persisted key as today's code does, byte
# for byte; parse then spell is the identity; the lanes equal hm_unit_id.h's. The fixture (two cameras,
# one with no serial, a Witmotion, a HackMotion) is written by the REAL AppSettings, with keys spelled
# by today's code, into a scratch store (IsolatedStores), and read back through the typed keys.
pp_add_test(device_key_test
    SOURCES device_key_test.cpp
            ${SRC}/Vocabulary/Rig/device_key.h
            ${GUI}/imu/imu_role_map.cpp
            ${GUI}/app/app_settings.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/app ${SRC} ${SRC}/Core ${SRC}/IMU ${SRC}/Video
    LINK    Qt6::Qml Qt6::Concurrent Qt6::Test  AUTOMOC)
# Its stored-key cases prove that a sensor's name and the learned club length, stored by today's build, are still
# found after an update.
pp_requirement(device_key_test REQ-INST-17)

# The one battery rule (src/Vocabulary/Rig/battery_band.h; IM §6.1 NOTDOM-6): every band edge (100, 50, 49, 21, 20,
# 0), charging at each level, no reading, the spelling the screens read, and the worst of several devices. The
# standard library only.
pp_add_test(battery_band_test
    SOURCES battery_band_test.cpp
            ${SRC}/Vocabulary/Rig/battery_band.h
    NO_QT)

# The one GenICam pixel-format mapping (src/Video/genicam_pixel_format.h, platform refactor SD-5): for
# each of the four sites that carried a copy (the enumeration's Aravis and Spinnaker branches, each
# backend's queryCapabilities), every name maps exactly as that copy did, odd cases included.
# Header-only, no camera SDK.
pp_add_test(genicam_pixel_format_test
    SOURCES genicam_pixel_format_test.cpp
    INCLUDE ${SRC}/Video)

# PacedConnectQueue — ImuManager's paced BLE connect (design §4.11, F10): first device at
# once, each next a gap later, cancel, replace, skip-without-gap. Runs on an 80 ms gap and
# a real event loop for about a second.
pp_add_test(paced_connect_queue_test
    SOURCES paced_connect_queue_test.cpp
            ${GUI}/imu/paced_connect_queue.cpp
    INCLUDE ${GUI} ${SRC}
    LINK    Qt6::Test  AUTOMOC)

# TtsController's prepared phrases (media/tts_phrase_cache.h, 27 Oct 2026): the (backend, voice, text)
# key, idempotent prepare, invalidation on a voice/backend change, LRU capacity. Header-only, no audio.
pp_add_test(tts_phrase_cache_test
    SOURCES tts_phrase_cache_test.cpp
    INCLUDE ${GUI} ${SRC})

# The one cloud-fallback piece of the speech-to-text, text-to-speech and AI Coach controllers
# (media/cloud_fallback.h), under each controller's own policy: which key, whether no local GPU means
# cloud, the rebuild-on-a-changed-key guard, availability before the switch, the toggle's refusals.
# Fakes for the secrets, the preference and the switches: no SecretsManager, no settings, no engine.
pp_add_test(cloud_fallback_test
    SOURCES cloud_fallback_test.cpp
            ${GUI}/media/cloud_fallback.h ${GUI}/media/cloud_fallback.cpp
    INCLUDE ${GUI} ${GUI}/media
    LINK    Qt6::Test  AUTOMOC)

# ShotListModel's ledger — the carousel's ordinals, its reload from disk, and the
# write-through of the user's review.  Distinct from shot_list_model_test above,
# which covers shotSummary() only: this one puts real swing.json documents on disk
# (temp_library.h) and reads them back, because "the number survives a restart" and
# "the note comes back verbatim" are round-trip properties and cannot be asserted
# against an in-memory model alone.  Qt6::Test is for QSignalSpy.
pp_add_test(shot_ledger_test
    SOURCES shot_ledger_test.cpp
            ${GUI}/shot/shot_list_model.cpp
            ${SRC}/Stores/swing_doc_writer.cpp ${SRC}/Stores/swing_store_repository.cpp ${SRC}/Core/folder_lock.cpp
            ${SRC}/Stores/swing_doc_reader.cpp
            ${SRC}/Stores/swing_doc_analysis.cpp
            ${SRC}/Stores/recording_header.cpp ${SRC}/Analysis/effective_context.cpp
            ${SRC}/Stores/swing_paths.cpp
            ${SRC}/Stores/session_store.cpp            # the one swing allocator (step 2.2)
            ${SRC}/DomainModel/session_identity.cpp
            ${SRC}/Vocabulary/Identity/identity.h
            ${SRC}/Core/folder_lock.cpp
            ${SRC}/Vocabulary/LaunchMonitor/launch_monitor_reading.cpp
            ${SRC}/LaunchMonitor/gcquad_csv_parser.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor
            ${SRC}/Stores ${SRC}/Stores/tests
    LINK    Qt6::Qml Qt6::Test  AUTOMOC)

# LmSessionModel — the launch monitor board's empty lines (#57): the picked shot with no reading, the
# latest unread with nothing picked, and a session with none, each its own sentence; graphics.sessionHas
# keeps the Session view drawing when only the headline shot is empty. Rows built in memory. The model
# names a device through launch_monitor_factory, which brings both connectors, so libgspro links too.
pp_require_gspro()
# The board's model and everything it links, as a list: report_lm_test (report_model_tests.cmake) runs the same board.
set(PP_LM_BOARD_SOURCES
    ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
            ${GUI}/launchmonitor/lm_session_model.cpp
            ${GUI}/shot/shot_list_model.cpp
            ${GUI}/trust/trust_readings.cpp
            ${SRC}/Diagnostics/derived/series/session_sequence.cpp
            ${GUI}/charts/session_sequence_words.cpp
            ${SRC}/Diagnostics/derived/series/session_curves.cpp
            ${SRC}/Stores/swing_doc_writer.cpp ${SRC}/Stores/swing_store_repository.cpp ${SRC}/Core/folder_lock.cpp
            ${SRC}/Stores/swing_doc_reader.cpp
            ${SRC}/Stores/swing_doc_analysis.cpp
            ${SRC}/Stores/recording_header.cpp ${SRC}/Analysis/effective_context.cpp
            ${SRC}/Stores/swing_paths.cpp
            ${SRC}/Vocabulary/LaunchMonitor/launch_monitor_reading.cpp
            ${SRC}/LaunchMonitor/launch_monitor_base.cpp
            ${SRC}/LaunchMonitor/launch_monitor_factory.cpp
            ${SRC}/LaunchMonitor/gcquad_monitor.cpp
            ${SRC}/LaunchMonitor/gcquad_csv_parser.cpp
            ${SRC}/LaunchMonitor/gspro_monitor.cpp
            ${SRC}/LaunchMonitor/gspro_reading.cpp
            ${SRC}/LaunchMonitor/premier_monitor.cpp
            ${SRC}/LaunchMonitor/premier_session_parser.cpp
            ${SRC}/Diagnostics/packs/resource_pack_provider.cpp
            ${SRC}/Diagnostics/packs/file_pack_provider.cpp
            ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp
            ${SRC}/Diagnostics/packs/merged_pack_provider.cpp
            ${SRC}/Diagnostics/packs/characteristic_pack.cpp
            ${SRC}/Diagnostics/packs/reference_pack.cpp
            ${SRC}/Diagnostics/engine/characteristic.cpp
            ${SRC}/Diagnostics/engine/pack_io.cpp
            ${SRC}/Diagnostics/measures/measure_facets.cpp
            ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp
            ${SRC}/Diagnostics/norms/norm.cpp
            ${SRC}/Diagnostics/norms/norm_pack.cpp
            ${SRC}/Diagnostics/norms/context_tree.cpp
            ${SRC}/Diagnostics/norms/resource_norm_provider.cpp
            ${SRC}/Diagnostics/norms/file_norm_provider.cpp ${SRC}/Stores/rule_data_reads.cpp
            ${SRC}/Diagnostics/norms/merged_norm_provider.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
            # the variability marks (R4b per-club grades, R8 typicality) and each reading's scaleType,
            # from the manifest: the catalogue chain chart_metrics_test links
            ${GUI}/variability/variability_facade.cpp
            ${SRC}/Vocabulary/Metrics/variability/variability_result.cpp
            ${SRC}/Vocabulary/Metrics/variability/variability_grader.cpp
            ${SRC}/Vocabulary/Metrics/variability/variability_typicality.cpp
            ${SRC}/Vocabulary/Metrics/metric_catalogue.cpp
            ${SRC}/Vocabulary/Metrics/metric_resolver.cpp
            ${SRC}/Vocabulary/Metrics/metric_catalogue_manifest.cpp
            ${SRC}/Vocabulary/Metrics/metric_providers.cpp
            ${SRC}/Wrist/reference_bands.cpp
)
set(PP_LM_BOARD_INCLUDES ${GUI} ${GUI}/shot ${GUI}/launchmonitor ${GUI}/charts ${GUI}/trust ${GUI}/variability ${SRC} ${SRC}/Core
    ${SRC}/LaunchMonitor ${SRC}/Stores ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics)
# T (refactor 6.1a fix round): the board THROUGH THE SHOT — an Athlete, its Session and Shot over a temporary library
# and the carousel's port (migrate_step2_test's domain link line, less what the board's list already has).
pp_add_test(lm_session_model_test
    SOURCES lm_session_model_test.cpp ${PP_LM_BOARD_SOURCES}
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
            ${SRC}/DomainModel/athlete.h ${SRC}/DomainModel/athlete.cpp ${SRC}/Stores/athlete_store.cpp
            ${SRC}/DomainModel/session.h ${SRC}/DomainModel/session.cpp
            ${SRC}/DomainModel/shot.h ${SRC}/DomainModel/shot.cpp
            ${SRC}/Stores/session_store.cpp ${SRC}/DomainModel/session_identity.cpp
            ${SRC}/Vocabulary/Identity/identity.h   # moc: ShotRef / SessionId are gadgets (the events carry them)
            ${SRC}/App/shot_source.h ${SRC}/App/session_shot_source.h ${SRC}/App/session_shot_source.cpp
    INCLUDE ${PP_LM_BOARD_INCLUDES}
    LINK    Qt6::Qml Qt6::Network Qt6::Test gspro  AUTOMOC
    COMPILE_OPTIONS -DPP_LIVE_SWINGS_DIR="${SRC}/Diagnostics/tests/data/live_swings")
set_tests_properties(lm_session_model_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")

# NotificationCenter — the one notification sink: stable ids, coalescing,
# cause-suppression and condition latching (design doc §7.4-7.5, item H-g).
# Drives a clock it owns, so the 7 s dwell and 30 s grace window cost nothing.
pp_add_test(notification_center_test
    SOURCES notification_center_test.cpp
            ${GUI}/app/notification_center.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${SRC} ${SRC}/Core
    LINK    Qt6::Qml  AUTOMOC)

# The AI Coach's cloud engine (src/LLM, beside the LLM controller in Gui/media): its request carries the
# Gemini API key in the x-goog-api-key header and never in the address, and follows a redirect only to the
# same site. A made-up key; the redirect is checked against two local servers, and no call leaves the
# machine. The test lives in src/LLM/tests.
find_package(Qt6 REQUIRED COMPONENTS Network)
pp_add_test(gemini_request_test
    SOURCES ${SRC}/LLM/tests/gemini_request_test.cpp
            ${SRC}/LLM/GeminiLlmEngine.h ${SRC}/LLM/GeminiLlmEngine.cpp
            ${SRC}/LLM/LlmEngine.h       ${SRC}/LLM/LlmEngine.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${SRC}/LLM ${SRC}/Core
    LINK    Qt6::Network  AUTOMOC)

# The cloud speech engines' requests, text to speech (src/TTS/AzureTTSEngine) and speech to text
# (src/STT/STTBackendAzure): each carries the Azure key in the Ocp-Apim-Subscription-Key header and
# follows a redirect only to the same site. A made-up key; each redirect is checked against two local
# servers, and no call leaves the machine. Multimedia is for the speech engine base's audio format.
find_package(Qt6 REQUIRED COMPONENTS Network Multimedia)
pp_add_test(azure_speech_request_test
    SOURCES azure_speech_request_test.cpp
            ${SRC}/TTS/AzureTTSEngine.h ${SRC}/TTS/AzureTTSEngine.cpp
            ${SRC}/TTS/TTSEngine.h      ${SRC}/TTS/TTSEngine.cpp
            ${SRC}/STT/STTBackendAzure.h ${SRC}/STT/STTBackendAzure.cpp
            ${SRC}/STT/STTBackend.h
    INCLUDE ${SRC}/TTS ${SRC}/STT
    LINK    Qt6::Network Qt6::Multimedia  AUTOMOC)

# MissedSwingNotice (#281's stopgap) — a launch-monitor reading discarded because its ball was struck while the
# last shot was still analysing tells the golfer, once per swing; a paired reading never does. The real ShotPairing
# makes every offer and the real NotificationCenter takes every notice, on a clock the test owns.
pp_add_test(missed_swing_notice_test
    SOURCES missed_swing_notice_test.cpp
            ${GUI}/launchmonitor/missed_swing_notice.cpp
            ${GUI}/app/notification_center.cpp
            ${SRC}/Vocabulary/LaunchMonitor/launch_monitor_reading.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor
    LINK    Qt6::Qml  AUTOMOC)

# markup_truth — the Markup panel's ground-truth sidecars: which camera a swing's
# swing.json offers (face-on / down-the-line), and the two files those views write.
# The pair is the point: truth.json is FACE-ON pixel space and always has been, so
# down-the-line marks go to truth_dtl.json, in DTL pixels, carrying their own view /
# frame / stream provenance — a corpus audit already found a face-on ball at x=690
# sitting beside a 512-px-wide DTL frame, and nothing in a bare shaft[] can tell the
# two spaces apart. Qt Core only (no GUI, no OpenCV, no QML), and it puts synthetic
# swing.json trees in a temp dir, so it needs no fixture and no corpus.
#
# Given a swing dir on the command line it ALSO round-trips that real swing — which
# rewrites its truth.json, so ctest runs it with no arguments.
pp_add_test(test_markup_truth
    SOURCES ${GUI}/markup/tests/test_markup_truth.cpp
            ${GUI}/markup/markup_truth.cpp
    INCLUDE ${GUI} ${GUI}/markup ${SRC} ${SRC}/Core
    LINK    Qt6::Core)

# PdfDocumentWriter + PpPdfPrinter (print/) — print is to PDF. Drawn QImages through the writer,
# the PDF read back as bytes (page objects, A4 MediaBox per orientation, lossless images, the
# creator), the failures reported rather than written; then the QML printer run for real against a
# three-page stand-in register in an offscreen window. Qt Gui's QPdfWriter only — no Qt Pdf.
# Offscreen is forced in main() too, so no window appears however it is run.
find_package(Qt6 REQUIRED COMPONENTS Quick)
pp_add_test(pdf_document_writer_test
    SOURCES pdf_document_writer_test.cpp ${GUI}/print/pdf_document_writer.cpp
    INCLUDE ${GUI} ${GUI}/print ${SRC}
    LINK    Qt6::Qml Qt6::Quick  AUTOMOC
    COMPILE_OPTIONS -DPP_PRINT_QML_DIR="${GUI}/print")
set_tests_properties(pdf_document_writer_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The session report's records (report/report_meta.h's rules; Stores/session_report_io.h, the repository's file work, both
# header-only): the file name, the fingerprint moving with exactly its inputs, the three states and their words, the send
# log, the two files' round trip and the lazy migration of an older report_meta.json (refactor step 5.6a).
# Links pinpoint_swingstore (pp_add_test does) for SwingStore::info.
pp_add_test(report_meta_test
    SOURCES report_meta_test.cpp
            ${SRC}/Stores/swing_paths.cpp ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp   # the report's swing fingerprint lists swings through SwingPaths (step 2.7b)
            ${SRC}/Core/folder_lock.cpp   # the records' writes take the session's lock (session_report_io.h, step 5.6a)
    INCLUDE ${GUI} ${SRC})

# The rest of the session report's C++ tests, one fragment per part of the build so the parts can be
# registered independently: the stills, the model, the session list and mail hand-off, the studio,
# sending over SMTP.
include(${CMAKE_CURRENT_LIST_DIR}/report_stills_tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/report_model_tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/report_list_tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/report_studio_tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/report_smtp_tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/athlete_session_guard_tests.cmake)   # no athlete switch mid-session (Stage 0.8g)
include(${CMAKE_CURRENT_LIST_DIR}/replay_score_tests.cmake)   # the stored score through every reader (Stage 0.6)
include(${CMAKE_CURRENT_LIST_DIR}/analysis_detail_tests.cmake)   # the one projection: live == replay == reload (step 1.3b)

# No binding may subscribe to a dependency it does not USE.
#
# A revision counter named on a line of its own is an expression statement whose value is discarded;
# the QML compiler drops it and the binding is left subscribed to nothing, answering once and then
# forever with the past. It shipped that way in the Diagnostic Model panel, it is invisible to review
# and to qmllint, and any test that loads QML through QQmlComponent::setData will pass regardless
# because the interpreter keeps what the compiler removes. So this reads the source.
pp_add_test(qml_reactivity_test
    SOURCES qml_reactivity_test.cpp
    INCLUDE ${GUI} ${SRC}
    LINK    Qt6::Core
    COMPILE_OPTIONS -DPP_QML_ROOT="${GUI}")

# Session-setup lint (session_wizard_refactor_design.md §7.5). Reads the wizard, calibration and
# setup QML against the C++ headers they reach: W5 — every member QML uses on imuManager,
# cameraManager, liveWrist, launchMonitor (and appLog, once it exists) is a PUBLIC Q_INVOKABLE,
# slot, signal or Q_PROPERTY, and no Q_INVOKABLE sits in a non-public section (moc registers it,
# QML silently gets undefined); W6 — the test enum stand-ins match Perspective / Session Type;
# W1 — Timer/Connections gating, report-only until kEnforceW1; W12 — no rule built in any production
# QML file (AD-52, R-QML-4), R-QML-4's exceptions frozen in qml_rule_allowlist.txt, one line per file
# with its issue (the Solution Architect's list).
pp_add_test(session_setup_lint_test
    SOURCES session_setup_lint_test.cpp
    INCLUDE ${GUI} ${SRC}
    LINK    Qt6::Core
    COMPILE_OPTIONS -DPP_QML_ROOT="${GUI}"
                    -DPP_QML_RULE_ALLOWLIST="${CMAKE_CURRENT_SOURCE_DIR}/qml_rule_allowlist.txt")

# Design-system lint (governance/artefacts/design_system/README.md, #133). Reads every screen's QML and fails on a
# hand-drawn control, a raw text input, a popup that draws its own surface, a literal colour, or space
# that ignores the Density setting — outside the reasoned exceptions in design_system_allowlist.txt.
pp_add_test(design_system_lint_test
    SOURCES design_system_lint_test.cpp
    INCLUDE ${GUI} ${SRC}
    LINK    Qt6::Core
    COMPILE_OPTIONS -DPP_QML_ROOT="${GUI}"
                    -DPP_DS_ALLOWLIST="${CMAKE_CURRENT_SOURCE_DIR}/design_system_allowlist.txt")

# Architecture lint (docs/implementation/swe-audit/03-target-architecture.md §2, §11; refactor Step G).
# Reads every C++ and QML source under src/ and fails on an include into an outer ring (A1), QML creating
# a session/athlete-scoped type (A2), folder-name parsing (A3), a session/athlete file name outside its
# owner (A4), file I/O in a rules directory (A5), QVariantMap/QVariantList on the QML boundary (A6), a
# function over 300 lines in rings 1-2 (A7) and Analysis past the Recording contract (A8) — outside the
# tagged entries in architecture_allowlist.txt. Plain C++ on Qt Core: no Python needed on any builder.
pp_add_test(architecture_lint_test
    SOURCES architecture_lint_test.cpp
    INCLUDE ${GUI} ${SRC}
    LINK    Qt6::Core
    COMPILE_OPTIONS -DPP_SRC_ROOT="${SRC}"
                    -DPP_ARCH_ALLOWLIST="${CMAKE_CURRENT_SOURCE_DIR}/architecture_allowlist.txt")

# The names QML reads are the names C++ gives it (R-QML-1: the context globals are only devices, settings, logging and
# `app`): main.cpp registers exactly the ruled globals, no production QML or JS reads one of the five names that moved
# under `app` bare, and every `app.<member>` it reads is a member of GuiApp or App (a misspelt one is undefined without a
# word). Plain CMake, reading the source at run time: no build, no Python. No allow-list.
add_test(NAME qml_globals_lint_test
         COMMAND ${CMAKE_COMMAND} -DGUI=${GUI} -DAPPH=${SRC}/App/app.h -P ${CMAKE_CURRENT_SOURCE_DIR}/qml_globals_check.cmake)
set_tests_properties(qml_globals_lint_test PROPERTIES TIMEOUT 120)

# Namespace collision test (platform refactor B1.20a). Reads every C++ source under src/ and fails when one
# (namespace, name) — a class, struct, enum or using alias at namespace scope — is defined in two files: an ODR
# violation the linker accepts (7 Oct 2026, pinpoint::analysis::DagEdge). A forward declaration beside its
# definition passes; anonymous namespaces are per file; a detail repeat fails when a site is a header (PR-42).
# No allow-list. Shares lint_source_text.h.
pp_add_test(namespace_collision_test
    SOURCES namespace_collision_test.cpp
    INCLUDE ${GUI} ${SRC}
    LINK    Qt6::Core
    COMPILE_OPTIONS -DPP_SRC_ROOT="${SRC}")

# THE HEADLESS START (refactor 4-c; testing_developer_guide.md "The headless start for probes"): the APP BINARY launched
# with PINPOINT_HEADLESS_PROBE=1, offscreen, over a scratch home, settings and library, with the probe
# probes/headless_start.qml (every screen and Settings panel). Fails on a device part made, a part refused at the device
# layer, a QML ReferenceError or TypeError, or (macOS) a display_prompt in tccd's log, or an unreadable tccd log.
# THE BINARY (the app is a different CMake project, so no $<TARGET_FILE:PinPointStudio> here): PP_APP_BINARY, else the
# first that exists of the app tree beside this one (a worktree's build/app) and the main tree's Qt Creator build
# (build/Qt_*_for_macOS_Debug, the newest name). A binary that is missing, or built before the switch, FAILS the test
# and is never launched (its startup would raise the Bluetooth, Speech and Microphone prompts). The one way to skip:
# -DPP_APP_BINARY=OFF, which ctest reports as "Not Run" (SKIP_RETURN_CODE 77), never as passed.
set(PP_APP_BINARY "" CACHE STRING "The PinPointStudio binary headless_probe_test launches; OFF skips the test (default: found)")
set(_pp_app_binary "${PP_APP_BINARY}")
if(NOT _pp_app_binary AND NOT PP_APP_BINARY STREQUAL "OFF")
    set(_pp_app_candidates)
    if(APPLE)
        list(APPEND _pp_app_candidates "${CMAKE_BINARY_DIR}/../app/PinPointStudio.app/Contents/MacOS/PinPointStudio")
        file(GLOB _pp_app_main LIST_DIRECTORIES true "${PP_REPO_ROOT}/build/Qt_*_for_macOS_Debug")
        list(SORT _pp_app_main ORDER DESCENDING)
        foreach(_d IN LISTS _pp_app_main)
            list(APPEND _pp_app_candidates "${_d}/PinPointStudio.app/Contents/MacOS/PinPointStudio")
        endforeach()
    elseif(WIN32)
        list(APPEND _pp_app_candidates "${CMAKE_BINARY_DIR}/../app/PinPointStudio.exe")
    else()
        list(APPEND _pp_app_candidates "${CMAKE_BINARY_DIR}/../app/PinPointStudio")
    endif()
    list(GET _pp_app_candidates 0 _pp_app_binary)        # none exists: the first is named in the failure
    foreach(_c IN LISTS _pp_app_candidates)
        if(EXISTS "${_c}")
            set(_pp_app_binary "${_c}")
            break()
        endif()
    endforeach()
    message(STATUS "headless_probe_test launches ${_pp_app_binary}")
endif()
add_test(NAME headless_probe_test
         COMMAND ${CMAKE_COMMAND} "-DAPP=${_pp_app_binary}" -DPROBE=${CMAKE_CURRENT_SOURCE_DIR}/probes/headless_start.qml
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR}/headless_probe_work
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/headless_probe_check.cmake)
set_tests_properties(headless_probe_test PROPERTIES TIMEOUT 400 SKIP_RETURN_CODE 77)

# run_headless_selftest (tooling 5.3d): tools/probe/run_headless.sh, the one launcher for agent probes, refuses a real
# home, settings file or library, and kills a launch whose process lacks PINPOINT_HEADLESS_PROBE=1. Proved with a stand-in
# binary the script compiles; it never launches the app, so it needs no app build.
if(UNIX)
    add_test(NAME run_headless_selftest
             COMMAND ${SRC}/../tools/probe/run_headless_selftest.sh ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(run_headless_selftest PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)   # it times a 1 s window
endif()

# THE QUIT PINS (SE-1b; testing_developer_guide.md "The quit pins"): the APP BINARY (the same one headless_probe_test
# finds) started headless through tools/probe/run_headless.sh and quit at each point of start-up, during and after the
# pose warm-up, and while a re-analysis runs; each pin twice. quit_pins/quit_pin.py is the driver and the oracle (exit
# code 0, gone within 5 s of the quit, no teardown fault line, no work run on after the application has gone, the
# pin's busy marker before the quit). One ctest per pin; quit_pins_selftest proves the oracle catches a planted abort,
# hang, missing marker, fault line and after-shutdown line, lets the near misses pass, and (with the fixture) catches a
# re-analysis log without the stop and a swing written after all (#294). Not Run (77):
# a busy pin with no fixture library (quit_pins/make_fixture.sh), or a pin that fails as its filed issue says (KNOWN;
# empty since #294's fix: a quit during a re-analysis stops it, and the swing's files are left byte-identical).
# Label quit_pins: about 7–8 minutes in all, most of it the pose model's load (CoreML: 1–2 minutes each time it
# finishes). Serial: one app at a time (AG-16).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(UNIX AND Python3_Interpreter_FOUND)
    set(_pp_quit_pins startup ready ready-close work-ons report-list housekeeping warm-up after-warm-up
                      athlete-on-screen re-analysis re-analysis-in-warm-up)
    set(_pp_quit_cmd ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/quit_pins/quit_pin.py --app ${_pp_app_binary}
                     --repo ${PP_REPO_ROOT} --work ${CMAKE_CURRENT_BINARY_DIR}/quit_pins_work)
    add_test(NAME quit_pins_selftest COMMAND ${_pp_quit_cmd} --selftest)
    set_tests_properties(quit_pins_selftest PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE LABELS quit_pins)
    foreach(_pin IN LISTS _pp_quit_pins)
        add_test(NAME quit_pin.${_pin} COMMAND ${_pp_quit_cmd} --pin ${_pin})
        set_tests_properties(quit_pin.${_pin} PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE SKIP_RETURN_CODE 77
                                                         LABELS quit_pins)
    endforeach()
endif()

# governance/artefacts/design_system/tokens.json is generated from theme/Theme.qml: fail when it is stale, so the
# design-system docs cannot drift from the code. Skipped (not failed) where there is no Python 3.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME design_system_tokens_check
             COMMAND ${Python3_EXECUTABLE} -I ${SRC}/../tools/theme/make_design_system.py --check)
    # The method's tools (tools/method/README.md): each proves itself on fixtures in a temporary directory, never on
    # the real information model.
    add_test(NAME im_excerpt_selftest
             COMMAND ${Python3_EXECUTABLE} -I ${SRC}/../tools/method/im_excerpt.py --self-test)
    add_test(NAME im_changelog_merge_selftest
             COMMAND ${Python3_EXECUTABLE} -I ${SRC}/../tools/method/im_changelog_merge.py --self-test)
    set_tests_properties(im_excerpt_selftest im_changelog_merge_selftest PROPERTIES TIMEOUT 60)
    # The requirements traceability matrix, governance/artefacts/rtm.md (test_strategy.md §8): rtm_check fails when it
    # is stale, or on a flag that is neither an accepted gap (rtm_accepted_gaps.txt) nor in the baseline
    # (rtm_baseline.txt); rtm_selftest proves every flag, red then green, on fixtures in a temporary directory. Both
    # run offline, from committed files only (the issue snapshot is rtm_issues.json, refreshed with --refresh-issues).
    add_test(NAME rtm_check
             COMMAND ${Python3_EXECUTABLE} -I ${SRC}/../tools/method/make_rtm.py --check)
    add_test(NAME rtm_selftest
             COMMAND ${Python3_EXECUTABLE} -I ${SRC}/../tools/method/make_rtm.py --self-test)
    set_tests_properties(rtm_check rtm_selftest PROPERTIES TIMEOUT 120)
    pp_requirement(rtm_check CTL-BT-39)
    pp_requirement(rtm_selftest CTL-BT-39)
    # tools/refactor/move_files.py (platform refactor B1.2), the move tool every scripted move rests on: its rules,
    # refusals and --verify, proved on a fixture git repository it builds in a temporary directory (never this tree).
    # Exit 77, a skip, where there is no git.
    add_test(NAME move_files_selftest
             COMMAND ${Python3_EXECUTABLE} -I ${SRC}/../tools/refactor/move_files.py --self-test)
    set_tests_properties(move_files_selftest PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
    # #195 moved the governing documents into governance/artefacts/ (governance/artefacts/README.md): fail when a
    # tracked file still cites one at its old path, by name or by a relative link; the dated records that keep the old
    # spelling are listed in the script with a reason. Exit 77, a skip, where there is no git.
    add_test(NAME artefact_paths_check
             COMMAND ${Python3_EXECUTABLE} -I ${SRC}/../tools/method/artefact_paths_check.py)
    set_tests_properties(artefact_paths_check PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()

# one_at_a_time_selftest: tools/method/one_at_a_time.sh, the machine-wide heavy-work lock, proved on a lock folder in a
# temporary directory (two holders, the second waits; a stale pid is reclaimed; the exit code passes through). It never
# takes the real build/.heavy.lock, which the ctest run itself is usually holding. A zsh script, so absent where there
# is no zsh.
find_program(PP_ZSH zsh)
if(UNIX AND PP_ZSH)
    add_test(NAME one_at_a_time_selftest
             COMMAND ${PP_ZSH} ${SRC}/../tools/method/one_at_a_time.sh --self-test)
    set_tests_properties(one_at_a_time_selftest PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)   # it times a short wait
    # cabin_run_selftest: tools/method/cabin_run.sh, the Mac's runner for the full suite on the studio PC, proved against a
    # fake ssh and cabin in a temporary directory (the cabin lock: two runners, a stale lock; BUSY and --wait; the summary
    # of real cabin log samples). It never reaches the real share, ssh or the studio PC.
    # It is a Mac tool (BSD sed, base64 -D, stat -f), so it is registered on macOS only.
    if(APPLE)
        add_test(NAME cabin_run_selftest
                 COMMAND ${PP_ZSH} ${SRC}/../tools/method/cabin_run.sh --self-test)
        set_tests_properties(cabin_run_selftest PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)   # it times short waits
    endif()
endif()

# ── The offscreen QML UI suite ────────────────────────────────────────────────
#
# 11 tst_*.qml that load real components, render them offscreen and press them — the only coverage
# of the QML↔C++ seam and of the layouts themselves. It used to live in the APP build, borrowing the
# app's QML module through a staged, prefer-stripped copy, which is why it ran in no release gate on
# any platform. It now declares that module itself.
#
# ⚠ DECLARED FROM src/Gui, NOT FROM HERE, AND THAT IS FORCED RATHER THAN STYLISTIC. Qt derives each
# file's qmlcachegen output directory from its path relative to CMAKE_CURRENT_SOURCE_DIR, so from
# THIS directory every one of the 157 QML files is reached by going up and the cache directory comes
# out as "qml_ui_test_../theme" — which macOS resolves and Windows ninja refuses. src/Gui is an
# ancestor of all of them, so the paths come out clean. See src/Gui/CMakeLists.txt.
add_subdirectory(${GUI} ${CMAKE_CURRENT_BINARY_DIR}/qmlui)
