# The wrist checkpoint profile every Charts target now carries (src/Gui/charts/session_wrist.{h,cpp},
# build/wrist-tab/CONTRACT.md I3): SessionSeriesModel::extractShot grades each shot's wrist DOFs through
# the wrist engine against the diagnostics norms, so EVERY target that compiles session_series_model.cpp
# links the engine, its adapter (wristCheckpoints, buildWristAngleSource) and the norm stack — and needs
# PINPOINT_CORE_NORMS / PINPOINT_CORE_CONTEXTS to find the shipped norm files (CHARTS_NORM_ENV below,
# the Analysis suite's pp_norm_env). Listing a source a target already lists is harmless (CMake keeps one).
# Included from each charts_*_tests.cmake (GUI, SRC set by src/Gui/tests/CMakeLists.txt).
set(CHARTS_WRIST_SRC
    ${GUI}/charts/session_wrist.cpp
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
    ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp)
set(CHARTS_NORM_ENV
    "PINPOINT_CORE_NORMS=${SRC}/Resources/diagnostics/norms.json;PINPOINT_CORE_CONTEXTS=${SRC}/Resources/diagnostics/contexts.json")
