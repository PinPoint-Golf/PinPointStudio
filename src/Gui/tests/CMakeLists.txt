# Standalone CTest suite for src/Gui model/helper logic — TimelineLabels (the train-map
# label solver + nearest/active searches + phase names) and SwingSeriesModel (playhead→row
# mapping). Self-contained — NOT part of the app build (mirrors src/Analysis/tests and
# src/Buffer/tests).
#
# Configure + run:
#   cmake -S src/Gui/tests -B build/gui-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
#   cmake --build build/gui-tests -j
#   ctest --test-dir build/gui-tests --output-on-failure
#
# Self-contained (own main() + printf asserts; no googletest). Compiles the handful of Gui
# .cpp directly. Qt6::Qml is required because the Gui headers use QML_ELEMENT
# (qqmlregistration.h); Qt6::Gui for QFontMetricsF/QFont used by TimelineLabels. The
# QML_ELEMENT classes carry Q_OBJECT, so each target enables AUTOMOC.

cmake_minimum_required(VERSION 3.16)
project(pinpoint_gui_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml)
enable_testing()

set(GUI    ${CMAKE_CURRENT_SOURCE_DIR}/..)   # src/Gui
set(SRC    ${GUI}/..)                         # src
set(BUFFER ${SRC}/Buffer)                     # types.h (pulled in by Gui models)

# Optional sanitizer pass: configure a separate build dir with e.g.
#   -DPP_SANITIZE=address   (ShotListModel is single-threaded, so ASan is the
# meaningful sanitizer here; thread has nothing to find).
set(PP_SANITIZE "" CACHE STRING "Sanitizer to build the test suite with (e.g. address)")

function(pp_add_test name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    target_include_directories(${name} PRIVATE ${GUI} ${GUI}/review ${SRC} ${SRC}/Core ${BUFFER})
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui Qt6::Qml)
    if(PP_SANITIZE)
        target_compile_options(${name} PRIVATE -fsanitize=${PP_SANITIZE} -g -fno-omit-frame-pointer)
        target_link_options(${name}    PRIVATE -fsanitize=${PP_SANITIZE})
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# TimelineLabels — 1-D label distribution solver + nearest/active searches + phase names.
pp_add_test(timeline_labels_test  timeline_labels_test.cpp
                                  ${GUI}/review/timeline_labels.cpp)

# SwingSeriesModel — playhead→row mapping (rowForTimeUs) for the table cross-highlight.
# resolveValue/Match are local to swing_series_model.cpp, and SwingSeries' methods are
# header-inline, so no swing_data_source.cpp link is needed.
pp_add_test(swing_series_model_test  swing_series_model_test.cpp
                                    ${GUI}/review/swing_series_model.cpp)

# ShotListModel::shotSummary — focused-shot metadata map for PpShotActionBar.
# Links the model + the swing.json writer it references (persistReview) and a
# stub PpLogStream (pp_debug.cpp drags in whisper/ggml — reuse the Core stub).
pp_add_test(shot_list_model_test  shot_list_model_test.cpp
                                  ${GUI}/shot/shot_list_model.cpp
                                  ${SRC}/Export/swing_doc.cpp
                                  ${SRC}/Export/swing_paths.cpp
                                  ${SRC}/Core/tests/pp_log_stub.cpp)

# ReanalysisController — the single re-analyse seam the carousel forwards to.
pp_add_test(reanalysis_controller_test  reanalysis_controller_test.cpp
                                        ${GUI}/shot/reanalysis_controller.cpp
                                        ${SRC}/Core/tests/pp_log_stub.cpp)
