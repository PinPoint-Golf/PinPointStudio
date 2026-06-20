# Standalone CTest suite for the IMU impact detector (shot detection P1).
# Self-contained — NOT part of the app build (the root CMake keeps
# BUILD_TESTING OFF), mirroring src/Analysis/tests and src/Buffer/tests.
#
# Configure + run:
#   cmake -S src/IMU/tests -B build/imu-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
#   cmake --build build/imu-tests -j
#   ctest --test-dir build/imu-tests --output-on-failure
#
# The tests are self-contained (own main() + printf CHECK macros; no
# googletest). impact_detector.h is pure math (<cmath>/<array> only), so the
# test needs neither BLE nor an app build; Qt is linked only for convention
# parity with the other standalone suites.

cmake_minimum_required(VERSION 3.16)
project(pinpoint_imu_tests LANGUAGES CXX)

# C++20: the EventBuffer headers (std::span) pulled in by imu_io_worker_test.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
enable_testing()

function(pp_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Impact-detector truth table: strike fires once; mat tap / waggle / slow
# swell rejected; refractory; orientation gate; back-dated est_t; 100↔200 Hz
# rate independence.
pp_add_test(impact_detector_test  impact_detector_test.cpp)

# ── ImuIoWorker (IMU I/O thread migration, stage W0) ─────────────────────────
# The per-packet hot path extracted from ImuInstance, validated across real
# threads against the real EventBuffer: snapshot coherence, atomic calibration
# swap, ring writes + the detachBuffer stop barrier, impact detection with a
# deterministic clock, attach/detach/deregister churn.
#
#   -DIMU_TESTS_TSAN=ON   build the suite under ThreadSanitizer
option(IMU_TESTS_TSAN "Build the IMU tests under ThreadSanitizer" OFF)

set(CMAKE_AUTOMOC ON)   # ImuIoWorker is a Q_OBJECT

# The real lock-free EventBuffer, as a subproject (its own tests stay out of
# this build — they are PROJECT_IS_TOP_LEVEL-gated).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Buffer pinpoint_buffer)

pp_add_test(imu_io_worker_test
    imu_io_worker_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../imu_io_worker.cpp)
target_include_directories(imu_io_worker_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(imu_io_worker_test PRIVATE pinpoint_buffer)

if(IMU_TESTS_TSAN)
    foreach(t imu_io_worker_test pinpoint_buffer)
        target_compile_options(${t} PRIVATE -fsanitize=thread -g -O1)
        target_link_options(${t}    PRIVATE -fsanitize=thread)
    endforeach()
endif()

# ── EskfOrientationFilter gyro-unit pin (R2-7) ───────────────────────────────
# Pins the rad/s gyro convention through the vendored ESKF (correctGyr converts
# deg/s internally; the adapter pre-scales rad→deg). Needs Eigen + the vendored
# imu_ekf headers, so it is GATED on Eigen being present — the lean detector
# suite still configures offline without it. PINPOINT_EIGEN_DIR defaults to the
# main build's FetchContent copy; override -DPINPOINT_EIGEN_DIR=... if elsewhere.
set(PINPOINT_EIGEN_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/Desktop_Qt_6_11_0-Debug/_deps/eigen-src"
    CACHE PATH "Eigen source tree (for the ESKF unit test)")
if(EXISTS "${PINPOINT_EIGEN_DIR}/Eigen/Core")
    pp_add_test(eskf_gyro_units_test
        eskf_gyro_units_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../eskf_orientation_filter.cpp)
    target_include_directories(eskf_gyro_units_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${PINPOINT_EIGEN_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/imu_ekf_compat
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/imu_ekf)
else()
    message(STATUS "Eigen not found at ${PINPOINT_EIGEN_DIR} — skipping eskf_gyro_units_test")
endif()
