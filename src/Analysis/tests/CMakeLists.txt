# CTest suite for the M1 shot-analyzer (spans src/Analysis + Export persistence +
# the IMU driver stack + ShaftTracker). Self-contained — NOT part of the app
# build. Runs standalone or under the tests/ umbrella; shared infra (Qt prefix,
# Eigen via pp_find_eigen, sanitizers, pp_add_test) lives in tests/cmake/PinPointTests.cmake.
#
#   cmake -S src/Analysis/tests -B build/analyzer-tests
#   cmake --build build/analyzer-tests -j && ctest --test-dir build/analyzer-tests --output-on-failure
#
# The tests are self-contained (own main() + CHECK_NEAR; no googletest). They
# compile the handful of source .cpp directly — there is no pinpoint_analysis
# library. PP_BUFFER (types.h, pulled in by swing_analysis.h) is on every test's
# include path via pp_add_test.

if(NOT COMMAND pp_add_test)
    cmake_minimum_required(VERSION 3.16)
    project(pinpoint_analyzer_tests LANGUAGES CXX)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../../tests/cmake)
    include(PinPointTests)
    enable_testing()
endif()

set(ANALYSIS ${CMAKE_CURRENT_SOURCE_DIR}/..)   # src/Analysis
set(SRC      ${ANALYSIS}/..)                    # src

if(MSVC)
    # The shaft_tracker/shaft_track timing targets request -O2 (→ /O2 on cl.exe),
    # which is incompatible with the /RTC1 runtime checks a Debug configuration
    # injects (cl error D8016). Drop /RTC1 for this test suite. (_USE_MATH_DEFINES
    # is provided globally by tests/cmake/PinPointTests.cmake.)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

# --- Pure angle math (header-only) -------------------------------------------
pp_add_test(wrist_angles_test         SOURCES wrist_angles_test.cpp)
pp_add_test(wrist_angle_sampling_test SOURCES wrist_angle_sampling_test.cpp)

# --- Wrist Motion assessment engine ------------------------------------------
# The engine .cpp pulls in the rule registry, so every engine-linked test
# compiles both.
set(WRIST_ENGINE ${ANALYSIS}/reference_bands.cpp
                 ${ANALYSIS}/wrist_assessment_engine.cpp
                 ${ANALYSIS}/assessment_rules.cpp)

pp_add_test(reference_bands_test    SOURCES reference_bands_test.cpp ${ANALYSIS}/reference_bands.cpp)
pp_add_test(archetype_bands_test    SOURCES archetype_bands_test.cpp ${WRIST_ENGINE})
pp_add_test(tier1_banding_test      SOURCES tier1_banding_test.cpp   ${WRIST_ENGINE})
pp_add_test(wrist_analysis_adapter_test
    SOURCES wrist_analysis_adapter_test.cpp ${ANALYSIS}/wrist_analysis_adapter.cpp ${WRIST_ENGINE})
pp_add_test(fault_rules_test        SOURCES fault_rules_test.cpp        ${WRIST_ENGINE})
pp_add_test(strengths_test          SOURCES strengths_test.cpp          ${WRIST_ENGINE})
pp_add_test(relationship_rules_test SOURCES relationship_rules_test.cpp ${WRIST_ENGINE})
pp_add_test(confidence_test         SOURCES confidence_test.cpp         ${WRIST_ENGINE})
pp_add_test(composite_score_v2_test SOURCES composite_score_v2_test.cpp ${WRIST_ENGINE})

# --- Header-only math contracts ----------------------------------------------
pp_add_test(imu_calibration_test  SOURCES imu_calibration_test.cpp)   # A·q·M solve + keystone golden
pp_add_test(live_wrist_angles_test SOURCES live_wrist_angles_test.cpp) # live tick() math contract
pp_add_test(imu_sample_test       SOURCES imu_sample_test.cpp)         # exported swing.json vector frame
pp_add_test(viz_frame_test        SOURCES viz_frame_test.cpp)         # worldToScene + ArmVizView bone math

# --- Segmentation v2/v3 + metric extraction ----------------------------------
pp_add_test(phase_signals_test
    SOURCES phase_signals_test.cpp ${ANALYSIS}/phase_signals.cpp)
pp_add_test(phase_segmenter_test
    SOURCES phase_segmenter_test.cpp ${ANALYSIS}/phase_segmenter.cpp ${ANALYSIS}/phase_signals.cpp)

