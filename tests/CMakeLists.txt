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
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/PinPointCcache.cmake)   # every compile through ccache
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

# --- The store-isolation lint (#187, #258) ------------------------------------
# Written after every suite above is declared (and after their late target_sources), into store_isolation_manifest.txt:
#   T <target>   every executable and in-tree library target (IMPORTED ones are not in BUILDSYSTEM_TARGETS)
#   K <type>     its TYPE (EXECUTABLE, STATIC_LIBRARY, OBJECT_LIBRARY, INTERFACE_LIBRARY, ...)
#   S <source>   each of its SOURCES and INTERFACE_SOURCES, made absolute; a source inside a generator expression is
#                listed by the path inside it, whatever the condition (stricter, never looser)
#   L <target>   each in-tree target it links: LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES ($<LINK_ONLY:..> and other
#                generator expressions opened, ALIAS targets resolved) and every $<TARGET_OBJECTS:..> in its sources
#   G <text>     a generator expression it could not read a path or a target from (printed by the lint)
#   X <test>     every test name
# store_isolation_lint_test reads it at run time, follows the L lines transitively, and fails when an executable
# reaches code that calls ppSettings() without making a pinpoint::test::IsolatedStores FIRST
# (src/Core/tests/isolated_stores.h). Plain CMake, so it is the same under Ninja (the Mac) and NMake JOM (the studio PC).
function(pp_store_manifest_link_item item out)
    set(_found "")
    if(item MATCHES "^::@")                       # the directory marker of a target_link_libraries from elsewhere
        set(${out} "" PARENT_SCOPE)
        return()
    endif()
    if(item MATCHES "\\$<")
        string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" _cands "${item}")
    else()
        set(_cands "${item}")
    endif()
    foreach(_c IN LISTS _cands)
        if(NOT TARGET "${_c}")
            continue()
        endif()
        get_target_property(_real "${_c}" ALIASED_TARGET)
        if(_real)
            set(_c "${_real}")
        endif()
        get_target_property(_imp "${_c}" IMPORTED)
        if(NOT _imp)
            list(APPEND _found "${_c}")
        endif()
    endforeach()
    set(${out} "${_found}" PARENT_SCOPE)
endfunction()
# The path inside a generator-expression source, whatever its conditions: each innermost $<K:V> becomes V when V names
# a file (holds a / or a .), else 1 (a condition), until no $< is left. Empty when it cannot be read.
function(pp_store_manifest_genex_path s out)
    set(_v "${s}")
    foreach(_i RANGE 32)
        if(NOT _v MATCHES "\\$<([^$<>]*)>")
            break()
        endif()
        set(_whole "${CMAKE_MATCH_0}")
        set(_body "${CMAKE_MATCH_1}")
        string(FIND "${_body}" ":" _colon)
        set(_val "1")
        if(_colon GREATER_EQUAL 0)
            math(EXPR _from "${_colon} + 1")
            string(SUBSTRING "${_body}" ${_from} -1 _val)
            if(NOT _val MATCHES "[/.]")
                set(_val "1")
            endif()
        endif()
        string(REPLACE "${_whole}" "${_val}" _v "${_v}")
    endforeach()
    if(_v MATCHES "[$<>]")
        set(_v "")
    endif()
    set(${out} "${_v}" PARENT_SCOPE)
endfunction()
function(pp_store_manifest_walk dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(NOT _type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|INTERFACE_LIBRARY)$")
            continue()
        endif()
        get_target_property(_sdir ${_t} SOURCE_DIR)
        string(APPEND PP_STORE_MANIFEST "T ${_t}\nK ${_type}\n")
        set(_srcs "")
        set(_links "")
        foreach(_p SOURCES INTERFACE_SOURCES)
            get_target_property(_v ${_t} ${_p})
            if(_v)
                list(APPEND _srcs ${_v})
            endif()
        endforeach()
        foreach(_p LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_v ${_t} ${_p})
            if(_v)
                list(APPEND _links ${_v})
            endif()
        endforeach()
        foreach(_s IN LISTS _srcs)
            if(_s MATCHES "\\$<")
                string(REGEX MATCHALL "TARGET_OBJECTS:[^>]+" _objs "${_s}")
                if(_objs)
                    foreach(_o IN LISTS _objs)
                        string(REPLACE "TARGET_OBJECTS:" "" _o "${_o}")
                        list(APPEND _links "${_o}")
                    endforeach()
                    continue()
                endif()
                # $<$<cond>:path> and the like: the path, whatever the condition; a G line when there is none, or
                # it names no file.
                pp_store_manifest_genex_path("${_s}" _inner)
                if(_inner STREQUAL "")
                    string(APPEND PP_STORE_MANIFEST "G ${_s}\n")
                    continue()
                endif()
                cmake_path(ABSOLUTE_PATH _inner BASE_DIRECTORY "${_sdir}" NORMALIZE OUTPUT_VARIABLE _abs)
                if(NOT EXISTS "${_abs}")
                    string(APPEND PP_STORE_MANIFEST "G ${_s}\n")
                    continue()
                endif()
                set(_s "${_abs}")
            endif()
            cmake_path(ABSOLUTE_PATH _s BASE_DIRECTORY "${_sdir}" NORMALIZE OUTPUT_VARIABLE _abs)
            string(APPEND PP_STORE_MANIFEST "S ${_abs}\n")
        endforeach()
        set(_deps "")
        foreach(_l IN LISTS _links)
            pp_store_manifest_link_item("${_l}" _r)
            list(APPEND _deps ${_r})
        endforeach()
        list(REMOVE_DUPLICATES _deps)
        foreach(_d IN LISTS _deps)
            string(APPEND PP_STORE_MANIFEST "L ${_d}\n")
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
