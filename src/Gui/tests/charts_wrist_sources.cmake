# The wrist checkpoint profile every Charts target carries (src/Gui/charts/session_wrist.{h,cpp},
# build/wrist-tab/CONTRACT.md I3): since refactor 3.8-ii (DEC-123) the cells are the findings' (diagnostics.json
# schema 16) and session_wrist only lays them out, but the ★ reference of another session still takes its Δ
# through the profile's rule (wristDeltasOnly), so EVERY target that compiles session_wrist.cpp
# links the engine, its adapter (wristCheckpoints, buildWristAngleSource) and the norm stack — and needs
# PINPOINT_CORE_NORMS / PINPOINT_CORE_CONTEXTS to find the shipped norm files (CHARTS_NORM_ENV below,
# the Analysis suite's pp_norm_env). Listing a source a target already lists is harmless (CMake keeps one).
# Included from each charts_*_tests.cmake (GUI, SRC set by src/Gui/tests/CMakeLists.txt).
set(CHARTS_WRIST_SRC
    ${RULE_DATA_SOURCES}   # RuleData and its store (refactor step 3.2)
    ${GUI}/charts/session_wrist.cpp
    # The Posture section's core (session_posture.{h,cpp}, posture_chart_design.md stage 2a) rides with
    # the wrist profile: it reads each cell through the measure engine's own sampler (measure_sample.cpp).
    ${GUI}/charts/session_posture.cpp
    ${SRC}/Stores/phase_grid_store.cpp   # the sampler's folder-keyed reads go through the file door (P-3)
    ${SRC}/Stores/rule_data_reads.cpp
    # A session's day and place (SessionDiagnosticsFiles::identityOf / sessionPlaces, step 2.7b): the session store
    # reads the session file, SessionIdentity a legacy folder's name, SwingPaths the swing numbers and the listing.
    ${SRC}/Stores/session_store.cpp ${SRC}/DomainModel/session_identity.cpp ${SRC}/Stores/swing_paths.cpp
    # The charts' one threading idiom and the series' pure half (refactor 5.5a): every presenter's lane is a ChartsLane on
    # the one WorkPool (charts_work.h; SeriesSource there too), every snapshot shot is extractShot's, and the ★ label is
    # the series store's (seriesSwingLabelOf).
    ${GUI}/charts/charts_work.cpp ${SRC}/Core/work_pool.h
    ${GUI}/charts/session_series_extract.cpp ${SRC}/Stores/series_store.cpp)
set(CHARTS_NORM_ENV
    "PINPOINT_CORE_NORMS=${SRC}/Resources/diagnostics/norms.json;PINPOINT_CORE_FLIGHT_NORMS=${SRC}/Resources/diagnostics/flight_norms.json;PINPOINT_CORE_CONTEXTS=${SRC}/Resources/diagnostics/contexts.json")
