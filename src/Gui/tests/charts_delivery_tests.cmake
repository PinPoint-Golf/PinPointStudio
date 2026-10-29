# The Club delivery section's core (src/Gui/charts/session_delivery.{h,cpp}; club_delivery_chart_design.md stage 2):
# rows cut from the evidence CSV (data/delivery/club_delivery_rows.json) reproduce §2.2's per-golfer fits to 0.01 —
# Mark 5 Oct and Jess 20 Oct shown, Nick 20 Oct refused — §2.4's two plane stories word for word, the no-monitor
# (Mark 7 Oct) and no-DTL (Mark 4 Aug driver) heads, lie → face on a 7-iron and a driver, and the monitor first with
# the camera as its fallback. It reads each shot through the measure engine's sampler (measure_sample.cpp, in
# CHARTS_WRIST_SRC). Included from src/Gui/tests/CMakeLists.txt (GUI, SRC set there).
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)
pp_add_test(session_delivery_test
    SOURCES session_delivery_test.cpp
            ${GUI}/charts/session_delivery.cpp
            ${CHARTS_WRIST_SRC}
            ${SRC}/Diagnostics/packs/resource_pack_provider.cpp
            ${SRC}/Diagnostics/packs/file_pack_provider.cpp
            ${SRC}/Diagnostics/packs/merged_pack_provider.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Metrics
    LINK    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(session_delivery_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV};PP_DELIVERY_ROWS=${CMAKE_CURRENT_SOURCE_DIR}/data/delivery/club_delivery_rows.json")

# The Club delivery section's presenter (src/Gui/charts/charts_delivery_presenter.{h,cpp}; stage 2): the payload the
# QML places — every documented key of choices, This shot and Session, plotLayers on This shot and Session (the scope
# switch), seekUsFor and the standfirsts, a shot from its own detail, an empty model, the report's page — on an
# in-memory session of synthetic shots.
pp_add_test(charts_delivery_presenter_test
    SOURCES charts_delivery_presenter_test.cpp
            ${GUI}/charts/charts_delivery_presenter.cpp
            ${GUI}/charts/session_delivery.cpp
            ${GUI}/charts/session_series_model.cpp
            ${CHARTS_WRIST_SRC}
            ${SRC}/Diagnostics/packs/resource_pack_provider.cpp
            ${SRC}/Diagnostics/packs/file_pack_provider.cpp
            ${SRC}/Diagnostics/packs/merged_pack_provider.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Metrics
    LINK    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(charts_delivery_presenter_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")
