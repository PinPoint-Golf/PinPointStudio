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
            ${GUI}/charts/session_curves.cpp
            ${GUI}/charts/session_warp.cpp
            ${GUI}/charts/session_traces.cpp
            ${GUI}/charts/session_relations.cpp
            ${GUI}/charts/session_sequence.cpp
            ${GUI}/charts/session_bridge.cpp
            ${SRC}/Kinematics/gap_bridge.cpp
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core
    DEFINES CHARTS_MATHS_DATA="${CMAKE_CURRENT_LIST_DIR}/data/charts"
    LINK    Qt6::Test
    AUTOMOC)

# The readings a gate touched (src/Gui/trust/trust_readings.h; trust_and_gates_design.md §4.3, §7.0) — the
# one statement the Trust panel, the in-place block and the Charts marks read: levels, the gate the claim
# shows, lifts less the routes a swing already ran, the shot's head, the session's counts. Fixture inline
# (18 Aug Wrist_02 shot 4's own records).
pp_add_test(trust_readings_test
    SOURCES trust_readings_test.cpp
            ${GUI}/trust/trust_readings.cpp
            ${GUI}/charts/session_sequence.cpp
            ${GUI}/charts/session_curves.cpp
    INCLUDE ${GUI} ${GUI}/charts ${GUI}/trust ${SRC} ${SRC}/Core)

# The Consistency env (src/Gui/charts/consistency_env.{h,cpp}): where each reading is placed and what norm it is
# judged against, on the REAL manifest, conditions and norms, so the catalogue chain chart_metrics_test links
# comes along with the pack stack. consistency_readings.csv is the tally's own output, committed with the design
# doc, and its area column is the reference for every reading it names.
include(${CMAKE_CURRENT_LIST_DIR}/charts_wrist_sources.cmake)      # CHARTS_NORM_ENV
set(CONSISTENCY_ENV_SOURCES
    ${GUI}/charts/consistency_env.cpp
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
    ${SRC}/Core/PpMessageLog.cpp)
set(CONSISTENCY_ENV_INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core ${SRC}/Analysis ${SRC}/Diagnostics ${SRC}/Metrics)
set(CONSISTENCY_ENV_ENV "PINPOINT_CORE_PACK=${SRC}/Resources/diagnostics/core.json;${CHARTS_NORM_ENV}")
pp_add_test(consistency_env_test
    SOURCES consistency_env_test.cpp ${CONSISTENCY_ENV_SOURCES}
    INCLUDE ${CONSISTENCY_ENV_INCLUDE}
    DEFINES CONSISTENCY_READINGS_CSV="${SRC}/../docs/research/data/consistency/consistency_readings.csv"
    LINK    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(consistency_env_test PROPERTIES ENVIRONMENT "${CONSISTENCY_ENV_ENV}")

# The variability scale provider (src/Gui/charts/variability_env.{h,cpp}, docs/design/variability-mark/spec.md §8.2
# step 4): the corridor rung is consistencyEnvFromPacks().normSigma on the real norms, a key with no norm has no
# scale, the scale type is the descriptor's; then the ladder and σnoise on a stub env.
pp_add_test(variability_env_test
    SOURCES variability_env_test.cpp ${GUI}/charts/variability_env.cpp ${CONSISTENCY_ENV_SOURCES}
    INCLUDE ${CONSISTENCY_ENV_INCLUDE}
    LINK    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(variability_env_test PROPERTIES ENVIRONMENT "${CONSISTENCY_ENV_ENV}")

# The Consistency maths (src/Gui/charts/session_consistency.{h,cpp}, consistency_design.md §4): the rules on a stub
# env and synthetic shots, the guard and sampling on the 4 July fixture, and the 5 Oct fixture on the real env for
# its timing and its golden (goldens_1005_consistency.json, cut by consistency_tally.py from a dump of that fixture).
# tools/diagnostics/consistency_make_goldens.sh cuts it and goldens_home_consistency.json again.
set(CONSISTENCY_MATHS_SOURCES
    ${GUI}/charts/session_consistency.cpp
    ${GUI}/charts/session_curves.cpp
    ${GUI}/charts/session_warp.cpp
    ${GUI}/charts/session_traces.cpp)
pp_add_test(session_consistency_test
    SOURCES session_consistency_test.cpp ${CONSISTENCY_MATHS_SOURCES} ${CONSISTENCY_ENV_SOURCES}
    INCLUDE ${CONSISTENCY_ENV_INCLUDE}
    DEFINES CHARTS_MATHS_DATA="${CMAKE_CURRENT_LIST_DIR}/data/charts"
    LINK    Qt6::Qml Qt6::Test
    AUTOMOC)
set_tests_properties(session_consistency_test PROPERTIES ENVIRONMENT "${CONSISTENCY_ENV_ENV}")

# consistency_dump <fixture.json> <out.json>: both layers on the production env, for consistency_tally.py to recompute
# and diff. A tool, not a test: no ctest entry. The shipped files' paths are compiled in as its defaults.
pp_require_swingstore()
add_executable(consistency_dump consistency_dump.cpp ${CONSISTENCY_MATHS_SOURCES} ${CONSISTENCY_ENV_SOURCES})
target_include_directories(consistency_dump PRIVATE ${PP_BUFFER} ${CONSISTENCY_ENV_INCLUDE})
target_compile_definitions(consistency_dump PRIVATE PP_DIAGNOSTICS_DIR="${SRC}/Resources/diagnostics")
target_link_libraries(consistency_dump PRIVATE Qt6::Core Qt6::Gui Qt6::Qml pinpoint_swingstore)
set_target_properties(consistency_dump PROPERTIES AUTOMOC ON)

# variability_calibrate <fixtures-root> <ks-root> <out-dir>: the variability grader (src/Metrics/variability/, spec
# §8) over a read-only extract of every athlete's sessions, for calibrating the §8.10 constants: the calibration and
# trend CSVs and the mark gallery's fixture. A tool, not a test: no ctest entry. Reads <fixtures-root>/<athlete>/*.json
# and the app's stored kinematic-sequence orders <ks-root>/<athlete>/<session>.json, and writes only into <out-dir>.
# The façade's token spellings (src/Gui/variability/variability_facade.cpp) are linked, not copied.
add_executable(variability_calibrate variability_calibrate.cpp ${GUI}/charts/variability_env.cpp
    ${GUI}/variability/variability_facade.cpp
    ${GUI}/charts/session_sequence.cpp
    ${SRC}/Metrics/variability/variability_grader.cpp ${SRC}/Metrics/variability/variability_result.cpp
    ${SRC}/Metrics/variability/variability_typicality.cpp ${SRC}/Metrics/variability/variability_trend.cpp
    ${CONSISTENCY_MATHS_SOURCES} ${CONSISTENCY_ENV_SOURCES})
target_include_directories(variability_calibrate PRIVATE ${PP_BUFFER} ${CONSISTENCY_ENV_INCLUDE})
target_compile_definitions(variability_calibrate PRIVATE PP_DIAGNOSTICS_DIR="${SRC}/Resources/diagnostics")
target_link_libraries(variability_calibrate PRIVATE Qt6::Core Qt6::Gui Qt6::Qml pinpoint_swingstore)
set_target_properties(variability_calibrate PROPERTIES AUTOMOC ON)
