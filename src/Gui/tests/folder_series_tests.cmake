# FolderSeries (src/Gui/tests/folder_series.h) — the Charts panel's old session loader, a test double since refactor
# 5.5a (the app reads each Session's own series: session_series_test), and the pure half it shares (session_series_extract).
# Included from src/Gui/tests/CMakeLists.txt (GUI, SRC set there).
#
# A synthetic session written by SwingStore's own writer (so the format is the real one): field
# extraction, the window-relative clock, impact, phases, validity and σ, a skipped shot, the cache,
# re-analysis, the live append, strike order and the ledger join. Plus the 4 July scratch session
# and, with PP_CHARTS_LARGE_SESSION set, any other — both QSKIP when /mnt/swingdata is not mounted.
#
# The condition catalogue is the SHIPPED one (src/Resources/diagnostics/core.json) through the
# PINPOINT_CORE_PACK seam, the way the diagnostics model's own test reads it; the user's packs are
# kept out by QStandardPaths test mode.
#
# The real-data cases load each session twice — SwingStore::loadAnalysis (the model's default)
# and the whole document — check both give the same shots, and report the cost of each when
# PP_CHARTS_MEASURE_OUT names a file.
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)
pp_add_test(folder_series_test
    SOURCES folder_series_test.cpp
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
            ${GUI}/tests/folder_series.cpp
            ${CHARTS_WRIST_SRC}
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics
    LINK pp_domain pp_rules pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(folder_series_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV};PP_CHARTS_FIXTURE=${SRC}/../build/run-me/chart-variants/qml/fixture/session.json")
