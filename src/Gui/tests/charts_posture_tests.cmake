# The Posture section's core (src/Gui/charts/session_posture.{h,cpp}; posture_chart_design.md stage 2a):
# the three golfers' §2.1 medians against the SHIPPED norms reproduce the recomputed worked match; ⚠ cells
# are drawn and never matched; face-on-only and DTL-only captures match on their own rows; and one real
# swing (a trimmed copy under data/posture/) reads, cell for cell, what measure_dump read off it. The pack
# and the norms come in through PINPOINT_CORE_PACK / PINPOINT_CORE_NORMS / PINPOINT_CORE_CONTEXTS.
# Included from src/Gui/tests/CMakeLists.txt (GUI, SRC set there).
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)
pp_add_test(session_posture_test
    SOURCES session_posture_test.cpp
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
            ${GUI}/tests/folder_series.cpp
            ${CHARTS_WRIST_SRC}
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics
    LINK pp_stores pp_domain pp_rules pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(session_posture_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV};PP_POSTURE_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/data/posture/nick_2026-10-20_03_swing_0002.json")

# The Posture section's presenter (src/Gui/charts/charts_posture_presenter.{h,cpp}; stage 2b): the payload
# the QML places — every documented key, This shot and Session, the pattern override, the Against ghost,
# seekUsFor, a shot from its own detail, an empty model — on the same real Nick swing joined to its own
# ledger row (data/posture/…_diagnostics.json, a trimmed copy of that session's diagnostics.json, shot 2). The picture
# strip (posture_picture.cpp, stage 5) on data/posture/picture_doc.json — that swing re-analysed for stage 4a, trimmed —
# with its closure: the stills' pure half, the markup stream rules, the DTL overlay translator and the head track.
# The session's consistency grades (R2b, R7): the grader, one window per club (variability_clubs.cpp, which needs the
# Consistency maths), and the `variabilityResult` value type the payload carries.
set(POSTURE_VARIABILITY_SRC
    ${GUI}/variability/variability_facade.cpp)
pp_add_test(charts_posture_presenter_test
    SOURCES charts_posture_presenter_test.cpp
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
            ${GUI}/charts/charts_posture_presenter.cpp
            ${POSTURE_VARIABILITY_SRC}
            ${GUI}/charts/posture_picture.cpp
            ${GUI}/report/report_stills_pure.cpp
            ${GUI}/markup/markup_truth.cpp
            ${GUI}/tests/folder_series.cpp
            ${CHARTS_WRIST_SRC}
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics
    LINK pp_stores pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(charts_posture_presenter_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV};PP_POSTURE_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/data/posture/nick_2026-10-20_03_swing_0002.json;PP_POSTURE_DIAGNOSTICS=${CMAKE_CURRENT_SOURCE_DIR}/data/posture/nick_2026-10-20_03_swing_0002_diagnostics.json;PP_POSTURE_PICTURE=${CMAKE_CURRENT_SOURCE_DIR}/data/posture/picture_doc.json")
