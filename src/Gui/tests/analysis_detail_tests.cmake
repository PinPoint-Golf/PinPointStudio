# The one projection of the analysis block (refactor step 1.3b; audit H-01's missing test). Included
# from CMakeLists.txt here, so ${GUI}, ${SRC} and pp_add_test are in scope.
#
# analysisDetailFor (swing_doc.h) is the ONE map a shot's analysis is shown from. This suite holds the
# three paths that hand it to QML to one another, key by key: the live join's liveAnalysisDetail, the
# carousel row's readSwingJson and the replay's DiskReplaySource::load — on a synthetic swing (the
# shared analysis fixture, on the ring clock), on an old absolute-time document, and on one real
# pinned-pose swing read from the share (SKIPs that case when the share is absent). The replay is
# linked whole, as replay_score_test links it: its Q_OBJECT base (replay_source.h, header-only, listed
# for moc), the swing document's own link line, the markup stream rule, the app log for ppWarn, and Qt
# Multimedia for its players (load() creates them; the test never plays one).
find_package(Qt6 REQUIRED COMPONENTS Multimedia Test)
pp_add_test(analysis_detail_projection_test
    SOURCES analysis_detail_projection_test.cpp
            ${GUI}/shot/disk_replay_source.cpp
            ${GUI}/shot/replay_source.h
            ${GUI}/markup/markup_truth.cpp
            ${SRC}/Export/swing_doc_writer.cpp ${SRC}/Export/swing_store_repository.cpp ${SRC}/Core/folder_lock.cpp
            ${SRC}/Export/swing_doc_reader.cpp
            ${SRC}/Export/swing_doc_analysis.cpp
            ${SRC}/Export/recording_header.cpp ${SRC}/Analysis/effective_context.cpp
            ${SRC}/Export/swing_paths.cpp        # SwingDocReader's session helpers → sanitise
            ${SRC}/Vocabulary/LaunchMonitor/launch_monitor_reading.cpp
            ${SRC}/LaunchMonitor/gcquad_csv_parser.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/shot ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor ${SRC}/Analysis ${SRC}/Vocabulary/Metrics
    LINK    Qt6::Multimedia Qt6::Test
    AUTOMOC)
