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

# Anatomical calibration solve (A·q·M) + end-to-end keystone golden (header-only:
# src/IMU/imu_calibration.h + src/Analysis/wrist_angles.h).
pp_add_test(imu_calibration_test  imu_calibration_test.cpp)

# Live wrist-angle MATH CONTRACT (mirrors live_wrist_angles.cpp tick(); header-only).
pp_add_test(live_wrist_angles_test  live_wrist_angles_test.cpp)

# Stored IMU sample frame — makeImuSample (= the exported swing.json vector frame).
pp_add_test(imu_sample_test  imu_sample_test.cpp)

# Viz binding chains: worldToScene() basis change + ArmVizView/ArmBoneController
# rendered-bone math (header-only: src/Gui/viz_frame.h).
pp_add_test(viz_frame_test  viz_frame_test.cpp)

# Phase segmenter + metric extractor over a synthetic swing.
pp_add_test(pipeline_test      pipeline_test.cpp
                               ${ANALYSIS}/phase_segmenter.cpp
                               ${ANALYSIS}/metric_extractor.cpp)

# Banded weighted-geometric-mean scorer.
pp_add_test(swing_scorer_test  swing_scorer_test.cpp
                               ${ANALYSIS}/swing_scorer.cpp)

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
