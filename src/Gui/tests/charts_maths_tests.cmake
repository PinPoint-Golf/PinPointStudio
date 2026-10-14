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
    INCLUDE ${GUI} ${GUI}/charts ${SRC} ${SRC}/Core
    DEFINES CHARTS_MATHS_DATA="${CMAKE_CURRENT_LIST_DIR}/data/charts"
    LINK    Qt6::Test
    AUTOMOC)
