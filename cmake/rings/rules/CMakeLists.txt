# pp_rules — the Rules ring's compiled files that need only Qt and Eigen, as one static library (#424): the diagnostics
# engine, its model, packs, norms and session series (src/Diagnostics), the wrist assessment (Wrist), the kinematic
# sequence (Kinematics), the body metrics (Body), the phase segmenter (Timeline), the launch monitor's inferred reads,
# the IMU's orientation filter and role rule, and Analysis' Rules files named by the ring tables (L8). Defined once
# for both trees by pp_require_ring(rules) (cmake/PinPointRings.cmake). It links only pp_vocabulary (and through it
# pp_platform) and Qt.
#
# Not here, and why (each stays in the lists of the targets that compile it today):
#  - the OpenCV files: Club/*, Ball/*, Pose/* (pp_rules_vision's ground, its own issue), Timeline/event_refine.cpp
#    (Club/ball_anchor.h holds cv::Mat), Analysis/frame_store.cpp;
#  - Analysis/recording_integrity.cpp and IMU/imu_vision_fuser.cpp: they call the Buffer's SwingWindow, and this
#    library links no Buffer (tests compile Buffer files themselves, and imu_test_stubs.cpp stands in for one);
#  - Skeleton3d/skeleton3d_fit.cpp: compiled optimised and assert-free by a source property of the root
#    CMakeLists.txt, and Qt-free with its own flags in skeleton3d_test, so each target keeps its own compile.
# Resources: resource_norm_provider.cpp reads ":/diagnostics", which each executable embeds itself: a static library
# holds no .qrc (an unreferenced resource object would be dropped by MSVC's linker).
#
# Include path: Eigen and the vendored ESKF headers (third_party/imu_ekf_compat first: its <Eigen.h> shim), for this
# library's own files only. PP_RINGS_EIGEN is the Eigen source folder, set by the tree before pp_require_ring(rules).

if(NOT PP_RINGS_EIGEN)
    message(FATAL_ERROR "pp_rules needs PP_RINGS_EIGEN (the Eigen source folder) set before pp_require_ring(rules)")
endif()
pp_require_ring(vocabulary)

set(_s ${PP_RINGS_SRC})
add_library(pp_rules STATIC
    ${_s}/Analysis/analysis_job_builder.cpp
    ${_s}/Analysis/effective_context.cpp
    ${_s}/Analysis/reuse_gate.cpp
    ${_s}/Body/body_rotation.cpp
    ${_s}/Body/foot_metrics.cpp
    ${_s}/Body/head_track.cpp
    ${_s}/Body/lower_body_metrics.cpp
    ${_s}/Body/upper_body_metrics.cpp
    ${_s}/Diagnostics/content/characteristic_pack.cpp
    ${_s}/Diagnostics/content/drill_pack.cpp
    ${_s}/Diagnostics/content/file_norm_provider.cpp
    ${_s}/Diagnostics/content/file_pack_provider.cpp
    ${_s}/Diagnostics/content/gate_words_pack.cpp
    ${_s}/Diagnostics/content/memory_norm_provider.cpp
    ${_s}/Diagnostics/content/memory_pack_provider.cpp
    ${_s}/Diagnostics/content/merged_norm_provider.cpp
    ${_s}/Diagnostics/content/merged_pack_provider.cpp
    ${_s}/Diagnostics/content/norm_pack.cpp
    ${_s}/Diagnostics/content/reference_pack.cpp
    ${_s}/Diagnostics/content/resource_norm_provider.cpp
    ${_s}/Diagnostics/content/resource_pack_provider.cpp
    ${_s}/Diagnostics/content/screen_pack.cpp
    ${_s}/Diagnostics/derived/series/consistency_grades.cpp
    ${_s}/Diagnostics/derived/series/session_bridge.cpp
    ${_s}/Diagnostics/derived/series/session_consistency.cpp
    ${_s}/Diagnostics/derived/series/session_curves.cpp
    ${_s}/Diagnostics/derived/series/session_relations.cpp
    ${_s}/Diagnostics/derived/series/session_sequence.cpp
    ${_s}/Diagnostics/derived/series/session_traces.cpp
    ${_s}/Diagnostics/derived/series/session_warp.cpp
    ${_s}/Diagnostics/derived/series/variability_clubs.cpp
    ${_s}/Diagnostics/engine/characteristic_engine.cpp
    ${_s}/Diagnostics/engine/diagnostics_health.cpp
    ${_s}/Diagnostics/engine/fire_findings.cpp
    ${_s}/Diagnostics/engine/live_measure_source.cpp
    ${_s}/Diagnostics/engine/relation_resolver.cpp
    ${_s}/Diagnostics/engine/session_reduction.cpp
    ${_s}/Diagnostics/model/anatomy_vocabulary.cpp
    ${_s}/Diagnostics/model/characteristic.cpp
    ${_s}/Diagnostics/model/context_tree.cpp
    ${_s}/Diagnostics/model/measure_facets.cpp
    ${_s}/Diagnostics/model/measure_sample.cpp
    ${_s}/Diagnostics/model/norm.cpp
    ${_s}/Diagnostics/model/pack_io.cpp
    ${_s}/IMU/eskf_orientation_filter.cpp
    ${_s}/IMU/imu_role_map.cpp
    ${_s}/Kinematics/gap_bridge.cpp
    ${_s}/Kinematics/kinematic_series.cpp
    ${_s}/Kinematics/segment_rates.cpp
    ${_s}/LaunchMonitor/inferred/standalone_gate.cpp
    ${_s}/Timeline/phase_segmenter.cpp
    ${_s}/Timeline/phase_signals.cpp
    ${_s}/Timeline/tempo_metrics.cpp
    ${_s}/Wrist/assessment_rules.cpp
    ${_s}/Wrist/metric_extractor.cpp
    ${_s}/Wrist/pose_wrist_angle_source.cpp
    ${_s}/Wrist/reference_bands.cpp
    ${_s}/Wrist/score_uncertainty.cpp
    ${_s}/Wrist/wrist_analysis_adapter.cpp
    ${_s}/Wrist/wrist_assessment_engine.cpp
    ${_s}/Wrist/wrist_profile_assess.cpp
    ${_s}/Wrist/wrist_resemblance.cpp
)
target_include_directories(pp_rules PRIVATE
    ${PP_RINGS_EIGEN}
    ${PP_RINGS_SRC}/../third_party/imu_ekf_compat   # <Eigen.h>/<Wire.h> shims: MUST precede imu_ekf
    ${PP_RINGS_SRC}/../third_party/imu_ekf)         # ESKF.h, Quaternion.h
target_link_libraries(pp_rules PUBLIC pp_vocabulary)

pp_ring_settings(pp_rules)
