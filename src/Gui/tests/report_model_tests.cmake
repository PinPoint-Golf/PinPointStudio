# Session report — model tests (owned by the agent building the model part). Included from CMakeLists.txt here,
# so ${GUI}, ${SRC} and pp_add_test are in scope.
#
# The report's sentences (report/report_words.h) and the golfer words for gate clauses
# (Coaching/report_gate_words.h), both header-only and pure: every template with every case written
# (§9.1 rule 7), the link grades, counts and dates, and the §9.3 bugs made impossible. The model that
# assembles the map from a live diagnostics model is proved on library sessions by
# tools/session-report-mock/dump_report.qml instead (see the test's header). session_spread.h reaches the
# norms and the measure vocabulary, which include swing_analysis.h non-relatively: hence the three paths.
pp_add_test(report_model_test
    SOURCES report_model_test.cpp
    INCLUDE ${GUI} ${SRC} ${SRC}/Diagnostics ${SRC}/Metrics ${SRC}/Analysis)
