# CTest suite for the resource profiler (pp_profiler, pp_os_metrics, PpStatsLog,
# pp_gpu_metrics + the Gui profiler_controller bridge). Self-contained — NOT part
# of the app build. Runs standalone or under the tests/ umbrella; shared infra
# (Qt prefix, sanitizers via -DPP_SANITIZE, gtest, pp_add_test) lives in
# tests/cmake/PinPointTests.cmake.
#
#   cmake -S src/Core/tests -B build/core-tests
#   cmake --build build/core-tests -j && ctest --test-dir build/core-tests --output-on-failure
#
# The Platform files these tests need (the profiler, the OS and GPU metrics, the
# stats and analysis-run logs, the folder lock, the worker pool, and the REAL log:
# pp_log_stream.cpp + PpMessageLog.cpp) come from one library, pp_platform
# (cmake/rings/platform/, defined by tests/cmake/PinPointTests.cmake for every
# suite), not hand-listed sources, so these tests log for real.
# The tests' own DEFINES still reach only their own files, which is all they need:
# no library source reads PINPOINT_DEBUG_LEVEL or PINPOINT_PROFILE*, they change
# only the callers' macros. Tests build with BOTH profiler tiers ON so the deep
# tier is exercised; the compile-out test builds with both OFF.

if(NOT COMMAND pp_add_test)
    cmake_minimum_required(VERSION 3.16)
    project(pinpoint_core_tests LANGUAGES CXX)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../../tests/cmake)
    include(PinPointTests)
    enable_testing()
endif()

set(CORE    ${CMAKE_CURRENT_SOURCE_DIR}/..)        # src/Core
set(MONITOR ${CORE}/../Gui/monitor)                # src/Gui/monitor

# Profiler tests with BOTH tiers compiled in (deep exercised).
pp_add_test(pp_profiler_test
    SOURCES pp_profiler_test.cpp
    INCLUDE ${CORE}  LINK pp_platform
    DEFINES PINPOINT_PROFILE_BASELINE=1 PINPOINT_PROFILE=1  GTEST)
pp_add_test(pp_os_metrics_test
    SOURCES pp_os_metrics_test.cpp
    INCLUDE ${CORE}  LINK pp_platform
    DEFINES PINPOINT_PROFILE_BASELINE=1 PINPOINT_PROFILE=1  GTEST)
pp_add_test(pp_stats_log_test
    SOURCES pp_stats_log_test.cpp
    INCLUDE ${CORE}  LINK pp_platform
    DEFINES PINPOINT_PROFILE_BASELINE=1 PINPOINT_PROFILE=1  GTEST)
# The standard club table (club_standards.h) — header-only, Qt Core for QString.
pp_add_test(club_standards_test
    SOURCES club_standards_test.cpp
    INCLUDE ${CORE}  LINK Qt6::Core
    GTEST)
# The athlete form's Height field (height_text.h) — header-only, Qt Core.
pp_add_test(height_text_test
    SOURCES height_text_test.cpp
    INCLUDE ${CORE}  LINK Qt6::Core
    GTEST)
# The one atomic-write helper (atomic_write.h, header-only, R-PERS-2): replaces whole, a missing folder
# made (or refused with intoExistingFolder), the original byte-identical on any failure — an unwritable
# folder, a read-only file, a refused rename — the streaming forms, text mode. Moved here from
# Diagnostics/tests/pack_io_test with the helper (refactor step 2.0). Hand-rolled main, Qt Core only.
pp_add_test(atomic_write_test
    SOURCES atomic_write_test.cpp
    INCLUDE ${CORE})
# The test-isolation helper (isolated_stores.h, header-only, #187): with it on, ppSettings(), the user diagnostics
# folder (diagnosticsDataDir), the studio profile's folder, the Rig's folder and libraryRoot() lie inside its temporary directory, and
# the standard locations are not the real ones. It reads the app's own path helpers, so a renamed seam fails here.
# Hand-rolled main; the real log (studio_profile.cpp logs).
pp_add_test(isolated_stores_test
    SOURCES isolated_stores_test.cpp ${PP_SRC}/App/studio_profile.cpp ${PP_SRC}/Stores/studio_profile_store.cpp
            ${PP_SRC}/Stores/rig_store.cpp
            ${PP_SRC}/Stores/swing_paths.cpp   # the stores' system-trash seam (SwingPaths::systemTrashAllowed)
            ${PP_SRC}/Diagnostics/model/pack_io.cpp
    INCLUDE ${CORE}  LINK pp_platform
    AUTOMOC)
