# The Charts panel's session maths (src/Gui/charts/session_{curves,warp,traces,relations,sequence}) —
# pure C++ over the SessionSnapshot value types. Included from src/Gui/tests/CMakeLists.txt (GUI, SRC
# set there).
#
# The goldens (data/charts/goldens_0704.json) are the MOCKS' OWN numbers, computed by
# build/charts-build/goldens/make_goldens.js running variant_a/lib/traces.js and variant_b/lib/Stats.js on
# the trimmed 4 July fixture (data/charts/session_0704.json, made by goldens/trim_fixture.py). Regenerate
# both with those two scripts if the fixture or the mocks change. The one thing taken out by hand is the
# mock's `apart` list: Apart from the session is now the Consistency stand-out rule (consistency_design.md
# §5), which charts_relations_presenter_test asserts; this test checks that apartFrom still means past a
# whisker, for the boxes. Drop `apart` again if make_goldens.js is ever re-run.
#
# No ChartMetrics and no metric catalogue: the windowed mean is series_reduce.h's (header-only), and the
# card read-at instants come from the fixture's readAt table.
pp_add_test(charts_session_maths_test
    SOURCES charts_session_maths_test.cpp
            ${GUI}/charts/session_sequence_words.cpp
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core
    DEFINES CHARTS_MATHS_DATA="${CMAKE_CURRENT_LIST_DIR}/data/charts"
    LINK pp_rules    Qt6::Test
    AUTOMOC)

# The readings a gate touched (src/Gui/trust/trust_readings.h; trust_and_gates_design.md §4.3, §7.0) — the
# one statement the Trust panel, the in-place block and the Charts marks read: levels, the gate the claim
# shows, lifts less the routes a swing already ran, the shot's head, the session's counts. Fixture inline
# (18 Aug Wrist_02 shot 4's own records).
pp_add_test(trust_readings_test
    SOURCES trust_readings_test.cpp
            ${GUI}/trust/trust_readings.cpp
            ${GUI}/charts/session_sequence_words.cpp
    INCLUDE ${GUI} ${GUI}/charts ${GUI}/trust ${SRC} ${SRC}/Core LINK pp_rules)

# The Consistency env (src/Gui/charts/consistency_env.{h,cpp}): where each reading is placed and what norm it is
# judged against, on the REAL manifest, conditions and norms, so the catalogue chain chart_metrics_test links
# comes along with the pack stack. consistency_readings.csv is the tally's own output, committed with the design
# doc, and its area column is the reference for every reading it names.
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)      # CHARTS_NORM_ENV
set(CONSISTENCY_ENV_SOURCES
    ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
    ${GUI}/charts/consistency_env.cpp)
set(CONSISTENCY_ENV_INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics)
set(CONSISTENCY_ENV_ENV "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")
pp_add_test(consistency_env_test
    SOURCES consistency_env_test.cpp ${CONSISTENCY_ENV_SOURCES}
            ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
    INCLUDE ${CONSISTENCY_ENV_INCLUDE}
    DEFINES CONSISTENCY_READINGS_CSV="${SRC}/../docs/research/data/consistency/consistency_readings.csv"
    LINK pp_stores pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(consistency_env_test PROPERTIES ENVIRONMENT "${CONSISTENCY_ENV_ENV}")

# The variability scale provider (src/Gui/charts/variability_env.{h,cpp}, docs/design/variability-mark/spec.md §8.2
# step 4): the corridor rung is consistencyEnvFromPacks().normSigma on the real norms, a key with no norm has no
# scale, the scale type is the descriptor's; then the ladder and σnoise on a stub env.
pp_add_test(variability_env_test
    SOURCES variability_env_test.cpp ${GUI}/charts/variability_env.cpp ${CONSISTENCY_ENV_SOURCES}
    INCLUDE ${CONSISTENCY_ENV_INCLUDE}
    LINK pp_stores pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(variability_env_test PROPERTIES ENVIRONMENT "${CONSISTENCY_ENV_ENV}")

# The Consistency maths (src/Diagnostics/derived/series/session_consistency.{h,cpp}, consistency_design.md §4): the rules on a stub
# env and synthetic shots, the guard and sampling on the 4 July fixture, and the 5 Oct fixture on the real env for
# its timing and its golden stand-outs (goldens_1005_consistency.json: frozen since the matrix verdicts and the tally that
# cut it were retired, Mark 7 Nov; its `standouts`, `flags` and shuffle figures are still the rules' own).
set(CONSISTENCY_MATHS_SOURCES)
pp_add_test(session_consistency_test
    SOURCES session_consistency_test.cpp ${CONSISTENCY_MATHS_SOURCES} ${CONSISTENCY_ENV_SOURCES}
    INCLUDE ${CONSISTENCY_ENV_INCLUDE}
    DEFINES CHARTS_MATHS_DATA="${CMAKE_CURRENT_LIST_DIR}/data/charts"
    LINK pp_stores pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(session_consistency_test PROPERTIES ENVIRONMENT "${CONSISTENCY_ENV_ENV}")

# Clubs and the variability grade (src/Diagnostics/derived/series/variability_clubs.{h,cpp}; Mark's rulings R2a and R2b, 7 Nov): the
# pooled within-club input (residuals about each club's median, consistencyResiduals' own centring, clubs under 3
# swings left out) and the per-club windows (ordered by readings, a club under N_MIN still graded 0), scalar and
# trace. Synthetic swings; no env is read.
pp_add_test(variability_clubs_test
    SOURCES variability_clubs_test.cpp ${CONSISTENCY_MATHS_SOURCES}
            ${CONSISTENCY_ENV_SOURCES}
    INCLUDE ${CONSISTENCY_ENV_INCLUDE}
    LINK pp_stores pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Qml Qt6::Test
    AUTOMOC)


# variability_calibrate <fixtures-root> <ks-root> <out-dir>: the variability grader (src/Vocabulary/Metrics/variability/, spec
# §8) over a read-only extract of every athlete's sessions, for calibrating the §8.10 constants: the calibration and
# trend CSVs and the mark gallery's fixture. A tool, not a test: no ctest entry. Reads <fixtures-root>/<athlete>/*.json
# and the app's stored kinematic-sequence orders <ks-root>/<athlete>/<session>.json, and writes only into <out-dir>.
# The façade's token spellings (src/Gui/variability/variability_facade.cpp) are linked, not copied.
add_executable(variability_calibrate variability_calibrate.cpp ${GUI}/charts/variability_env.cpp
    ${GUI}/variability/variability_facade.cpp
    ${GUI}/charts/session_sequence_words.cpp
    ${CONSISTENCY_MATHS_SOURCES} ${CONSISTENCY_ENV_SOURCES})
target_include_directories(variability_calibrate PRIVATE ${PP_BUFFER} ${CONSISTENCY_ENV_INCLUDE})
target_compile_definitions(variability_calibrate PRIVATE PP_DIAGNOSTICS_DIR="${SRC}/Resources/diagnostics")
target_link_libraries(variability_calibrate PRIVATE pp_stores pp_domain pp_rules pp_vocabulary pp_platform Qt6::Core Qt6::Gui Qt6::Qml pinpoint_swingstore)
set_target_properties(variability_calibrate PROPERTIES AUTOMOC ON)
