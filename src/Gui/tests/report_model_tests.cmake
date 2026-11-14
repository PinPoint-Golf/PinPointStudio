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
# Diagnostics/packs/gate_words_pack.cpp), and the test's last block proves that a Diagnostic Model edit reaches
# the next report: it writes a user pack, a user drill and a user gate-word override and reads them back the way
# SessionReportModel::build() does. Hence the pack stack below — the loader, the three pack providers that
# makeCharacteristicPackProvider() assembles, and the drill and gate-word registries — and the norm sources that
# travel with file_pack_provider.cpp on every other link line (core_pack_test's). The shipped content comes in
# through the PINPOINT_CORE_* files, as for the Analysis suite's model tests; the user layers go to Qt's test data
# location, which the test turns on itself.
set(_pp_report_diag ${SRC}/Resources/diagnostics)
pp_add_test(report_model_test
    SOURCES report_model_test.cpp
            ${SRC}/Diagnostics/engine/pack_io.cpp
            ${SRC}/Diagnostics/engine/characteristic.cpp
            ${SRC}/Diagnostics/packs/characteristic_pack.cpp
            ${SRC}/Diagnostics/packs/reference_pack.cpp
            ${SRC}/Diagnostics/measures/measure_facets.cpp
            ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp
            ${SRC}/Diagnostics/norms/norm.cpp
            ${SRC}/Diagnostics/norms/norm_pack.cpp
            ${SRC}/Diagnostics/norms/context_tree.cpp
            ${SRC}/Diagnostics/packs/resource_pack_provider.cpp
            ${SRC}/Diagnostics/packs/file_pack_provider.cpp
            ${SRC}/Core/pp_log_stream.cpp ${SRC}/Core/PpMessageLog.cpp
            ${SRC}/Diagnostics/packs/merged_pack_provider.cpp
            ${SRC}/Diagnostics/packs/drill_pack.cpp
            ${SRC}/Diagnostics/packs/gate_words_pack.cpp
            # The report's variability marks (report/report_variability.h, R2a R4b R10): the grader, its result, and
            # the within-club pooling over the Consistency maths' centring (variability_clubs.cpp needs those four).
            ${SRC}/Metrics/variability/variability_grader.cpp
            ${SRC}/Metrics/variability/variability_result.cpp
            ${GUI}/charts/variability_clubs.cpp
            ${GUI}/charts/session_consistency.cpp
            ${GUI}/charts/session_curves.cpp
            ${GUI}/charts/session_warp.cpp
            ${GUI}/charts/session_traces.cpp
    INCLUDE ${GUI} ${SRC} ${SRC}/Diagnostics ${SRC}/Metrics ${SRC}/Analysis)
set_tests_properties(report_model_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${_pp_report_diag}/core.json;PINPOINT_CORE_DRILLS=${_pp_report_diag}/drills.json;PINPOINT_CORE_GATE_WORDS=${_pp_report_diag}/gate_words.json;PINPOINT_CORE_REFERENCES=${_pp_report_diag}/references.json")

# The report's launch monitor figures from ONE source (report/report_lm.h, audit F-03): the cover's ball tiles, the
# numbers table, the swing grid's carry row, the reference swing's carries and the appendix, all from the launch monitor
# board's own shots (LmSessionModel::boardShots()), over an in-memory session with one shot left out and one re-labelled
# on the board. Links the board exactly as lm_session_model_test does (PP_LM_BOARD_SOURCES, CMakeLists.txt).
pp_add_test(report_lm_test
    SOURCES report_lm_test.cpp ${GUI}/report/report_lm.cpp ${PP_LM_BOARD_SOURCES}
    INCLUDE ${PP_LM_BOARD_INCLUDES} ${GUI}/report
    LINK    Qt6::Qml Qt6::Network Qt6::Test gspro  AUTOMOC)
set_tests_properties(report_lm_test PROPERTIES
    ENVIRONMENT "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")
