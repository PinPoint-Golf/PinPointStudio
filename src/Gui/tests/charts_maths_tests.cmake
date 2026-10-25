# The Charts panel's session maths (src/Gui/charts/session_{curves,warp,traces,relations,sequence}) —
# pure C++ over the SessionSnapshot value types. Included from src/Gui/tests/CMakeLists.txt (GUI, SRC
# set there).
#
# The goldens (data/charts/goldens_0704.json) are the MOCKS' OWN numbers, computed by
# build/charts-build/goldens/make_goldens.js running variant_a/lib/traces.js and variant_b/lib/Stats.js on
# the trimmed 4 July fixture (data/charts/session_0704.json, made by goldens/trim_fixture.py). Regenerate
# both with those two scripts if the fixture or the mocks change.
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
