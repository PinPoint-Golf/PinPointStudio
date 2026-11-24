# PinPoint umbrella test project (DRAFT SKELETON — Option A).
#
# One configure / one build / one ctest for every standalone unit-test suite,
# WITHOUT building the app (no whisper/ggml, FFmpeg, espeak, OpenCV-app, or QML
# module). Each suite still recompiles only the .cpp it needs, exactly as today.
#
#   cmake -S tests -B build/tests          # configure all suites
#   cmake --build build/tests -j6          # build all tests
#   ctest --test-dir build/tests --output-on-failure   # run all tests
#
# Or via the presets in tests/CMakePresets.json (run from THIS dir — cmake reads
# CMakePresets.json from the current directory):
#   cd tests
#   cmake --preset tests && cmake --build --preset tests && ctest --preset tests
#   # tests-asan / tests-tsan presets build the suites under sanitizers.

# 3.21, not 3.16: the Gui suite declares a QML module (qt_add_qml_module) and Qt's QML
# macros need the newer policy set. CMakePresets.json already declared 3.21.
cmake_minimum_required(VERSION 3.21)
project(pinpoint_tests LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PinPointTests)   # Qt prefix, Eigen locate, sanitizers, gtest, pp_add_test

enable_testing()

# --- Buffer is special -------------------------------------------------------
# Its tests live under src/Buffer/tests but are PROJECT_IS_TOP_LEVEL-gated and
# need the real `pinpoint_buffer` library target. Build the library first, then
# pull its tests in directly. (Post-migration, IMU/tests must reuse this target
# instead of add_subdirectory-ing Buffer again — see MIGRATION.md.)
add_subdirectory("${PP_SRC}/Buffer"       "${CMAKE_BINARY_DIR}/Buffer-lib")
add_subdirectory("${PP_SRC}/Buffer/tests" "${CMAKE_BINARY_DIR}/Buffer")

# --- The remaining standalone suites -----------------------------------------
foreach(suite Analysis Audio Core Gui IMU LaunchMonitor Pose Ppcp Update)
    add_subdirectory("${PP_SRC}/${suite}/tests" "${CMAKE_BINARY_DIR}/${suite}")
endforeach()

# --- The store-isolation lint (#187) ------------------------------------------
# Written after every suite above is declared (and after their late target_sources): each executable target with its
# sources made absolute, and every test name. store_isolation_lint_test reads it at run time and fails when a suite
# compiles code that calls ppSettings() and makes no pinpoint::test::IsolatedStores (src/Core/tests/isolated_stores.h).
# Plain CMake, so it is the same under Ninja (the Mac) and NMake JOM (the studio PC).
function(pp_store_manifest_walk dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(NOT _type STREQUAL "EXECUTABLE")
            continue()
        endif()
        get_target_property(_srcs ${_t} SOURCES)
        get_target_property(_sdir ${_t} SOURCE_DIR)
        string(APPEND PP_STORE_MANIFEST "T ${_t}\n")
        foreach(_s IN LISTS _srcs)
            if(_s MATCHES "\\$<")
                continue()
            endif()
            cmake_path(ABSOLUTE_PATH _s BASE_DIRECTORY "${_sdir}" NORMALIZE OUTPUT_VARIABLE _abs)
            string(APPEND PP_STORE_MANIFEST "S ${_abs}\n")
        endforeach()
    endforeach()
    get_property(_tests DIRECTORY "${dir}" PROPERTY TESTS)
    foreach(_x IN LISTS _tests)
        string(APPEND PP_STORE_MANIFEST "X ${_x}\n")
    endforeach()
    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_d IN LISTS _subs)
        pp_store_manifest_walk("${_d}")
    endforeach()
    set(PP_STORE_MANIFEST "${PP_STORE_MANIFEST}" PARENT_SCOPE)
endfunction()
set(PP_STORE_MANIFEST "")
pp_store_manifest_walk("${CMAKE_CURRENT_SOURCE_DIR}")
file(WRITE "${CMAKE_BINARY_DIR}/store_isolation_manifest.txt" "${PP_STORE_MANIFEST}")
pp_add_test(store_isolation_lint_test
    SOURCES ${PP_SRC}/Core/tests/store_isolation_lint_test.cpp
    DEFINES PP_STORE_MANIFEST="${CMAKE_BINARY_DIR}/store_isolation_manifest.txt")
