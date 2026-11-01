# PinPointOpenSSL.cmake — locate OpenSSL for the PPCP transport (src/Ppcp).
#
# Shared by the app's top-level CMakeLists.txt and by the standalone test suite
# in src/Ppcp/tests, the same way cmake/PinPointQtPrefix.cmake is shared, so a
# developer gets the same OpenSSL whichever they configure.
#
# WHY OpenSSL AT ALL, when Qt Network is already linked.  PPCP-RV §5 needs a TLS
# EXTERNAL pre-shared key, and RV §8 says in as many words that the PSK
# interfaces in common toolkits — identity hints, RFC 4279 ciphersuite selection
# — do not reach TLS 1.3 external PSKs.  Qt's QSslPreSharedKeyAuthenticator is
# one of those interfaces.  The transport therefore drives OpenSSL's external-PSK
# session callbacks directly, which means the headers, not just the runtime Qt
# already loads.
#
# A missing OpenSSL is NOT fatal here.  It leaves PP_OPENSSL_FOUND false and the
# caller decides — the app excludes the transport with a warning, the test suite
# skips itself.  That keeps a Linux or Windows box without libssl-dev configuring
# as it always did, which the alternative (a hard find_package REQUIRED) would
# not.  It is a build-time absence, not a runtime downgrade: nothing in this
# project can fall back to an unencrypted connection (RV 5.2f) because no such
# code path exists.

include_guard(GLOBAL)

# Runs its find_package on EVERY configure, not once per build directory. A
# cached "yes" is not enough: find_package is what creates the OpenSSL::SSL
# imported target, and a target does not survive into the next configure. Caching
# the answer instead of repeating the question is how a reconfigure ends up
# linking to a target that was never defined.
macro(pp_find_openssl)
    if(NOT TARGET OpenSSL::SSL)
        # Homebrew's openssl@3 is keg-only, so CMake will not find it on macOS
        # without a hint — and what it WOULD find otherwise is LibreSSL, whose
        # SSL_CTX_set_psk_use_session_callback does not exist.  Probe the prefix
        # the same way the espeak-ng block in the top-level file does.
        if(APPLE AND NOT OPENSSL_ROOT_DIR)
            execute_process(
                COMMAND brew --prefix openssl@3
                OUTPUT_VARIABLE _pp_brew_openssl
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE _pp_brew_result)
            if(_pp_brew_result EQUAL 0 AND EXISTS "${_pp_brew_openssl}/include/openssl/ssl.h")
                set(OPENSSL_ROOT_DIR "${_pp_brew_openssl}")
            endif()
            unset(_pp_brew_openssl)
            unset(_pp_brew_result)
        endif()

        # Windows has no system OpenSSL, so without a hint find_package finds
        # nothing, and that failure is silent apart from one status line: PPCP
        # quietly left out of the v0.1-beta1 installer (2026-09-03), and the
        # release gate skipped the PPCP transport suite until 2026-11-01. So
        # every configure that left out -DOPENSSL_ROOT_DIR got it wrong. Default
        # it to the vcpkg tree: the project's own (C:/pp-vcpkg, which
        # build_installer.ps1 also defaults to), then VCPKG_ROOT's classic
        # layout. An explicit -DOPENSSL_ROOT_DIR still wins.
        if(WIN32 AND NOT OPENSSL_ROOT_DIR)
            foreach(_pp_vcpkg_root
                    "C:/pp-vcpkg/vcpkg_installed/x64-windows"
                    "$ENV{VCPKG_ROOT}/installed/x64-windows")
                if(EXISTS "${_pp_vcpkg_root}/include/openssl/ssl.h")
                    set(OPENSSL_ROOT_DIR "${_pp_vcpkg_root}")
                    message(STATUS "OpenSSL: OPENSSL_ROOT_DIR not given, using ${_pp_vcpkg_root}")
                    break()
                endif()
            endforeach()
            unset(_pp_vcpkg_root)
        endif()

        # 1.1.1 is the floor: SSL_CTX_set_psk_use_session_callback and
        # SSL_CTX_set_psk_find_session_callback — the external-PSK session
        # callbacks RV §8 points at — arrived with TLS 1.3 support in 1.1.1.
        find_package(OpenSSL 1.1.1 QUIET COMPONENTS SSL Crypto)

        if(OpenSSL_FOUND)
            message(STATUS "OpenSSL ${OPENSSL_VERSION}: ${OPENSSL_SSL_LIBRARY}")
        endif()
    endif()

    if(TARGET OpenSSL::SSL)
        set(PP_OPENSSL_FOUND TRUE)
        set(PP_OPENSSL_VERSION "${OPENSSL_VERSION}")
    else()
        set(PP_OPENSSL_FOUND FALSE)
        set(PP_OPENSSL_VERSION "")
    endif()

    # Windows: where the DLLs live. vcpkg keeps them in <root>/bin, beside the
    # import libs' <root>/lib. The app copies them next to its exe, but a test
    # exe only finds them on PATH. A missing libssl-3-x64.dll is exit 0xc0000135
    # before main(). For Qt's TLS plugin it's worse: Qt falls back to Schannel
    # without a word. pp_openssl_test_env() puts this folder on a test's PATH.
    set(PP_OPENSSL_BIN_DIR "")
    if(WIN32 AND PP_OPENSSL_FOUND AND OPENSSL_SSL_LIBRARY)
        get_filename_component(_pp_ssl_libdir "${OPENSSL_SSL_LIBRARY}" DIRECTORY)
        get_filename_component(_pp_ssl_bin "${_pp_ssl_libdir}/../bin" ABSOLUTE)
        file(GLOB _pp_ssl_dll "${_pp_ssl_bin}/libssl-*.dll")
        if(_pp_ssl_dll)
            set(PP_OPENSSL_BIN_DIR "${_pp_ssl_bin}")
        endif()
        unset(_pp_ssl_libdir)
        unset(_pp_ssl_bin)
        unset(_pp_ssl_dll)
    endif()
endmacro()

# Puts OpenSSL's DLL folder on the named tests' PATH (Windows; a no-op elsewhere,
# and a no-op when OpenSSL wasn't found). Call after pp_find_openssl().
function(pp_openssl_test_env)
    if(WIN32 AND PP_OPENSSL_BIN_DIR)
        foreach(_t IN LISTS ARGN)
            set_property(TEST ${_t} APPEND PROPERTY
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${PP_OPENSSL_BIN_DIR}")
        endforeach()
    endif()
endfunction()
