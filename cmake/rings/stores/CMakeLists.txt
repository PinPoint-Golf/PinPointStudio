# pp_stores — the stores and file doors of src/Stores (the Adapters ring's one library), as one static library (#424):
# the session, athlete, Rig, studio-profile and rule-data stores, the swing document's reader and writer, the phase
# grid and series stores, the session archiver. Defined once for both trees by pp_require_ring(stores)
# (cmake/PinPointRings.cmake). It links pp_domain (and through it the inner rings), pinpoint_swingstore (the one door
# to swing.ppsw, which stays its own library, compiled optimised: cmake/swingstore) and Qt.
#
# Not here, and why (each stays in the lists of the targets that compile it today; library_membership_lint_test does
# not count them, since no ring library holds them):
#  - swing_store.cpp, ppsw_qt.cpp: pinpoint_swingstore's;
#  - the files that need OpenCV or the Buffer: recording_loader.cpp (and the MP4 reader it builds,
#    Analysis/mp4_frame_reader.cpp), recording_phase_grid.cpp (it calls the loader), swing_exporter.cpp and
#    export_tone.cpp (cv::Mat). In here they would make every user of the stores link OpenCV for five files;
#  - the encoders and the zip exporter (video_encoder.cpp, ffmpeg_video_encoder.cpp, swing_zip_exporter.cpp): FFmpeg,
#    OpenCV and Qt Concurrent, which the test tree does not build; so this library needs no AUTOMOC;
#  - swing_folder_reanalysis.cpp: the offline tools' only, and it calls the re-analysis pipeline (Analysis).
# Two of its files call ppSettings() (athlete_store.cpp, rule_data_store.cpp): store_isolation_lint_test follows a
# test's link into this library to find them.

if(NOT TARGET pinpoint_swingstore)
    message(FATAL_ERROR "pp_stores links pinpoint_swingstore: define it before pp_require_ring(stores)")
endif()
pp_require_ring(domain)

set(_st ${PP_RINGS_SRC}/Stores)
add_library(pp_stores STATIC
    ${_st}/athlete_store.cpp
    ${_st}/firing_reader_store.cpp
    ${_st}/phase_grid_store.cpp
    ${_st}/recording_header.cpp
    ${_st}/rig_store.cpp
    ${_st}/rule_data_reads.cpp
    ${_st}/rule_data_store.cpp
    ${_st}/series_store.cpp
    ${_st}/session_archiver.cpp
    ${_st}/session_index_summary.cpp
    ${_st}/session_list_reads.cpp
    ${_st}/session_store.cpp
    ${_st}/studio_profile_store.cpp
    ${_st}/swing_doc_analysis.cpp
    ${_st}/swing_doc_reader.cpp
    ${_st}/swing_doc_writer.cpp
    ${_st}/swing_paths.cpp
    ${_st}/swing_store_repository.cpp
)
target_link_libraries(pp_stores PUBLIC pp_domain pinpoint_swingstore)

pp_ring_settings(pp_stores)
