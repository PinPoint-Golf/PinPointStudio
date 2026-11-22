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
    ${SRC}/Wrist/wrist_profile_assess.cpp   # the ★ reference's Δ (wristDeltasOnly) and the fixtures' profiles (3.8-ii)
    # The Posture section's core (session_posture.{h,cpp}, posture_chart_design.md stage 2a) rides with
    # the wrist profile: it reads each cell through the measure engine's own sampler (measure_sample.cpp).
    ${GUI}/charts/session_posture.cpp
    ${SRC}/Diagnostics/measures/measure_sample.cpp
    ${SRC}/Wrist/wrist_analysis_adapter.cpp
    ${SRC}/Wrist/wrist_assessment_engine.cpp
    ${SRC}/Wrist/assessment_rules.cpp
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
    ${SRC}/Diagnostics/measures/measure_facets.cpp
    ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp
    # A session's day and place (SessionDiagnosticsFiles::identityOf / sessionPlaces, step 2.7b): the session store
    # reads the session file, SessionIdentity a legacy folder's name, SwingPaths the swing numbers and the listing.
    ${SRC}/Export/session_store.cpp ${SRC}/DomainModel/session_identity.cpp ${SRC}/Export/swing_paths.cpp
    ${SRC}/Core/folder_lock.cpp
    # The charts' one threading idiom and the series' pure half (refactor 5.5a): every presenter's lane is a ChartsLane on
    # the one WorkPool (charts_work.h; SeriesSource there too), every snapshot shot is extractShot's, and the ★ label is
    # the series store's (seriesSwingLabelOf).
    ${GUI}/charts/charts_work.cpp ${SRC}/Core/work_pool.h ${SRC}/Core/work_pool.cpp
    ${GUI}/charts/session_series_extract.cpp ${SRC}/Export/series_store.cpp)
set(CHARTS_NORM_ENV
    "PINPOINT_CORE_NORMS=${SRC}/Resources/diagnostics/norms.json;PINPOINT_CORE_FLIGHT_NORMS=${SRC}/Resources/diagnostics/flight_norms.json;PINPOINT_CORE_CONTEXTS=${SRC}/Resources/diagnostics/contexts.json")
