# The stored score through every reader (audit H-01 / H-08, Stage 0.6). Included from CMakeLists.txt
# here, so ${GUI}, ${SRC} and pp_add_test are in scope.
#
# One document per score shape (the /2 bare int, the /3 ScoreBreakdown object), in both on-disk
# formats, read back through the summary, the full reader, the replay (DiskReplaySource::load) and the
# data viewer's Properties rows (SwingDataSource::metadata), each of which must give the stored overall.
# SwingDataSource comes with its own closure (its table and coverage models, the phase labels, the trust
# readings it names a missing lane's gate from, and Qt Qml for QML_ELEMENT). The replay is linked whole: its Q_OBJECT base
# (replay_source.h, header-only, so listed for moc), the swing document's own link line (as
# swing_doc_test's), the markup stream rule and the DTL overlay payload it calls, the app log for
# ppWarn, and Qt Multimedia for its players (load() creates them; the test never plays one).
find_package(Qt6 REQUIRED COMPONENTS Multimedia Qml Test)
pp_add_test(replay_score_test
    SOURCES replay_score_test.cpp
            ${GUI}/review/swing_data_source.cpp
            ${GUI}/review/swing_series_model.cpp
            ${GUI}/review/swing_coverage_model.cpp
            ${GUI}/review/timeline_labels.cpp
            ${GUI}/trust/trust_readings.cpp
            ${GUI}/charts/session_sequence.cpp
            ${GUI}/charts/session_curves.cpp
            ${GUI}/shot/disk_replay_source.cpp
            ${GUI}/shot/replay_source.h
            ${GUI}/shot/dtl_overlay_payload.cpp
            ${GUI}/markup/markup_truth.cpp
            ${SRC}/Export/swing_doc_writer.cpp
            ${SRC}/Export/swing_doc_reader.cpp
            ${SRC}/Export/swing_doc_analysis.cpp
            ${SRC}/Export/swing_paths.cpp        # SwingDocReader's session helpers → sanitise
            ${SRC}/LaunchMonitor/launch_monitor_reading.cpp
            ${SRC}/LaunchMonitor/gcquad_csv_parser.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/shot ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor ${SRC}/Analysis ${SRC}/Metrics
    LINK    Qt6::Multimedia Qt6::Qml Qt6::Test
    AUTOMOC)
