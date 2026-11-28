# Session report — list tests (owned by the agent building the list part). Included from CMakeLists.txt here,
# so ${GUI}, ${SRC} and pp_add_test are in scope.

# The session list (report/session_report_list_model) over a temp library of fake sessions — its rows the one session
# list's (Gui/session/session_list_presenter, refactor step 5.7a) over a real SessionStore's index: newest first,
# the session-of-day count, canReport (three swings, not live), work-on 1 from a current record only and
# its goal through the resolver, the live row on top, the state words through report_meta.h. Plus the
# mail hand-off's half that needs no mail app (report_mailer.cpp: validation, PINPOINT_MAIL_DRYRUN);
# platformCompose is the test's own, so no platform file is linked. report_mailer.cpp also sends over
# SMTP (report_smtp_test covers that), so the SMTP files come along, with the password store's portable
# half only — its platform half is the test's own too. The Sessions screen's half: sizes, the archived
# marker, the filters and their counts, the short report words, and a real archive / restore through
# SessionArchiver keeping a current report current.
find_package(Qt6 REQUIRED COMPONENTS Network)
pp_add_test(report_list_test
    SOURCES report_list_test.cpp
            ${GUI}/report/session_report_list_model.cpp
            ${GUI}/session/session_list_presenter.cpp ${SRC}/Stores/session_list_reads.cpp   # its rows (refactor step 5.7a)
            ${SRC}/Stores/session_index_summary.cpp   # the entries' summaries (5.7a-P1)
            ${GUI}/review/device_filter_model.cpp
            ${GUI}/report/report_mailer.cpp
            ${GUI}/report/smtp_message.cpp
            ${GUI}/report/smtp_client.cpp
            ${SRC}/Secrets/mail_credentials.cpp ${SRC}/Secrets/secret_store.cpp   # the password, through the credential store
            ${SRC}/Stores/swing_doc_writer.cpp ${SRC}/Stores/swing_store_repository.cpp
            ${SRC}/Stores/swing_doc_reader.cpp
            ${SRC}/Stores/swing_doc_analysis.cpp
            ${SRC}/Stores/recording_header.cpp ${SRC}/Analysis/effective_context.cpp
            ${SRC}/Stores/session_archiver.cpp
            ${SRC}/Stores/session_store.cpp ${SRC}/DomainModel/session_identity.cpp
            ${SRC}/Vocabulary/Identity/identity.h   # the archiver's door (step 2.4)
            ${SRC}/Stores/swing_paths.cpp
            ${SRC}/Vocabulary/LaunchMonitor/launch_monitor_reading.cpp
    INCLUDE ${GUI} ${GUI}/report ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor
            ${SRC}/Analysis   # metric_descriptor.h names the bare "swing_analysis.h" (reached through session_diagnostics_files.h since a6220739)
    LINK pp_platform    Qt6::Network
    AUTOMOC)
set_tests_properties(report_list_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
