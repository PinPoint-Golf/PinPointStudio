# Standalone CTest suite for the M1 shot-analyzer (spans src/Analysis + the Export
# persistence writer/reader). Self-contained — NOT part of the app build (the root
# CMake keeps BUILD_TESTING OFF), mirroring src/Buffer/tests.
#
# Configure + run:
#   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
#   cmake --build build/analyzer-tests -j
#   ctest --test-dir build/analyzer-tests --output-on-failure
#
# The tests are self-contained (own main() + CHECK_NEAR; no googletest). They compile
# the handful of source .cpp directly — there is no pinpoint_analysis library — and link
# only Qt6 Core/Gui. Quoted includes resolve relative to each source file's directory; the
# one cross-tree need is src/Buffer (types.h, pulled in by swing_analysis.h).

cmake_minimum_required(VERSION 3.16)
project(pinpoint_analyzer_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC's <cmath> does not expose M_PI without this; the vendored imu_ekf ESKF.cpp
# (reached via eskf_orientation_filter.cpp) relies on it. The app target defines
# the same (see root CMakeLists.txt).
if(MSVC)
    add_compile_definitions(_USE_MATH_DEFINES)
    # The shaft_tracker/shaft_track timing targets request -O2 (→ /O2 on cl.exe),
    # which is incompatible with the /RTC1 runtime checks a Debug configuration
    # injects (cl error D8016). Drop /RTC1 for this test suite.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
enable_testing()

set(ANALYSIS ${CMAKE_CURRENT_SOURCE_DIR}/..)   # src/Analysis
set(SRC      ${ANALYSIS}/..)                    # src
set(BUFFER   ${SRC}/Buffer)                     # types.h

function(pp_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${BUFFER})
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Pure angle math (header-only).
pp_add_test(wrist_angles_test  wrist_angles_test.cpp)

# Wrist Motion assessment engine — Phase 0 windowed-median sampler over the input contract,
# driven by the synthetic golden fixtures (all header-only: wrist_assessment_types/contract.h,
# wrist_angle_sampler.h, wrist_assessment_fixtures.h).
pp_add_test(wrist_angle_sampling_test  wrist_angle_sampling_test.cpp)

# Wrist Motion assessment engine — the assess() pipeline: Tier-1 banding + Tier-2 rules + score v2.
# The engine .cpp pulls in the rule registry, so every engine-linked test compiles both.
set(WRIST_ENGINE ${ANALYSIS}/reference_bands.cpp
                 ${ANALYSIS}/wrist_assessment_engine.cpp
                 ${ANALYSIS}/assessment_rules.cpp)

pp_add_test(reference_bands_test     reference_bands_test.cpp ${ANALYSIS}/reference_bands.cpp)
pp_add_test(archetype_bands_test     archetype_bands_test.cpp     ${WRIST_ENGINE})
pp_add_test(tier1_banding_test       tier1_banding_test.cpp       ${WRIST_ENGINE})

# Live adapter: shotReplay.analysisDetail (QVariantMap) → engine contract → assess.
pp_add_test(wrist_analysis_adapter_test  wrist_analysis_adapter_test.cpp
                                         ${ANALYSIS}/wrist_analysis_adapter.cpp ${WRIST_ENGINE})

# Tier-2 rule engine — golden fault/strength suite + relationships + confidence + score v2.
pp_add_test(fault_rules_test         fault_rules_test.cpp         ${WRIST_ENGINE})
pp_add_test(strengths_test           strengths_test.cpp           ${WRIST_ENGINE})
pp_add_test(relationship_rules_test  relationship_rules_test.cpp  ${WRIST_ENGINE})
pp_add_test(confidence_test          confidence_test.cpp          ${WRIST_ENGINE})
pp_add_test(composite_score_v2_test  composite_score_v2_test.cpp  ${WRIST_ENGINE})

# Anatomical calibration solve (A·q·M) + end-to-end keystone golden (header-only:
# src/IMU/imu_calibration.h + src/Analysis/wrist_angles.h).
pp_add_test(imu_calibration_test  imu_calibration_test.cpp)

# Live wrist-angle MATH CONTRACT (mirrors live_wrist_angles.cpp tick(); header-only).
pp_add_test(live_wrist_angles_test  live_wrist_angles_test.cpp)

# Stored IMU sample frame — makeImuSample (= the exported swing.json vector frame).
pp_add_test(imu_sample_test  imu_sample_test.cpp)

# Viz binding chains: worldToScene() basis change + ArmVizView rendered-bone
# math (header-only: src/Gui/viz_frame.h).
pp_add_test(viz_frame_test  viz_frame_test.cpp)

# Segmentation v2/v3 signal preparation (G0): zero-phase filtering, envelopes,
# world-frame gyro, swing-plane axis, inclination, stillness, sub-grid refine.
pp_add_test(phase_signals_test phase_signals_test.cpp
                               ${ANALYSIS}/phase_signals.cpp)

# PhaseSegmenter v2 ladder (G1): synthetic full swing with FD-consistent gyro;
# adversarial waggle/regrip/truncation/degradation/grid-rate cases.
pp_add_test(phase_segmenter_test phase_segmenter_test.cpp
                                 ${ANALYSIS}/phase_segmenter.cpp
                                 ${ANALYSIS}/phase_signals.cpp)

# Phase segmenter + metric extractor over a synthetic swing.
# metric_extractor.cpp carries resource-profiler instrumentation, so it pulls in
# the profiler core (pp_profiler/pp_os_metrics/PpStatsLog) and a PpLogStream
# (imu_test_stubs, which also swallows the log without whisper/ggml).
pp_add_test(pipeline_test      pipeline_test.cpp
                               ${ANALYSIS}/phase_segmenter.cpp
                               ${ANALYSIS}/phase_signals.cpp
                               ${ANALYSIS}/metric_extractor.cpp
                               ${SRC}/Core/pp_profiler.cpp
                               ${SRC}/Core/pp_os_metrics.cpp
                               ${SRC}/Core/PpStatsLog.cpp
                               imu_test_stubs.cpp)
target_include_directories(pipeline_test PRIVATE ${SRC}/Core)   # pp_debug.h for the stub
# imu_test_stubs.cpp pulls event_buffer.h → swing_window.h (std::span) → needs C++20,
# matching the IMU driver-stack tests that share the stub.
set_target_properties(pipeline_test PROPERTIES CXX_STANDARD 20)

# Banded weighted-geometric-mean scorer.
pp_add_test(swing_scorer_test  swing_scorer_test.cpp
                               ${ANALYSIS}/swing_scorer.cpp)

# Session-metadata aggregation for the review drawer (header-only: src/Gui/session_summary.h).
pp_add_test(session_summary_test  session_summary_test.cpp)

# Shot-detection arbiter — candidate→hold→fuse→commit decisions (header-only:
# src/Gui/shot_arbiter.h).
pp_add_test(arbiter_test  arbiter_test.cpp)

# Unified swing.json writer/reader round-trip (Export subsystem).
pp_add_test(swing_doc_test     ${SRC}/Export/tests/swing_doc_test.cpp
                               ${SRC}/Export/swing_doc.cpp)

# --- Orientation filters (Phase 0.4): Madgwick (header-only) + ESKF (Eigen-backed) ---
# The ESKF .cpp pulls the vendored hobbeshunter/IMU_EKF headers (third_party/imu_ekf)
# through the Arduino-style <Eigen.h>/<Wire.h> shims in imu_ekf_compat (which MUST
# precede imu_ekf on the include path), and those need real Eigen. Eigen is not a system
# package here, so resolve it from, in order: an explicit -DEIGEN_INCLUDE_DIR; the app
# build's existing FetchContent copy (build/*/_deps/eigen-src); or a 3.4.0 fetch.
set(REPO ${SRC}/..)                               # repo root
set(EIGEN_INCLUDE_DIR "" CACHE PATH "Eigen include root (dir containing Eigen/Dense)")
if(NOT EIGEN_INCLUDE_DIR)
    file(GLOB _eigen_candidates ${REPO}/build/*/_deps/eigen-src)
    foreach(_c ${_eigen_candidates})
        if(EXISTS ${_c}/Eigen/Dense)
            set(EIGEN_INCLUDE_DIR ${_c})
            break()
        endif()
    endforeach()
endif()
if(NOT EIGEN_INCLUDE_DIR)
    message(STATUS "Eigen not found under build/*/_deps — fetching 3.4.0")
    include(FetchContent)
    FetchContent_Declare(eigen GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git GIT_TAG 3.4.0)
    FetchContent_MakeAvailable(eigen)
    set(EIGEN_INCLUDE_DIR ${eigen_SOURCE_DIR})
endif()
message(STATUS "orientation_filter_test Eigen include: ${EIGEN_INCLUDE_DIR}")

pp_add_test(orientation_filter_test  orientation_filter_test.cpp
                                     ${SRC}/IMU/eskf_orientation_filter.cpp)
target_include_directories(orientation_filter_test PRIVATE
    ${REPO}/third_party/imu_ekf_compat    # <Eigen.h>/<Wire.h> shims — MUST precede imu_ekf
    ${REPO}/third_party/imu_ekf           # ESKF.h, Quaternion.h
    ${EIGEN_INCLUDE_DIR})

# --- IMU driver frame-parse + WT901BLE67 eulerToQuat override (Phase 0.3) ---
# Heaviest test: exercises the REAL driver stack. parseQuaternion is reached via
# receiveData(); the eulerToQuat override needs WT9011DCL_BLE (its BleImuTransport ctor
# is trivial / headless-safe, so no adapter is required). imu_test_stubs.cpp supplies
# EventBuffer::nowMicros (Buffer) and PpLogStream (pp_debug) so we link NEITHER the
# Buffer library NOR the whisper-dependent pp_debug.cpp. The Q_OBJECT driver classes
# need moc, hence per-target AUTOMOC.
find_package(Qt6 REQUIRED COMPONENTS Bluetooth)
add_executable(imu_driver_frame_test
    imu_driver_frame_test.cpp
    imu_test_stubs.cpp
    ${SRC}/IMU/wt9011dcl_ble.cpp
    ${SRC}/IMU/ble_imu_transport.cpp
    ${SRC}/IMU/wt9011dcl_base.cpp
    ${SRC}/IMU/imu_base.cpp
    ${SRC}/IMU/eskf_orientation_filter.cpp)
# event_buffer.h → swing_window.h uses std::span, so this target needs C++20 (the app
# standard), unlike the C++17 header-only tests above.
set_target_properties(imu_driver_frame_test PROPERTIES
    AUTOMOC ON CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(imu_driver_frame_test PRIVATE
    ${SRC}/IMU ${SRC}/Core ${BUFFER}
    ${REPO}/third_party/imu_ekf_compat ${REPO}/third_party/imu_ekf ${EIGEN_INCLUDE_DIR})
target_link_libraries(imu_driver_frame_test PRIVATE Qt6::Core Qt6::Gui Qt6::Bluetooth)
add_test(NAME imu_driver_frame_test COMMAND imu_driver_frame_test)

# --- ShaftTracker OpenCV targets (the only OpenCV-dependent targets here) ---
# OpenCV (system 4.x, core imgproc only — no contrib) is found and linked for
# these two targets ONLY; the Qt-only targets above are untouched. Both sources
# are Qt-free, so no Qt libraries are linked either.
find_package(OpenCV REQUIRED COMPONENTS core imgproc)

# Shared frame decode (src/Export/frame_decode, ShaftTracker S0): synthetic
# NV12/YUV420P/BGRA32/Bayer8 payloads with golden-pixel checks.
add_executable(frame_decode_test
    frame_decode_test.cpp
    ${SRC}/Export/frame_decode.cpp)
target_include_directories(frame_decode_test PRIVATE ${BUFFER} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(frame_decode_test PRIVATE ${OpenCV_LIBS})
add_test(NAME frame_decode_test COMMAND frame_decode_test)

# ShaftTracker S1: anchored radial shaft detection math core. Added manually
# rather than via pp_add_test (that helper links Qt, which this target neither
# needs nor wants). -O2 because the suite sets no build type and the timing
# assertion measures real hot-loop cost.
add_executable(shaft_tracker_test
    shaft_tracker_test.cpp
    ${ANALYSIS}/shaft_tracker_math.cpp)
target_include_directories(shaft_tracker_test PRIVATE ${OpenCV_INCLUDE_DIRS})
target_compile_options(shaft_tracker_test PRIVATE -O2)
target_link_libraries(shaft_tracker_test PRIVATE ${OpenCV_LIBS})
add_test(NAME shaft_tracker_test COMMAND shaft_tracker_test)

# ShaftTracker S2: track assembly (ŝ_hand calibration + Viterbi association +
# wrap-aware 2-channel KF/RTS) over synthetic candidate sequences. Needs Qt
# (QQuaternion/QPointF) AND OpenCV core (Matx/Vec) — both linked here only.
add_executable(shaft_track_test
    shaft_track_test.cpp
    ${ANALYSIS}/shaft_track_assembly.cpp)
target_include_directories(shaft_track_test PRIVATE ${BUFFER} ${OpenCV_INCLUDE_DIRS})
target_compile_options(shaft_track_test PRIVATE -O2)
target_link_libraries(shaft_track_test PRIVATE Qt6::Core Qt6::Gui ${OpenCV_LIBS})
add_test(NAME shaft_track_test COMMAND shaft_track_test)

# ShaftTracker R6 kinematics (lead-arm → club wrist-cock model) — pure header-only
# (no Qt, no OpenCV). K0 builds an empty passing stub; the seed-table / branch-flip /
# angle-envelope assertions arrive in the K2a phase (skeleton-aware enhancement).
add_executable(shaft_kinematics_test shaft_kinematics_test.cpp)
add_test(NAME shaft_kinematics_test COMMAND shaft_kinematics_test)
