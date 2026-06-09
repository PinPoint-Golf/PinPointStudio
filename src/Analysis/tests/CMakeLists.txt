# Standalone CTest suite for the M1 shot-analyzer (spans src/Analysis + the Export
# persistence writer/reader). Self-contained — NOT part of the app build (the root
# CMake keeps BUILD_TESTING OFF), mirroring src/Buffer/tests.
#
# Configure + run:
#   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
#   cmake --build build/analyzer-tests -j
#   ctest --test-dir build/analyzer-tests --output-on-failure
#
# The tests are self-contained (own main() + CHECK_NEAR; no googletest). They compile
# the handful of source .cpp directly — there is no pinpoint_analysis library — and link
# only Qt6 Core/Gui. Quoted includes resolve relative to each source file's directory; the
# one cross-tree need is src/Buffer (types.h, pulled in by swing_analysis.h).

cmake_minimum_required(VERSION 3.16)
project(pinpoint_analyzer_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
enable_testing()

set(ANALYSIS ${CMAKE_CURRENT_SOURCE_DIR}/..)   # src/Analysis
set(SRC      ${ANALYSIS}/..)                    # src
set(BUFFER   ${SRC}/Buffer)                     # types.h

function(pp_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${BUFFER})
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Pure angle math (header-only).
pp_add_test(wrist_angles_test  wrist_angles_test.cpp)

# Phase segmenter + metric extractor over a synthetic swing.
pp_add_test(pipeline_test      pipeline_test.cpp
                               ${ANALYSIS}/phase_segmenter.cpp
                               ${ANALYSIS}/metric_extractor.cpp)

# Banded weighted-geometric-mean scorer.
pp_add_test(swing_scorer_test  swing_scorer_test.cpp
                               ${ANALYSIS}/swing_scorer.cpp)

# Unified swing.json writer/reader round-trip (Export subsystem).
pp_add_test(swing_doc_test     ${SRC}/Export/tests/swing_doc_test.cpp
                               ${SRC}/Export/swing_doc.cpp)
