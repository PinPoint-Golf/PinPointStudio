# DevCodesign.cmake — sign the dev .app with a stable self-signed identity.
#
# Run via `cmake -DBUNDLE=<app> -DIDENTITY=<name> -P cmake/DevCodesign.cmake`.
#
# macOS keys camera / microphone / speech / Bluetooth permission grants (TCC) to
# the running binary's code signature, not just its bundle id.  An unsigned
# build has no durable identity to match, and an ad-hoc-signed one gets a fresh
# cdhash every rebuild — so either way a grant made in System Settings does not
# apply to the next run and every entitled API returns Denied, even though the
# app shows as "enabled" there.  Signing the finished bundle with a stable
# self-signed certificate gives TCC something constant to match, so a grant made
# once survives all subsequent rebuilds.
#
# Must run AFTER the bundle is otherwise final (the Info.plist force-copy in
# particular): any write into the bundle after codesign invalidates the seal.
# The release pipeline re-signs with Developer ID + notarises separately; this is
# strictly the local dev loop.

# security find-identity lists signing certs as e.g. `1) <hash> "Name"`.  We do
# NOT pass -v ("valid only"): a self-signed dev cert is an untrusted root, so -v
# hides it (it lists as `"Name" (CSSMERR_TP_NOT_TRUSTED)`).  codesign signs with
# it regardless of trust, and TCC keys the grant to the signature either way, so
# trust is irrelevant here — we only need the identity to exist.
execute_process(
    COMMAND security find-identity -p codesigning
    OUTPUT_VARIABLE _identities
    ERROR_QUIET)

if(_identities MATCHES "\"${IDENTITY}\"")
    execute_process(
        COMMAND codesign --force --deep --sign "${IDENTITY}" "${BUNDLE}"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "codesign failed (exit ${_rc}) signing ${BUNDLE}")
    endif()
    message(STATUS "Signed ${BUNDLE} with '${IDENTITY}' (TCC-stable dev identity)")
else()
    message(WARNING
        "Dev signing identity '${IDENTITY}' not found in keychain — bundle left unsigned.\n"
        "  Camera / microphone / speech / Bluetooth grants will NOT persist across\n"
        "  rebuilds until a stable signing certificate exists.  Create one once:\n"
        "    Keychain Access ▸ Certificate Assistant ▸ Create a Certificate…\n"
        "      Name: ${IDENTITY}\n"
        "      Identity Type: Self Signed Root\n"
        "      Certificate Type: Code Signing\n"
        "  Then rebuild and grant each permission once; the grants then stick.\n"
        "  Override the name with -DPINPOINT_DEV_CODESIGN_IDENTITY=<name>.")
endif()
