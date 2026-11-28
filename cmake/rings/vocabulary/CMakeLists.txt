# pp_vocabulary — the Vocabulary ring's compiled files of src/Vocabulary (the metric catalogue and its resolver, the
# set-up capability, the variability grader, the launch monitor's reading), as one static library (#424). Defined once
# for both trees by pp_require_ring(vocabulary) (cmake/PinPointRings.cmake). It links only pp_platform and Qt.
#
# Not here:
#  - Recording/frame_decode.cpp: it needs OpenCV (cv::Mat), which this library would then force on every user; its few
#    users list it.
#  - the header-only areas (Analysis, Club, Common, Rig, most of Recording) compile nothing.
# Identity/identity.h is header-only and moc'd (Q_GADGETs: AthleteId, SessionId, ShotRef, RigRef ...): its meta-objects
# are built here, once, so no user lists it (two copies of a meta-object are a duplicate symbol). Recording/perspective.h
# (Q_NAMESPACE) is not here: the QML module's targets list it so their type registrar sees the namespace it registers
# (cmake/PinPointQmlModule.cmake), and each target that needs its meta-object moc's it, as before.
#
# Qt Gui as well as Core: Analysis/swing_analysis.h holds QQuaternion. src/Buffer is exported because swing_analysis.h
# includes the Buffer's "types.h" by name.

if(NOT TARGET Qt6::Gui)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
endif()
pp_require_ring(platform)

set(_voc ${PP_RINGS_SRC}/Vocabulary)
add_library(pp_vocabulary STATIC
    ${_voc}/Metrics/metric_catalogue.h               ${_voc}/Metrics/metric_catalogue.cpp
    ${_voc}/Metrics/metric_catalogue_manifest.cpp    # the catalogue's manifest table (metric_catalogue.h)
    ${_voc}/Metrics/metric_providers.h               ${_voc}/Metrics/metric_providers.cpp
    ${_voc}/Metrics/metric_resolver.h                ${_voc}/Metrics/metric_resolver.cpp
    ${_voc}/Metrics/setup_capability.h               ${_voc}/Metrics/setup_capability.cpp
    ${_voc}/Metrics/variability/variability_grader.h      ${_voc}/Metrics/variability/variability_grader.cpp
    ${_voc}/Metrics/variability/variability_result.h      ${_voc}/Metrics/variability/variability_result.cpp
    ${_voc}/Metrics/variability/variability_trend.h       ${_voc}/Metrics/variability/variability_trend.cpp
    ${_voc}/Metrics/variability/variability_typicality.h  ${_voc}/Metrics/variability/variability_typicality.cpp
    ${_voc}/LaunchMonitor/launch_monitor_reading.h   ${_voc}/LaunchMonitor/launch_monitor_reading.cpp
    ${_voc}/Identity/identity.h                       # moc'd here (Q_GADGET)
)
set_target_properties(pp_vocabulary PROPERTIES AUTOMOC ON)
target_include_directories(pp_vocabulary PUBLIC ${PP_RINGS_SRC}/Buffer)
target_link_libraries(pp_vocabulary PUBLIC pp_platform Qt6::Core Qt6::Gui)

pp_ring_settings(pp_vocabulary)
