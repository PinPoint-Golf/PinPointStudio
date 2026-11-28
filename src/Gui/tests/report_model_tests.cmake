# Session report — model tests (owned by the agent building the model part). Included from CMakeLists.txt here,
# so ${GUI}, ${SRC} and pp_add_test are in scope.
#
# The report's sentences (report/report_words.h), header-only and pure: every template with every case written
# (§9.1 rule 7), the link grades, counts and dates, and the §9.3 bugs made impossible. The model that
# assembles the map from a live diagnostics model is proved on library sessions by
# tools/session-report-mock/dump_report.qml instead (see the test's header). session_spread.h reaches the
# norms and the measure vocabulary, which include swing_analysis.h non-relatively: hence the three paths.
#
# The work-on's consistency badge and the launch monitor summary's (report/report_variability.h): the grade a work-on
# carries over the session's readings — warm-ups, data warnings and unassessed swings out, pooled within club, scaled by
# the norm's half-width; grade 0 under eight; no badge for a yes/no condition of the shipped pack, a threshold reading or
# no norm — and the summary row's copy of the panel's variability map, club delivery only.
#
# The golfer words for gate clauses are model content now (Resources/diagnostics/gate_words.json, read through
# Diagnostics/content/gate_words_pack.cpp), and the test's last block proves that a Diagnostic Model edit reaches
# the next report: it writes a user pack, a user drill and a user gate-word override and reads them back the way
# SessionReportModel::build() does. Hence the pack stack below — the loader, the three pack providers that
# makeCharacteristicPackProvider() assembles, and the drill and gate-word registries — and the norm sources that
# travel with file_pack_provider.cpp on every other link line (core_pack_test's). The shipped content comes in
# through the PINPOINT_CORE_* files, as for the Analysis suite's model tests; the user layers go to Qt's test data
# location, which the test turns on itself.
set(_pp_report_diag ${SRC}/Resources/diagnostics)
pp_add_test(report_model_test
    SOURCES report_model_test.cpp
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
            ${SRC}/Stores/swing_paths.cpp   # report_swing_no.h: an unlisted swing's number off its folder (step 2.7b)
            # The report's variability marks (report/report_variability.h, R2a R4b R10): the grader, its result, and
            # the within-club pooling over the Consistency maths' centring (variability_clubs.cpp needs those four).
            ${GUI}/charts/session_sequence_words.cpp
            ${GUI}/trust/trust_readings.cpp      # the session's data warnings into "What we could not check" (#123)
    INCLUDE ${GUI} ${SRC} ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics ${SRC}/Analysis
    AUTOMOC LINK pp_domain pp_rules pp_vocabulary pp_platform)
set_tests_properties(report_model_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${_pp_report_diag}/core.json;PINPOINT_CORE_DRILLS=${_pp_report_diag}/drills.json;PINPOINT_CORE_GATE_WORDS=${_pp_report_diag}/gate_words.json;PINPOINT_CORE_REFERENCES=${_pp_report_diag}/references.json")

# The report's launch monitor figures from ONE source (report/report_lm.h, audit F-03): the cover's ball tiles, the
# numbers table, the swing grid's carry row, the reference swing's carries and the appendix, all from the launch monitor
# board's own shots (LmSessionModel::boardShots()), over an in-memory session with one shot left out and one re-labelled
# on the board. Links the board exactly as lm_session_model_test does (PP_LM_BOARD_SOURCES, CMakeLists.txt).
pp_add_test(report_lm_test
    SOURCES report_lm_test.cpp ${GUI}/report/report_lm.cpp ${PP_LM_BOARD_SOURCES}
    INCLUDE ${PP_LM_BOARD_INCLUDES} ${GUI}/report ${SRC}/Stores/tests
    LINK pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Qml Qt6::Network Qt6::Test gspro  AUTOMOC
    # Z: a real launch-monitor swing document (the fixture the diagnostics tests read).
    COMPILE_OPTIONS -DPP_LM_SWING_FIXTURE="${SRC}/Diagnostics/tests/data/live_swings/lm_7iron/swing.json")
set_tests_properties(report_lm_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")