# The advisory folder lock (folder_lock.*, R-PERS-6): two lock objects on one folder — the second is
# Locked until the first lets go; a lock from another machine freed only by age; a missing folder is
# not made. Hand-rolled main, Qt Core only.
pp_add_test(folder_lock_test
    SOURCES folder_lock_test.cpp
    INCLUDE ${CORE}  LINK pp_platform)
# The one worker pool, its serial lanes and the post-to-owner receiver (work_pool.*, R-CON-5; refactor
# step 3.1, D15): a lane keeps order; two lanes do not block each other; a cancelled lane drops its waiting
# jobs; the receiver drops what it cannot resolve; the counter never repeats. Hand-rolled main, Qt Core only.
pp_add_test(work_pool_test
    SOURCES work_pool_test.cpp
    INCLUDE ${CORE}  LINK pp_platform)
# The [PostShot] step timer (post_shot_trace.h, header-only, Platform; refactor 3.7-S / 3.7-I): the line's format,
# begin/Scope/end, the thread gate, the 256-step cap, the quiet end, a window cut by the next begin(). Its own main
# (a QCoreApplication: the quiet end is a QTimer).
pp_add_test(post_shot_trace_test
    SOURCES post_shot_trace_test.cpp
    INCLUDE ${CORE}  LINK Qt6::Core Threads::Threads
    GTEST_NO_MAIN)
# ppDebug() takes no caller's `else` (pp_debug.h; platform refactor A4): one source built at both debug levels —
# level 1 (compiled away, and the old `if (false)` form shown capturing the else) and level 3 (runs). The real
# log (pp_platform's pp_log_stream.cpp + PpMessageLog.cpp) so level 3's line really runs: the level is the test's
# own, since ppDebug() expands in the caller. Hand-rolled main, Qt Core only.
foreach(level 1 3)
    pp_add_test(pp_debug_else_test_l${level}
        SOURCES pp_debug_else_test.cpp
        INCLUDE ${CORE}  LINK pp_platform
        DEFINES PINPOINT_DEBUG_LEVEL=${level})
endforeach()
pp_add_test(pp_profiler_concurrency_test
    SOURCES pp_profiler_concurrency_test.cpp
    INCLUDE ${CORE}  LINK pp_platform
    DEFINES PINPOINT_PROFILE_BASELINE=1 PINPOINT_PROFILE=1  GTEST)

# GPU metrics — graceful-degradation contract (verifiable without a GPU). The
# code under test is pure C++ + dlopen (no Qt, no profiler core); the Metal
# backend is an ObjC++ .mm on Apple. Both come from pp_platform, which also
# carries Metal/Foundation, dxgi or ${CMAKE_DL_LIBS}. NO_QT keeps the test's own
# file Qt-free; the library puts Qt Core on its link line all the same.
pp_add_test(pp_gpu_metrics_test
    SOURCES pp_gpu_metrics_test.cpp
    INCLUDE ${CORE}  LINK pp_platform
    NO_QT  GTEST)

# Zero-overhead claim: BOTH tiers OFF → every macro expands to ((void)0).
pp_add_test(pp_profiler_compileout_test
    SOURCES pp_profiler_compileout_test.cpp
    INCLUDE ${CORE}  LINK pp_platform
    DEFINES PINPOINT_PROFILE_BASELINE=0 PINPOINT_PROFILE=0  GTEST)

# Phase 2 controller — Q_OBJECT + QTimer, so it needs a QCoreApplication: the
# test supplies its own main(), hence GTEST_NO_MAIN (GTest::gtest, not gtest_main).
pp_add_test(profiler_controller_test
    SOURCES profiler_controller_test.cpp
            ${MONITOR}/profiler_controller.cpp
    INCLUDE ${CORE} ${MONITOR}  LINK pp_platform  # gpumetrics::init/sample/reset, the analysis-run ring, the profiler
    DEFINES PINPOINT_PROFILE_BASELINE=1 PINPOINT_PROFILE=1  GTEST_NO_MAIN  AUTOMOC)
