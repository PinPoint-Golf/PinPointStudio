# The Charts panel's wrist presenter (src/Gui/charts/charts_wrist_presenter.{h,cpp}; build/wrist-tab
# CONTRACT I4) — the Wrist tab's This shot, Session, Against (previous / median / reference ★), curves
# behind and the playhead's checkpoint, on a three-shot session built in memory (no fixture file). The
# cells are graded by the REAL wrist engine against the shipped norms, the findings are the shipped
# pack's wrist-domain conditions, and "what moves with" is ChartsRelationsPresenter::movesWith, so the
# relations presenter and the metric catalogue chain come along. Included from src/Gui/tests/CMakeLists.txt.
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)
pp_add_test(charts_wrist_presenter_test
    SOURCES charts_wrist_presenter_test.cpp
            ${GUI}/charts/charts_wrist_presenter.cpp
            ${GUI}/charts/charts_relations_presenter.cpp
            ${GUI}/charts/session_series_model.cpp
            ${GUI}/charts/session_curves.cpp
            ${GUI}/charts/session_warp.cpp
            ${GUI}/charts/session_traces.cpp
            ${GUI}/charts/session_relations.cpp
            ${GUI}/charts/session_sequence.cpp
            ${GUI}/trust/trust_readings.cpp
            ${CHARTS_WRIST_SRC}
            ${GUI}/review/timeline_labels.cpp
            ${SRC}/Metrics/metric_catalogue.cpp
            ${SRC}/Metrics/metric_resolver.cpp
            ${SRC}/Metrics/metric_catalogue_manifest.cpp
            ${SRC}/Metrics/metric_providers.cpp
            ${SRC}/Diagnostics/packs/resource_pack_provider.cpp
            ${SRC}/Diagnostics/packs/file_pack_provider.cpp
            ${SRC}/Diagnostics/packs/merged_pack_provider.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/charts ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Metrics
    LINK    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(charts_wrist_presenter_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")