# Phase segmenter + metric extractor over a synthetic swing. metric_extractor.cpp
# carries resource-profiler instrumentation, so it pulls in the profiler core
# (pp_profiler/pp_os_metrics/PpStatsLog) and a PpLogStream (imu_test_stubs).
# imu_test_stubs.cpp pulls event_buffer.h → swing_window.h (std::span) → C++20.
pp_add_test(pipeline_test
    SOURCES pipeline_test.cpp
            ${ANALYSIS}/phase_segmenter.cpp
            ${ANALYSIS}/phase_signals.cpp
            ${ANALYSIS}/metric_extractor.cpp
            ${SRC}/Core/pp_profiler.cpp
            ${SRC}/Core/pp_os_metrics.cpp
            ${SRC}/Core/PpStatsLog.cpp
            imu_test_stubs.cpp
    INCLUDE ${SRC}/Core   # pp_debug.h for the stub
    STD 20)

# --- Banded weighted-geometric-mean scorer (Swing/GRF adherence) -------------
pp_add_test(swing_scorer_test SOURCES swing_scorer_test.cpp ${ANALYSIS}/swing_scorer.cpp)

# --- Per-archetype resemblance scorer + uncertainty interval (Wrist, §B.0a/§B.7)
pp_add_test(wrist_resemblance_test SOURCES wrist_resemblance_test.cpp
    ${ANALYSIS}/wrist_resemblance.cpp ${ANALYSIS}/score_uncertainty.cpp)

# --- Tunable-parameter plumbing: frozen-constant parity + dotted-key overrides
pp_add_test(tuned_constants_parity_test SOURCES tuned_constants_parity_test.cpp)
pp_add_test(tuning_overrides_test SOURCES tuning_overrides_test.cpp ${ANALYSIS}/swing_scorer.cpp ${ANALYSIS}/reference_bands.cpp)

# --- Header-only review/shot helpers -----------------------------------------
pp_add_test(session_summary_test SOURCES session_summary_test.cpp)   # session_summary.h
pp_add_test(arbiter_test         SOURCES arbiter_test.cpp)           # shot_arbiter.h

# --- Unified swing.json writer/reader round-trip (Export subsystem) ----------
pp_add_test(swing_doc_test
    SOURCES ${SRC}/Export/tests/swing_doc_test.cpp ${SRC}/Export/swing_doc.cpp)

# --- Orientation filters (Phase 0.4): Madgwick (header-only) + ESKF (Eigen) ---
# The ESKF .cpp pulls the vendored imu_ekf headers through the Arduino-style
# <Eigen.h>/<Wire.h> shims in imu_ekf_compat (which MUST precede imu_ekf on the
# include path), and those need real Eigen. pp_find_eigen() resolves it.
pp_find_eigen(EIGEN_DIR)
pp_add_test(orientation_filter_test
    SOURCES orientation_filter_test.cpp ${SRC}/IMU/eskf_orientation_filter.cpp
    INCLUDE ${PP_THIRD}/imu_ekf_compat ${PP_THIRD}/imu_ekf ${EIGEN_DIR})

# Offline orientation re-fusion + parity (the post-hoc tuning substrate; corpus-1
# pre-collection gate E1). Same ESKF/Eigen deps; the refuser is header-only.
pp_add_test(orientation_refuse_test
    SOURCES orientation_refuse_test.cpp ${SRC}/IMU/eskf_orientation_filter.cpp
    INCLUDE ${SRC}/IMU ${PP_THIRD}/imu_ekf_compat ${PP_THIRD}/imu_ekf ${EIGEN_DIR})

# --- IMU driver frame-parse + WT901BLE67 eulerToQuat override (Phase 0.3) -----
# Exercises the REAL driver stack. imu_test_stubs.cpp supplies EventBuffer::nowMicros
# and PpLogStream so we link neither the Buffer library nor pp_debug.cpp. The
# Q_OBJECT driver classes need moc (AUTOMOC); std::span → C++20.
find_package(Qt6 REQUIRED COMPONENTS Bluetooth)
pp_add_test(imu_driver_frame_test
    SOURCES imu_driver_frame_test.cpp
            imu_test_stubs.cpp
            ${SRC}/IMU/wt9011dcl_ble.cpp
            ${SRC}/IMU/ble_imu_transport.cpp
            ${SRC}/IMU/wt9011dcl_base.cpp
            ${SRC}/IMU/imu_base.cpp
            ${SRC}/IMU/eskf_orientation_filter.cpp
    INCLUDE ${SRC}/IMU ${SRC}/Core
            ${PP_THIRD}/imu_ekf_compat ${PP_THIRD}/imu_ekf ${EIGEN_DIR}
    LINK    Qt6::Bluetooth
    STD 20  AUTOMOC)

