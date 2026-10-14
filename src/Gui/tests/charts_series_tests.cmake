# SessionSeriesModel (src/Gui/charts/session_series_model.h) — the Charts panel's session loader.
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
pp_add_test(charts_series_model_test
    SOURCES charts_series_model_test.cpp
            ${GUI}/charts/session_series_model.cpp
            ${SRC}/Diagnostics/packs/resource_pack_provider.cpp
            ${SRC}/Diagnostics/packs/file_pack_provider.cpp
            ${SRC}/Diagnostics/packs/merged_pack_provider.cpp
            ${SRC}/Diagnostics/packs/characteristic_pack.cpp
            ${SRC}/Diagnostics/packs/reference_pack.cpp
            ${SRC}/Diagnostics/engine/characteristic.cpp
            ${SRC}/Diagnostics/engine/pack_io.cpp
            ${SRC}/Diagnostics/measures/measure_facets.cpp
            ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp
            ${SRC}/Diagnostics/norms/norm.cpp
            ${SRC}/Diagnostics/norms/norm_pack.cpp
            ${SRC}/Diagnostics/norms/context_tree.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Metrics
    LINK    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(charts_series_model_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;PP_CHARTS_FIXTURE=${SRC}/../build/run-me/chart-variants/qml/fixture/session.json")
