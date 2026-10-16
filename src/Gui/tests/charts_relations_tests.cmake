# The Charts panel's relations presenter (src/Gui/charts/charts_relations_presenter.{h,cpp}) — design §2.1
# KeyFigures and "Apart from the session", §2.3 Relations, §2.4 Kinds, §3 Replay Session. Included from
# src/Gui/tests/CMakeLists.txt (GUI, SRC set there).
#
# The read-at instants are the REAL metric catalogue's card readAt (the presenter calls
# makeMetricCatalogue directly), so the catalogue chain chart_metrics_test links comes along; the session
# loader (SessionSeriesModel) and its pack stack come along for the real-data cases, which QSKIP when
# /mnt/swingdata is not mounted. The fixture is the maths suite's trimmed 4 July session.
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)
pp_add_test(charts_relations_presenter_test
    SOURCES charts_relations_presenter_test.cpp
            ${GUI}/charts/charts_relations_presenter.cpp
            ${GUI}/charts/session_curves.cpp
            ${GUI}/charts/session_warp.cpp
            ${GUI}/charts/session_traces.cpp
            ${GUI}/charts/session_relations.cpp
            ${GUI}/charts/session_sequence.cpp
            ${GUI}/trust/trust_readings.cpp
            ${GUI}/charts/session_series_model.cpp
            ${CHARTS_WRIST_SRC}
            ${GUI}/review/timeline_labels.cpp
            ${SRC}/Metrics/metric_catalogue.cpp
            ${SRC}/Metrics/metric_resolver.cpp
            ${SRC}/Metrics/metric_catalogue_manifest.cpp
            ${SRC}/Metrics/metric_providers.cpp
            ${SRC}/Wrist/reference_bands.cpp
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
            ${SRC}/Diagnostics/norms/resource_norm_provider.cpp
            ${SRC}/Diagnostics/norms/file_norm_provider.cpp
            ${SRC}/Diagnostics/norms/merged_norm_provider.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/charts ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Metrics
    DEFINES CHARTS_MATHS_DATA="${CMAKE_CURRENT_LIST_DIR}/data/charts"
    LINK    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(charts_relations_presenter_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")
