# Standalone CTest suite for the calibrated ball detection core (ball_model.h).
# Self-contained — NOT part of the app build (the root CMake keeps
# BUILD_TESTING OFF), mirroring src/Analysis/tests, src/Audio/tests and
# src/IMU/tests.
#
# Configure + run:
#   cmake -S src/Pose/tests -B build/pose-tests
#   cmake --build build/pose-tests -j
#   ctest --test-dir build/pose-tests --output-on-failure
#
# ball_model.h is OpenCV-only (no Qt), so only OpenCV is required.

cmake_minimum_required(VERSION 3.16)
project(pinpoint_pose_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc)
enable_testing()

function(pp_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE ${OpenCV_LIBS})
    target_include_directories(${name} PRIVATE ${OpenCV_INCLUDE_DIRS})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Calibrated detection core: background/ball model fitting, theta derivation,
# multi-cue scoring (dim-studio golden case), gain invariance, drift severity.
pp_add_test(ball_model_test  ball_model_test.cpp)
