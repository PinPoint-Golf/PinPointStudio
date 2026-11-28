# The Charts panel's relations presenter (src/Gui/charts/charts_relations_presenter.{h,cpp}) — design §2.1
# KeyFigures and "Apart from the session", §2.3 Relations, §2.4 Kinds, §3 Replay Session. Included from
# src/Gui/tests/CMakeLists.txt (GUI, SRC set there).
#
# The read-at instants are the REAL metric catalogue's card readAt (the presenter calls
# makeMetricCatalogue directly), so the catalogue chain chart_metrics_test links comes along; the session
# loader (FolderSeries, the old loader as a test double since 5.5a) and its pack stack come along for the real-data cases, which QSKIP when
# /mnt/swingdata is not mounted. The fixtures are the maths suite's trimmed 4 July session and, for
# "Apart from the session", the 5 Oct one with a launch monitor and its Consistency golden.
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)
pp_add_test(charts_relations_presenter_test
    SOURCES charts_relations_presenter_test.cpp
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
            ${GUI}/charts/charts_relations_presenter.cpp
            # R8 typicality (Apart, Readings, the key figures, Replay's "now"): the session window and the façade's payload
            ${GUI}/variability/variability_facade.cpp
            ${GUI}/charts/consistency_env.cpp
            ${GUI}/charts/session_sequence_words.cpp
            ${GUI}/trust/trust_readings.cpp
            ${GUI}/tests/folder_series.cpp
            ${CHARTS_WRIST_SRC}
            ${GUI}/review/timeline_labels.cpp
            ${SRC}/Stores/rule_data_reads.cpp
    INCLUDE ${GUI} ${GUI}/charts ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics
    DEFINES CHARTS_MATHS_DATA="${CMAKE_CURRENT_LIST_DIR}/data/charts"
    LINK pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(charts_relations_presenter_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")

# The Consistency tab's presenter (src/Gui/charts/charts_consistency_presenter.{h,cpp}, consistency_design.md §3, §4.3 to
# §4.5): the same chain, as it builds its rows with the relations presenter's, and the variability grades it shows
# (consistency_grades.cpp, the grader, the scale provider and the façade's views; governance/artefacts/design_system/variability.md). The fixtures are 5 Oct, with a launch
# monitor and its Consistency golden, and 4 July, with none.
pp_add_test(charts_consistency_presenter_test
    SOURCES charts_consistency_presenter_test.cpp
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
            ${GUI}/charts/charts_consistency_presenter.cpp
            ${GUI}/charts/charts_consistency_types.h   # its typed values: moc (refactor 5.5b)
            ${GUI}/charts/variability_env.cpp
            ${GUI}/variability/variability_facade.cpp
            ${GUI}/charts/charts_relations_presenter.cpp
            ${GUI}/charts/consistency_env.cpp
            ${GUI}/charts/session_sequence_words.cpp
            ${GUI}/trust/trust_readings.cpp
            ${GUI}/tests/folder_series.cpp
            ${CHARTS_WRIST_SRC}
            ${GUI}/review/timeline_labels.cpp
            ${SRC}/Stores/rule_data_reads.cpp
    INCLUDE ${GUI} ${GUI}/charts ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics
    DEFINES CHARTS_MATHS_DATA="${CMAKE_CURRENT_LIST_DIR}/data/charts"
    LINK pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(charts_consistency_presenter_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")
