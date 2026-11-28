# The Charts panel's wrist presenter (src/Gui/charts/charts_wrist_presenter.{h,cpp}; build/wrist-tab
# CONTRACT I4) — the Wrist tab's This shot, Session, Against (previous / median / reference ★), curves
# behind and the playhead's checkpoint, on a three-shot session built in memory (no fixture file). The
# cells are graded by the REAL wrist engine against the shipped norms, the findings are the shipped
# pack's wrist-domain conditions, and "what moves with" is ChartsRelationsPresenter::movesWith, so the
# relations presenter and the metric catalogue chain come along. Included from src/Gui/tests/CMakeLists.txt.
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)
pp_add_test(charts_wrist_presenter_test
    SOURCES charts_wrist_presenter_test.cpp
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
            ${GUI}/charts/charts_wrist_presenter.cpp
            ${GUI}/charts/charts_relations_presenter.cpp
            ${GUI}/charts/consistency_env.cpp
            ${GUI}/tests/folder_series.cpp
            ${GUI}/charts/session_sequence_words.cpp
            # the session's consistency grades (R2b, R7): one window per club, the grader, the `variabilityResult` type
            ${GUI}/variability/variability_facade.cpp
            ${GUI}/trust/trust_readings.cpp
            ${CHARTS_WRIST_SRC}
            ${GUI}/review/timeline_labels.cpp
    INCLUDE ${GUI} ${GUI}/charts ${GUI}/review ${GUI}/variability ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics
    LINK pp_stores pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(charts_wrist_presenter_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")
