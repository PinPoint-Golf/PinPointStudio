# The Charts panel's traces presenter (src/Gui/charts/charts_traces_presenter.h) — This shot, Against
# and Session of direction A, on the trimmed 4 July fixture (data/charts/session_0704.json, synchronous)
# and, when /mnt/swingdata is mounted, the real 4 July scratch session and the 50-shot 7 Oct session
# through SessionSeriesModel (read-only), where the STEP2A budgets are measured and printed.
#
# It links the REAL metric catalogue (ChartMetrics: presets, domains, card specs, sequenceTable) — the
# chart_metrics_test chain — plus the session maths and the session model with its pack chain.
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)
pp_add_test(charts_traces_presenter_test
    SOURCES charts_traces_presenter_test.cpp
            ${GUI}/charts/charts_traces_presenter.cpp
            ${GUI}/charts/session_series_model.cpp
            ${CHARTS_WRIST_SRC}
            ${GUI}/charts/session_curves.cpp
            ${GUI}/charts/session_warp.cpp
            ${GUI}/charts/session_traces.cpp
            ${GUI}/charts/session_relations.cpp
            ${GUI}/charts/session_sequence.cpp
            ${GUI}/charts/session_bridge.cpp
            ${GUI}/trust/trust_readings.cpp
            ${GUI}/review/chart_metrics.cpp
            ${GUI}/review/timeline_labels.cpp
            ${SRC}/Metrics/metric_catalogue.cpp
            ${SRC}/Metrics/metric_resolver.cpp
            ${SRC}/Metrics/metric_catalogue_manifest.cpp
            ${SRC}/Metrics/metric_providers.cpp
            ${SRC}/Wrist/reference_bands.cpp
            ${SRC}/Diagnostics/norms/norm.cpp
            ${SRC}/Diagnostics/norms/norm_pack.cpp
            ${SRC}/Diagnostics/norms/context_tree.cpp
            ${SRC}/Diagnostics/norms/resource_norm_provider.cpp
            ${SRC}/Diagnostics/norms/file_norm_provider.cpp
            ${SRC}/Diagnostics/norms/merged_norm_provider.cpp
            ${SRC}/Diagnostics/engine/pack_io.cpp
            ${SRC}/Diagnostics/engine/characteristic.cpp
            ${SRC}/Diagnostics/packs/characteristic_pack.cpp
            ${SRC}/Diagnostics/packs/reference_pack.cpp
            ${SRC}/Diagnostics/packs/resource_pack_provider.cpp
            ${SRC}/Diagnostics/packs/file_pack_provider.cpp
            ${SRC}/Diagnostics/packs/merged_pack_provider.cpp
            ${SRC}/Diagnostics/measures/measure_facets.cpp
            ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/charts ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/Metrics ${SRC}/Analysis ${SRC}/Diagnostics
    DEFINES CHARTS_MATHS_DATA="${CMAKE_CURRENT_LIST_DIR}/data/charts"
    LINK    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(charts_traces_presenter_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV};PP_CHARTS_MEASURE_OUT=${CMAKE_BINARY_DIR}/charts_traces_measure.txt"
    TIMEOUT 600)
