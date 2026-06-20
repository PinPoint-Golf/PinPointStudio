# CTest suite for the acoustic onset detector (shot detection P2).
# Self-contained — NOT part of the app build. Runs standalone or under the
# tests/ umbrella; shared infra (Qt prefix, sanitizers, pp_add_test) lives in
# tests/cmake/PinPointTests.cmake.
#
#   cmake -S src/Audio/tests -B build/audio-tests
#   cmake --build build/audio-tests -j && ctest --test-dir build/audio-tests --output-on-failure

if(NOT COMMAND pp_add_test)
    cmake_minimum_required(VERSION 3.16)
    project(pinpoint_audio_tests LANGUAGES CXX)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../../tests/cmake)
    include(PinPointTests)
    enable_testing()
endif()

# Onset truth table: click fires (sample-accurate); speech bursts / sustained
# tone / tone cutoff / ambient rejected; refractory; onset→est_t* math.
# onset_detector.h is pure math; Qt is linked only for convention parity.
pp_add_test(acoustic_shot_detector_test
    SOURCES acoustic_shot_detector_test.cpp)
