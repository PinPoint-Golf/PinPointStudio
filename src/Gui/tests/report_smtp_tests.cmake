# Session report — sending over SMTP (design §8.3, option B). Included from CMakeLists.txt here, so ${GUI},
# ${SRC} and pp_add_test are in scope.

# The message (report/smtp_message), the client (report/smtp_client) against a fake SMTP server in the
# test's own process on 127.0.0.1, and the mailer's sending half (report/report_mailer.cpp). The password
# through the credential store's portable half only (Secrets/mail_credentials.cpp, Secrets/secret_store.cpp,
# PINPOINT_MAIL_CREDENTIALS=memory): its platform half is the test's own and counts any call, so the keychain
# is never reached. Settings go to a scratch
# INI (QSettings::setPath). The fake server's TLS is data/smtp/ (test-only CA + certificate, see README).
#
# The fake server needs TLS server mode, which OpenSSL's TLS backend does in memory; the test picks that
# backend when it loads. Qt loads libssl at run time by leaf name, so on macOS the OpenSSL the app links
# (Homebrew's) is put on the dyld fallback path for this test.
find_package(Qt6 REQUIRED COMPONENTS Network)
pp_add_test(report_smtp_test
    SOURCES report_smtp_test.cpp
            ${GUI}/report/smtp_message.cpp
            ${GUI}/report/smtp_client.cpp
            ${SRC}/Secrets/mail_credentials.cpp ${SRC}/Secrets/secret_store.cpp
            ${GUI}/report/report_mailer.cpp
    INCLUDE ${GUI} ${GUI}/report ${SRC} ${SRC}/Core
    LINK pp_platform    Qt6::Network
    COMPILE_OPTIONS -DPP_SMTP_TEST_DATA="${CMAKE_CURRENT_LIST_DIR}/data/smtp"
    AUTOMOC)
set(_pp_smtp_env "QT_QPA_PLATFORM=offscreen" "PINPOINT_MAIL_CREDENTIALS=memory")
if(APPLE)
    find_package(OpenSSL QUIET)
    if(OPENSSL_SSL_LIBRARY)
        get_filename_component(_pp_ssl_dir "${OPENSSL_SSL_LIBRARY}" DIRECTORY)
        list(APPEND _pp_smtp_env "DYLD_FALLBACK_LIBRARY_PATH=${_pp_ssl_dir}")
    endif()
endif()
set_tests_properties(report_smtp_test PROPERTIES ENVIRONMENT "${_pp_smtp_env}" TIMEOUT 120)
# Windows: Qt finds libssl-3-x64.dll on PATH or not at all, and falls back to Schannel, which cannot be
# the fake server (every TLS case then fails with "remote host closed the connection"; first seen on the
# v0.1-beta6 gate). pp_openssl_test_env (cmake/PinPointOpenSSL.cmake) puts vcpkg's bin on the test's PATH.
if(WIN32)
    pp_find_openssl()
    if(PP_OPENSSL_BIN_DIR)
        pp_openssl_test_env(report_smtp_test)
    else()
        message(WARNING "report_smtp_test: OpenSSL's DLLs not found, so its TLS cases will fail on Schannel.")
    endif()
endif()

# The credential store (src/Secrets/secret_store.h) and its first client, the report-sending password: the
# old password's names on all three platforms, a password saved under them read back, read-once holding, and
# the in-memory test mode. No platform file is linked: the test's own stand-in store counts every call, so no
# keychain, Credential Manager or settings file is reached. The test lives in src/Secrets/tests; it is
# registered here, beside the password's other test, until src/Secrets has a test directory of its own.
pp_add_test(secret_store_test
    SOURCES ${SRC}/Secrets/tests/secret_store_test.cpp
            ${SRC}/Secrets/secret_store.cpp
            ${SRC}/Secrets/mail_credentials.cpp
    INCLUDE ${SRC})
set_tests_properties(secret_store_test PROPERTIES TIMEOUT 60)
pp_requirement(secret_store_test REQ-PRIV-3)
