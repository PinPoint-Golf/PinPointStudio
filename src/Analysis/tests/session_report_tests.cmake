# Session report — pure reductions tests (src/Coaching/session_report.h). Included at the END of CMakeLists.txt
# here, so ${SRC}, ${DIAG}, ${DIAGRES} and the other path variables, and pp_add_test, are in scope.

# Every rule the client report uses to choose what to say (strengths and the whole-axis rule, the
# reference swing, which still, range and magnitude words, what came with a habit, the cause groups
# and their caps, worth checking, the swing timeline, the photo rule, why this order), on hand-built
# sessions, then Nick Henn's 20 Oct session rebuilt from the model dump against the mock. Header-only:
# session_report.h reads the pack's structs (characteristic.h, whose measure vocabulary reaches
# swing_analysis.h non-relatively, hence NORM_INC) and session_spread.h's inline corridor types, so
# nothing is linked beyond Qt.
pp_add_test(session_report_test SOURCES session_report_test.cpp INCLUDE ${NORM_INC})
