# Session report — list tests (owned by the agent building the list part). Included from CMakeLists.txt here,
# so ${GUI}, ${SRC} and pp_add_test are in scope.

# The session list (report/session_report_list_model) over a temp library of fake sessions: newest first,
# the session-of-day count, canReport (three swings, not live), work-on 1 from a current record only and
# its goal through the resolver, the live row on top, the state words through report_meta.h. Plus the
# mail hand-off's half that needs no mail app (report_mailer.cpp: validation, PINPOINT_MAIL_DRYRUN);
# platformCompose is the test's own, so no platform file is linked. report_mailer.cpp also sends over
# SMTP (report_smtp_test covers that), so the SMTP files come along, with the password store's portable
# half only — its platform half is the test's own too.
find_package(Qt6 REQUIRED COMPONENTS Network)
pp_add_test(report_list_test
    SOURCES report_list_test.cpp
            ${GUI}/report/session_report_list_model.cpp
            ${GUI}/review/device_filter_model.cpp
            ${GUI}/report/report_mailer.cpp
            ${GUI}/report/smtp_message.cpp
            ${GUI}/report/smtp_client.cpp
            ${GUI}/report/mail_credentials.cpp
            ${SRC}/Export/swing_doc.cpp
            ${SRC}/Export/swing_paths.cpp
            ${SRC}/LaunchMonitor/launch_monitor_reading.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${GUI}/report ${GUI}/review ${SRC} ${SRC}/Core ${SRC}/LaunchMonitor
    LINK    Qt6::Network
    AUTOMOC)
set_tests_properties(report_list_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
