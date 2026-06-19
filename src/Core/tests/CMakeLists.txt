# Standalone CTest suite for the resource profiler (src/Core/pp_profiler,
# pp_os_metrics, PpStatsLog + the Gui profiler_controller bridge). Self-contained
# — NOT part of the app build — mirroring src/Buffer/tests and src/Analysis/tests.
#
# Configure + run:
#   cmake -S src/Core/tests -B build/core-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
#   cmake --build build/core-tests -j
#   ctest --test-dir build/core-tests --output-on-failure
#
# Sanitizers (separate build dirs):
#   cmake -S src/Core/tests -B build/core-tests-asan -DPINPOINT_ENABLE_ASAN=ON -DPINPOINT_ENABLE_UBSAN=ON ...
#   cmake -S src/Core/tests -B build/core-tests-tsan -DPINPOINT_ENABLE_TSAN=ON ...
#
# Each profiler test compiles the Core sources it needs directly (there is no
# pinpoint_core library) + a PpLogStream stub (pp_log_stub.cpp) so we avoid the
# whisper/ggml-dependent pp_debug.cpp. Tests build with BOTH tiers ON so the deep
# tier is exercised; the compile-out test builds with both OFF.

cmake_minimum_required(VERSION 3.16)
project(pinpoint_core_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)               # profiler_controller is a Q_OBJECT

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)
enable_testing()

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Standalone copy of the Buffer helper (this suite is configured on its own).
function(pinpoint_apply_sanitizers target)
    if(PINPOINT_ENABLE_ASAN)
        target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address)
    endif()
    if(PINPOINT_ENABLE_UBSAN)
        target_compile_options(${target} PRIVATE -fsanitize=undefined)
        target_link_options(${target} PRIVATE -fsanitize=undefined)
    endif()
    if(PINPOINT_ENABLE_TSAN)
        target_compile_options(${target} PRIVATE -fsanitize=thread)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

set(CORE    ${CMAKE_CURRENT_SOURCE_DIR}/..)        # src/Core
set(MONITOR ${CORE}/../Gui/monitor)                # src/Gui/monitor

# The profiler core + a PpLogStream stub, compiled directly into each test.
set(PROFILER_CORE
    ${CORE}/pp_profiler.cpp
    ${CORE}/pp_os_metrics.cpp
    ${CORE}/PpStatsLog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pp_log_stub.cpp)

# Helper: a profiler test with BOTH tiers compiled in (deep exercised).
function(pp_core_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CORE})
    target_compile_definitions(${name} PRIVATE PINPOINT_PROFILE_BASELINE=1 PINPOINT_PROFILE=1)
    target_link_libraries(${name} PRIVATE Qt6::Core Threads::Threads GTest::gtest_main)
    pinpoint_apply_sanitizers(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

pp_core_test(pp_profiler_test             pp_profiler_test.cpp             ${PROFILER_CORE})
pp_core_test(pp_os_metrics_test           pp_os_metrics_test.cpp          ${CORE}/pp_os_metrics.cpp)
pp_core_test(pp_stats_log_test            pp_stats_log_test.cpp           ${CORE}/PpStatsLog.cpp)
pp_core_test(pp_profiler_concurrency_test pp_profiler_concurrency_test.cpp ${PROFILER_CORE})

# GPU metrics — graceful-degradation contract (verifiable without a GPU). Pure
# C++ + dlopen (no Qt, no profiler core); the Metal backend is an ObjC++ .mm on
# Apple. Linked against ${CMAKE_DL_LIBS} for dlopen on glibc < 2.34.
add_executable(pp_gpu_metrics_test pp_gpu_metrics_test.cpp ${CORE}/pp_gpu_metrics.cpp)
target_include_directories(pp_gpu_metrics_test PRIVATE ${CORE})
target_link_libraries(pp_gpu_metrics_test PRIVATE Threads::Threads GTest::gtest_main ${CMAKE_DL_LIBS})
if(APPLE)
    target_sources(pp_gpu_metrics_test PRIVATE ${CORE}/pp_gpu_metrics_mac.mm)
    set_source_files_properties(${CORE}/pp_gpu_metrics_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(pp_gpu_metrics_test PRIVATE "-framework Metal" "-framework Foundation")
elseif(WIN32)
    target_link_libraries(pp_gpu_metrics_test PRIVATE dxgi)
endif()
pinpoint_apply_sanitizers(pp_gpu_metrics_test)
add_test(NAME pp_gpu_metrics_test COMMAND pp_gpu_metrics_test)

# Zero-overhead claim: BOTH tiers OFF → every macro expands to ((void)0).
add_executable(pp_profiler_compileout_test pp_profiler_compileout_test.cpp ${PROFILER_CORE})
target_include_directories(pp_profiler_compileout_test PRIVATE ${CORE})
target_compile_definitions(pp_profiler_compileout_test PRIVATE PINPOINT_PROFILE_BASELINE=0 PINPOINT_PROFILE=0)
target_link_libraries(pp_profiler_compileout_test PRIVATE Qt6::Core Threads::Threads GTest::gtest_main)
pinpoint_apply_sanitizers(pp_profiler_compileout_test)
add_test(NAME pp_profiler_compileout_test COMMAND pp_profiler_compileout_test)

# Phase 2 controller — Q_OBJECT + QTimer, so it needs a QCoreApplication: the
# test supplies its own main(), hence GTest::gtest (not gtest_main).
add_executable(profiler_controller_test
    profiler_controller_test.cpp
    ${MONITOR}/profiler_controller.cpp
    ${CORE}/pp_gpu_metrics.cpp   # profiler_controller.cpp calls gpumetrics::init/sample/reset
    ${PROFILER_CORE})
target_include_directories(profiler_controller_test PRIVATE ${CORE} ${MONITOR})
target_compile_definitions(profiler_controller_test PRIVATE PINPOINT_PROFILE_BASELINE=1 PINPOINT_PROFILE=1)
target_link_libraries(profiler_controller_test PRIVATE Qt6::Core Threads::Threads GTest::gtest)
if(APPLE)
    target_sources(profiler_controller_test PRIVATE ${CORE}/pp_gpu_metrics_mac.mm)
    set_source_files_properties(${CORE}/pp_gpu_metrics_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(profiler_controller_test PRIVATE "-framework Metal" "-framework Foundation")
elseif(WIN32)
    target_link_libraries(profiler_controller_test PRIVATE dxgi)
endif()
pinpoint_apply_sanitizers(profiler_controller_test)
add_test(NAME profiler_controller_test COMMAND profiler_controller_test)
