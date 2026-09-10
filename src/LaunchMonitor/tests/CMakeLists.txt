# CTest suite for the launch monitor connector (src/LaunchMonitor). Self-contained
# — NOT part of the app build, and needs no launch monitor, no FSX2020 and no
# Windows. Two kinds of test:
#   • parser  — LastShot.CSV → LaunchMonitorReading: column matching by name,
#               unit conversion (FSX2020 can be switched to imperial), the three
#               derived values, and every way the file can be malformed or torn.
#   • pairing — which swing a reading is attributed to: arming, displacement by
#               the next shot, parking until a swingDir exists, and the watermark
#               that stops a stale file being claimed at startup.
#   • gspro   — the libgspro build dependency (that it links and its headers match
#               its archive), the gsp_message → reading mapping against real
#               clients' byte patterns, and the listener itself over loopback.
# Shared infra (Qt prefix, sanitizers via -DPP_SANITIZE, gtest, pp_add_test) lives
# in tests/cmake/PinPointTests.cmake.
#
#   cmake -S src/LaunchMonitor/tests -B build/lm-tests
#   cmake --build build/lm-tests -j && ctest --test-dir build/lm-tests --output-on-failure

if(NOT COMMAND pp_add_test)
    cmake_minimum_required(VERSION 3.16)
    project(pinpoint_launchmonitor_tests LANGUAGES CXX)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../../tests/cmake)
    include(PinPointTests)
    enable_testing()
endif()

set(LM  ${CMAKE_CURRENT_SOURCE_DIR}/..)   # src/LaunchMonitor
set(SRC ${LM}/..)                          # src

# PinPointTests.cmake finds only Core + Gui; suites add what else they need.
# QSignalSpy (Qt6::Test) is how the pairing test observes readingAvailable.
# Qt6::Network is the GSPro listener's QTcpServer; the connector suite drives a
# real client socket at it on loopback.
find_package(Qt6 REQUIRED COMPONENTS Test Network)

# --- LastShot.CSV parsing: columns, units, derivations, malformed input ----------
# Pure functions only — no QObject, so no AUTOMOC.
pp_add_test(gcquad_csv_parser_test
    SOURCES gcquad_csv_parser_test.cpp
            ${LM}/gcquad_csv_parser.cpp
            ${LM}/launch_monitor_reading.cpp
    INCLUDE ${LM}
    GTEST)

# --- Whether a reading nothing else saw becomes a shot of its own ----------------
# Pure decision logic, split out of the controller precisely so it can be tested without
# standing up a CameraManager. Exhaustive over all 2^7 states.
pp_add_test(standalone_gate_test
    SOURCES standalone_gate_test.cpp ${LM}/standalone_gate.cpp
    INCLUDE ${LM}
    GTEST)

# --- Attribution: which swing a reading belongs to -------------------------------
# ShotPairing is header-only and pure. GcQuadMonitor is a QObject and does real file
# I/O against a QTemporaryDir (the repo convention — tests write their own fixtures),
# so this target needs AUTOMOC and Qt6::Core's event loop for its QTimer.
# The real log (pp_log_stream.cpp + PpMessageLog.cpp), NOT pp_debug.cpp — that one
# still drags in whisper/ggml for its installer, but the log primitive no longer
# comes with it, so this suite carries the log rather than a stub that discards it.
pp_add_test(gcquad_pairing_test
    SOURCES gcquad_pairing_test.cpp
            ${LM}/gcquad_monitor.cpp
            ${LM}/launch_monitor_base.cpp
            ${LM}/gcquad_csv_parser.cpp
            ${LM}/launch_monitor_reading.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
            ${LM}/gcquad_monitor.h
            ${LM}/launch_monitor_base.h
    INCLUDE ${LM} ${SRC}/Core
    LINK    Qt6::Test
    GTEST_NO_MAIN AUTOMOC)

# --- libgspro links, and its headers match its archive ---------------------------
# The GSPro Open Connect connector is the second Kind beside GcQuad, and until it
# lands no PinPoint code references a libgspro symbol — so a dependency that
# configures and compiles but fails to LINK would go unnoticed on every platform.
# This target is what makes "the dependency is added" a claim rather than an
# expectation, exactly as hackmotion_link_test does for libwrist.
#
# pp_require_gspro() resolves the library the way the app does: -DPP_LIBGSPRO_DIR,
# a sibling ../libgspro checkout, an app build's _deps copy, else fetch main. So
# this suite configures on a machine that has never built the app.
#
# NO_QT: the library is C11 with no Qt anywhere near it, and it opens no socket —
# a whole session runs on a synthetic counter and byte strings.
pp_require_gspro()
pp_add_test(gspro_link_test
    SOURCES gspro_link_test.cpp
    LINK    gspro
    NO_QT
    GTEST)

# --- gsp_message → LaunchMonitorReading ------------------------------------------
# The pure half of the GSPro connector, the same split as the CSV parser above: no
# socket, no settings, no clock, so every client quirk is covered against a JSON
# string literal. Needs Qt for QString (the reading is full of them) but no event
# loop and no QObject, so no AUTOMOC.
pp_add_test(gspro_reading_test
    SOURCES gspro_reading_test.cpp
            ${LM}/gspro_reading.cpp
            ${LM}/launch_monitor_reading.cpp
    INCLUDE ${LM}
    LINK    gspro
    GTEST)

# --- The listener, over a real loopback socket -----------------------------------
# GsProMonitor is a QObject that owns a QTcpServer, so this needs AUTOMOC and an
# event loop; the test drives a real client socket at it, which is the only way to
# see what a launch monitor would see. The real log (pp_log_stream + PpMessageLog),
# never pp_debug.cpp — that one drags in whisper/ggml for its installer.
pp_add_test(gspro_monitor_test
    SOURCES gspro_monitor_test.cpp
            ${LM}/gspro_monitor.cpp
            ${LM}/gspro_reading.cpp
            ${LM}/launch_monitor_base.cpp
            ${LM}/launch_monitor_reading.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
            ${LM}/gspro_monitor.h
            ${LM}/launch_monitor_base.h
    INCLUDE ${LM} ${SRC}/Core
    LINK    gspro Qt6::Network Qt6::Test
    GTEST_NO_MAIN AUTOMOC)
