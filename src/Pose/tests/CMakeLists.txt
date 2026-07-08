# CTest suite for the ball-detection cores — v1 calibrated (ball_model.h) and v2
# temporal matched-filter (ball_temporal.h, with a byte-exact C++<->python parity
# gate) — plus the detector throttle contract. Self-contained — NOT part of the app
# build. Runs standalone or under the tests/ umbrella; shared infra lives in
# tests/cmake/PinPointTests.cmake.
#
#   cmake -S src/Pose/tests -B build/pose-tests
#   cmake --build build/pose-tests -j && ctest --test-dir build/pose-tests --output-on-failure

if(NOT COMMAND pp_add_test)
    cmake_minimum_required(VERSION 3.16)
    project(pinpoint_pose_tests LANGUAGES CXX)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../../tests/cmake)
    include(PinPointTests)
    enable_testing()
endif()

# ball_model.h is OpenCV-only (no Qt); ball_detector.cpp pulls in Qt + the profiler.
find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d)

set(POSE ${CMAKE_CURRENT_SOURCE_DIR}/..)   # src/Pose
set(CORE ${POSE}/../Core)                  # profiler core

# Calibrated detection core: background/ball model fitting, theta derivation,
# multi-cue scoring (dim-studio golden case), gain invariance, drift severity.
pp_add_test(ball_model_test
    SOURCES ball_model_test.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    NO_QT)

# Ball Detection v2 temporal matched-filter core (ball_temporal.h): DoG band-pass,
# robust noise, is_blob shape gate, sub-pixel peak, padded-crop response, and the
# SEARCH->CANDIDATE->LOCKED->VANISHED state machine over synthetic scenes.
pp_add_test(ball_temporal_test
    SOURCES ball_temporal_test.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    NO_QT)

# Ball v2 NUMERIC PARITY vs the python exemplar (ball_state_machine.py): run
# TemporalBallTracker over python's OWN dumped DoG response stack + baseline (NOT a
# C++ decode — independent OpenCV versions decode the H.264 to different pixels), a
# byte-exact test of the state machine. Diffs locked position / launch frame /
# median novelty against the python reference. SKIPs when the fixture is absent.
# Qt (JSON fixture parsing); the DoG/padding pipeline is covered by ball_temporal_test.
pp_add_test(ball_temporal_parity_test
    SOURCES ball_temporal_parity_test.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS})

# Calibration protocol: round bookkeeping (consecutive-clean pass rule,
# failed-round evidence retention, exhaustion) + profile save/load round-trip
# (FileStorage, schema-version rejection, detector-equivalence of loaded profile).
pp_add_test(ball_calibration_test
    SOURCES ball_calibration_test.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    NO_QT)

# BallDetector throttle contract: exactly one ballDetected/detectionSkipped per
# detect() on every path (legacy, calibrated, fallback, guards). Builds the real
# ball_detector.cpp (Q_OBJECT → AUTOMOC) with a PpLogStream stub (no whisper) and
# the profiler core (ball_detector.cpp carries a baseline PP_PROFILE_SCOPE).
pp_add_test(ball_detector_contract_test
    SOURCES ball_detector_contract_test.cpp
            pose_test_stubs.cpp
            ${POSE}/ball_detector.cpp
            ${CORE}/pp_profiler.cpp
            ${CORE}/pp_os_metrics.cpp
            ${CORE}/PpStatsLog.cpp
    INCLUDE ${POSE} ${CORE} ${OpenCV_INCLUDE_DIRS}
    LINK    ${OpenCV_LIBS}
    DEFINES HAVE_OPENCV
    AUTOMOC)