# --- ShaftTracker OpenCV targets ---------------------------------------------
# OpenCV (system 4.x, core imgproc only — no contrib). Both math sources are
# Qt-free; -O2 because the suites set no build type and assert real hot-loop cost.
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

# Shared frame decode (src/Export/frame_decode, ShaftTracker S0).
pp_add_test(frame_decode_test
    SOURCES frame_decode_test.cpp ${SRC}/Export/frame_decode.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    NO_QT)

# Shaft v3.0-r1 evidence engines (E1 discrete band + E2 polarity ridge sweep) —
# Qt-free math core (shaft_tracker_math). -O2: the suites set no build type and
# these assert real hot-loop cost.
pp_add_test(shaft_evidence_test
    SOURCES shaft_evidence_test.cpp ${ANALYSIS}/shaft_tracker_math.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    COMPILE_OPTIONS -O2
    NO_QT)

# Shaft v3.0-r1 deciding half (phase model, geometry, emission, banded Viterbi
# DP, ψ-isotonic reconcile, tiering + the decideTrack core, which calls the E1/E2
# engines) — needs Qt (QVariantMap config) + OpenCV. ball_anchor.cpp supplies
# medianGripBallLenPx (A1), now referenced by decideTrack's length ladder.
pp_add_test(shaft_decide_test
    SOURCES shaft_decide_test.cpp ${ANALYSIS}/shaft_track_assembly.cpp ${ANALYSIS}/shaft_tracker_math.cpp ${ANALYSIS}/ball_anchor.cpp ${ANALYSIS}/clubhead_track.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    COMPILE_OPTIONS -O2)

# Ball anchor (v3.4): medianGripBallLenPx (median/jump-gate/stillness/window) +
# applyBallAnchor's A3 head recompute and no-op contracts — synthetic, no fixture.
pp_add_test(ball_anchor_test
    SOURCES ball_anchor_test.cpp ${ANALYSIS}/ball_anchor.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    COMPILE_OPTIONS -O2)

# Persisted live-baseline blob loader (ball_baseline_io.h) — happy path + the
# truncated/missing/wrong-dims rejects that force BallRunner's self-seed fallback.
# Header-only; Qt (QFile/QTemporaryDir) + OpenCV (CV_32F Mat); HAVE_OPENCV un-guards
# the header.
pp_add_test(ball_baseline_io_test
    SOURCES ball_baseline_io_test.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    DEFINES HAVE_OPENCV)

# Clubhead Stage-2 (Phase B): the per-frame gap-tolerant on-axis terminus
# (measureHeadRadius, rayEdgeRadius, the B2 ball-length model) and the segmented
# KF + per-segment RTS temporal model (HeadKf1D, runHeadTemporal). Both synthetic
# — the measure test builds tiny CV_32F frames; the temporal test is frameless.
pp_add_test(clubhead_measure_test
    SOURCES clubhead_measure_test.cpp ${ANALYSIS}/clubhead_track.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    COMPILE_OPTIONS -O2)
pp_add_test(clubhead_temporal_test
    SOURCES clubhead_temporal_test.cpp ${ANALYSIS}/clubhead_track.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    COMPILE_OPTIONS -O2)

# Stage A true-onset segmentation (swing_span_bounding_plan.md §4/§7.1): the bs0
# walk-back (A1 speed + A2 φ), the impact clamp (A3), the swLow=0 legacy path,
# and the estimateSwingSpanUs Stage B helper — synthetic grip tracks, no fixture.
# ball_anchor.cpp resolves medianGripBallLenPx, referenced from shaft_track_assembly.
pp_add_test(swing_onset_test
    SOURCES swing_onset_test.cpp ${ANALYSIS}/shaft_track_assembly.cpp ${ANALYSIS}/shaft_tracker_math.cpp ${ANALYSIS}/ball_anchor.cpp ${ANALYSIS}/clubhead_track.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    COMPILE_OPTIONS -O2)

# shaft_parity_test (numeric parity vs the Python exemplar) was retired
# 2026-07-09: the C++ tracker is the development reference and has diverged
# (onset v2). Parity was the port-time acceptance gate only; regression
# baselines are now C++-vs-C++ (SwingLab runs on recorded corpora).
