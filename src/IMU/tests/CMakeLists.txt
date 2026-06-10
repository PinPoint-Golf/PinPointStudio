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

set(CMAKE_CXX_STANDARD 17)
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
