# PinPointSwingStore.cmake — the swing-document store library, shared by the app build and the
# test umbrella.
#
# pinpoint_swingstore is src/Export/swing_store.cpp + ppsw_qt.cpp: THE door to a swing's
# document on disk (swing.ppsw, or a legacy swing.json). Every target that reads or writes a
# document links it — the app, the swinglab/lm_repair tools, and most test suites, which compile
# swing_doc.cpp and friends straight from source.
#
# It is a library rather than two more sources on every link line because it is the ONLY place
# libppswing is reached: the ppsw headers stay inside these two files (swing_store.h names no ppsw
# type), so a target gains the new format by linking one name.
#
# Caller contract: the `ppswing::ppswing` target already exists (FetchContent of libppswing) and
# Qt6::Core is found. Idempotent.
function(pp_define_swingstore repo_root)
    if(TARGET pinpoint_swingstore)
        return()
    endif()
    # Its own directory (cmake/swingstore) so it can be built optimised in a Debug configuration —
    # see the note there. Called from a function, so the root is passed through a variable.
    set(PP_SWINGSTORE_ROOT ${repo_root})
    add_subdirectory(${repo_root}/cmake/swingstore ${CMAKE_BINARY_DIR}/pinpoint_swingstore)
endfunction()
