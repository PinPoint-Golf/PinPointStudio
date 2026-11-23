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

set(GUI ${CMAKE_CURRENT_SOURCE_DIR}/..)   # src/Gui
set(SRC ${GUI}/..)                         # src

# RuleData and its store (refactor step 3.2: the packs and norms under one owner; D5, DEC-170): every
# target that reads the packs, the norms or the content sets through RuleData's snapshot, or builds a
# provider from a file by path, links these. With what they read: the registries, the pack and norm
# providers and the pack loader. RuleData is a QObject, so such a target is AUTOMOC.
set(RULE_DATA_SOURCES
    ${SRC}/DomainModel/rule_data.cpp ${SRC}/Export/rule_data_store.cpp
    ${SRC}/Diagnostics/packs/drill_pack.cpp ${SRC}/Diagnostics/packs/screen_pack.cpp
    ${SRC}/Diagnostics/packs/reference_pack.cpp ${SRC}/Diagnostics/packs/gate_words_pack.cpp
    ${SRC}/Diagnostics/packs/resource_pack_provider.cpp ${SRC}/Diagnostics/packs/file_pack_provider.cpp
    ${SRC}/Diagnostics/packs/merged_pack_provider.cpp ${SRC}/Diagnostics/packs/memory_pack_provider.cpp
    ${SRC}/Diagnostics/norms/resource_norm_provider.cpp ${SRC}/Diagnostics/norms/file_norm_provider.cpp
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
# every reduction to src/Metrics/series_reduce.h, which is HEADER-ONLY — the reductions run on
# borrowed C arrays and only viewOf() touches MetricSeries, so there is nothing to compile or link
# here or in any other consumer target. Its own suite is series_reduce_test in src/Analysis/tests.
pp_add_test(chart_metrics_test
    SOURCES chart_metrics_test.cpp
            ${GUI}/review/chart_metrics.cpp
            # segments()/nearestPhase() ask TimelineLabels which phases are P-positions, so
            # the chips can never be labelled "→P2" from an untagged endpoint.
            ${GUI}/review/timeline_labels.cpp
            ${SRC}/Metrics/metric_catalogue.cpp
            ${SRC}/Metrics/metric_resolver.cpp
            ${SRC}/Metrics/metric_catalogue_manifest.cpp
            ${SRC}/Metrics/metric_providers.cpp
            ${SRC}/Wrist/reference_bands.cpp
            ${SRC}/Diagnostics/norms/norm.cpp
            ${SRC}/Diagnostics/norms/norm_pack.cpp ${SRC}/Diagnostics/norms/context_tree.cpp
            ${SRC}/Diagnostics/engine/pack_io.cpp   # the registries' shared vocabulary and LoadResult<T>
            ${SRC}/Diagnostics/packs/characteristic_pack.cpp ${SRC}/Diagnostics/engine/characteristic.cpp
            ${SRC}/Diagnostics/packs/reference_pack.cpp
            ${SRC}/Diagnostics/measures/measure_facets.cpp ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp
            ${SRC}/Diagnostics/norms/resource_norm_provider.cpp
            ${SRC}/Diagnostics/norms/file_norm_provider.cpp
            ${SRC}/Diagnostics/norms/merged_norm_provider.cpp
    INCLUDE ${GUI} ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/Metrics ${SRC}/Analysis ${SRC}/Diagnostics
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
pp_add_test(swing_rig_driver_test
    SOURCES swing_rig_driver_test.cpp ${GUI}/swing3d/swing_rig_driver.cpp ${GUI}/swing3d/swing_annotation_mesh.cpp
            ${SRC}/Export/swing_paths.cpp ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp   # the session's swings by SwingPaths, keyed by swing number (step 2.7b)
    INCLUDE ${GUI} ${GUI}/swing3d ${SRC} ${SRC}/Core ${SRC}/Export
    LINK    Qt6::Qml  AUTOMOC)

# The 3-D swing panel's motion annotations as meshes (swing_annotation_mesh.h) — trace growth and
# its exact end at the playhead, gaps, the swing window, the fan's ≤ 24 frames on the tiles' ramp,
# lead side, trace targets, the plane quad. No Quick 3D: the mesh is pure arithmetic.
pp_add_test(swing_annotation_geometry_test
    SOURCES swing_annotation_geometry_test.cpp ${GUI}/swing3d/swing_annotation_mesh.cpp
            ${GUI}/swing3d/swing_rig_driver.cpp
            ${SRC}/Export/swing_paths.cpp ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp   # the rig driver's session listing (step 2.7b)
    INCLUDE ${GUI} ${GUI}/swing3d ${SRC} ${SRC}/Core ${SRC}/Export
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
            ${SRC}/Export/swing_doc_writer.cpp ${SRC}/Export/swing_store_repository.cpp ${SRC}/Core/folder_lock.cpp
            ${SRC}/Export/swing_doc_reader.cpp
            ${SRC}/Export/swing_doc_analysis.cpp
            ${SRC}/Export/recording_header.cpp ${SRC}/Analysis/effective_context.cpp
            ${SRC}/Export/swing_paths.cpp
            ${SRC}/LaunchMonitor/launch_monitor_reading.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor ${SRC}/Export ${SRC}/Export/tests
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
            ${SRC}/Metrics/variability/variability_result.cpp
            ${SRC}/Metrics/variability/variability_grader.cpp
            ${SRC}/Metrics/variability/variability_typicality.cpp
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

# ShotListModel's ledger — the carousel's ordinals, its reload from disk, and the
# write-through of the user's review.  Distinct from shot_list_model_test above,
# which covers shotSummary() only: this one puts real swing.json documents on disk
# (temp_library.h) and reads them back, because "the number survives a restart" and
# "the note comes back verbatim" are round-trip properties and cannot be asserted
# against an in-memory model alone.  Qt6::Test is for QSignalSpy.
pp_add_test(shot_ledger_test
    SOURCES shot_ledger_test.cpp
            ${GUI}/shot/shot_list_model.cpp
            ${SRC}/Export/swing_doc_writer.cpp ${SRC}/Export/swing_store_repository.cpp ${SRC}/Core/folder_lock.cpp
            ${SRC}/Export/swing_doc_reader.cpp
            ${SRC}/Export/swing_doc_analysis.cpp
            ${SRC}/Export/recording_header.cpp ${SRC}/Analysis/effective_context.cpp
            ${SRC}/Export/swing_paths.cpp
            ${SRC}/Export/session_store.cpp            # the one swing allocator (step 2.2)
            ${SRC}/DomainModel/session_identity.cpp
            ${SRC}/Identity/identity.h
            ${SRC}/Core/folder_lock.cpp
            ${SRC}/LaunchMonitor/launch_monitor_reading.cpp
            ${SRC}/LaunchMonitor/gcquad_csv_parser.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor
            ${SRC}/Export ${SRC}/Export/tests
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
            ${GUI}/charts/session_sequence.cpp
            ${GUI}/charts/session_curves.cpp
            ${SRC}/Export/swing_doc_writer.cpp ${SRC}/Export/swing_store_repository.cpp ${SRC}/Core/folder_lock.cpp
            ${SRC}/Export/swing_doc_reader.cpp
            ${SRC}/Export/swing_doc_analysis.cpp
            ${SRC}/Export/recording_header.cpp ${SRC}/Analysis/effective_context.cpp
            ${SRC}/Export/swing_paths.cpp
            ${SRC}/LaunchMonitor/launch_monitor_reading.cpp
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
            ${SRC}/Diagnostics/norms/file_norm_provider.cpp
            ${SRC}/Diagnostics/norms/merged_norm_provider.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
            # the variability marks (R4b per-club grades, R8 typicality) and each reading's scaleType,
            # from the manifest: the catalogue chain chart_metrics_test links
            ${GUI}/variability/variability_facade.cpp
            ${SRC}/Metrics/variability/variability_result.cpp
            ${SRC}/Metrics/variability/variability_grader.cpp
            ${SRC}/Metrics/variability/variability_typicality.cpp
            ${SRC}/Metrics/metric_catalogue.cpp
            ${SRC}/Metrics/metric_resolver.cpp
            ${SRC}/Metrics/metric_catalogue_manifest.cpp
            ${SRC}/Metrics/metric_providers.cpp
            ${SRC}/Wrist/reference_bands.cpp
)
set(PP_LM_BOARD_INCLUDES ${GUI} ${GUI}/shot ${GUI}/launchmonitor ${GUI}/charts ${GUI}/trust ${GUI}/variability ${SRC} ${SRC}/Core
    ${SRC}/LaunchMonitor ${SRC}/Export ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Metrics)
pp_add_test(lm_session_model_test
    SOURCES lm_session_model_test.cpp ${PP_LM_BOARD_SOURCES}
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
    INCLUDE ${PP_LM_BOARD_INCLUDES}
    LINK    Qt6::Qml Qt6::Network Qt6::Test gspro  AUTOMOC)
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

# The session report's records (report/report_meta.h's rules; Export/session_report_io.h, the repository's file work, both
# header-only): the file name, the fingerprint moving with exactly its inputs, the three states and their words, the send
# log, the two files' round trip and the lazy migration of an older report_meta.json (refactor step 5.6a).
# Links pinpoint_swingstore (pp_add_test does) for SwingStore::info.
pp_add_test(report_meta_test
    SOURCES report_meta_test.cpp
            ${SRC}/Export/swing_paths.cpp ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp   # the report's swing fingerprint lists swings through SwingPaths (step 2.7b)
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
# W1 — Timer/Connections gating, report-only until kEnforceW1.
pp_add_test(session_setup_lint_test
    SOURCES session_setup_lint_test.cpp
    INCLUDE ${GUI} ${SRC}
    LINK    Qt6::Core
    COMPILE_OPTIONS -DPP_QML_ROOT="${GUI}")

# Design-system lint (docs/design/system/README.md, #133). Reads every screen's QML and fails on a
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

# docs/design/system/tokens.json is generated from theme/Theme.qml: fail when it is stale, so the
# design-system docs cannot drift from the code. Skipped (not failed) where there is no Python 3.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME design_system_tokens_check
             COMMAND ${Python3_EXECUTABLE} -I ${SRC}/../tools/theme/make_design_system.py --check)
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
