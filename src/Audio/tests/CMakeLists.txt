# Standalone CTest suite for the acoustic onset detector (shot detection P2).
# Self-contained — NOT part of the app build (the root CMake keeps
# BUILD_TESTING OFF), mirroring src/Analysis/tests and src/IMU/tests.
#
# Configure + run:
#   cmake -S src/Audio/tests -B build/audio-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
#   cmake --build build/audio-tests -j
#   ctest --test-dir build/audio-tests --output-on-failure
#
# onset_detector.h is pure math (no Qt), so the test needs no audio device;
# Qt is linked only for convention parity with the other standalone suites.

cmake_minimum_required(VERSION 3.16)
project(pinpoint_audio_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
enable_testing()

function(pp_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Onset truth table: click fires (sample-accurate); speech bursts / sustained
# tone / tone cutoff / ambient rejected; refractory; onset→est_t* math.
pp_add_test(acoustic_shot_detector_test  acoustic_shot_detector_test.cpp)
