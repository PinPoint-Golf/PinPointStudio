# PinPointRings.cmake — the ring libraries, one static library per ring (platform refactor Stage B Part 2, #424):
# pp_platform, then the inner rings each links. Shared by the app build (the root CMakeLists.txt) and the test
# umbrella (tests/cmake/PinPointTests.cmake), as cmake/PinPointSwingStore.cmake is: one definition for both trees.
#
#   pp_require_ring(platform)    defines pp_platform once (cmake/rings/platform/CMakeLists.txt), in this build tree
#
# Each library lives in cmake/rings/<name>/CMakeLists.txt and names its sources by path from PP_RINGS_SRC (src/). It
# links only inner rings, PUBLIC, so an order-sensitive linker (GNU ld) sees each inner library after its user. A
# source is in one library and in no executable that links it: library_membership_lint_test (the test tree) and the
# app tree's configure check (cmake/PinPointTargetManifest.cmake) hold that.
#
# pp_ring_settings(<target>) is what every ring library calls on itself:
#  - the app tree's PINPOINT_DEBUG_LEVEL and PINPOINT_PROFILE* (PP_RING_DEFINITIONS, set by the root CMakeLists.txt
#    before the first pp_require_ring): a library source that calls ppDebug() or a profiler scope expands it with the
#    app's level and tiers, as it did when it was one of the app's own files. The test tree sets none, so its
#    libraries take pp_debug.h's and pp_profiler.h's defaults, as a hand-listed source did;
#  - under MSVC, the app's _CRT_SECURE_NO_WARNINGS and _USE_MATH_DEFINES, and /bigobj (the Debug objects of
#    template-heavy files pass the 65,279-section limit, C1128);
#  - the test tree's sanitizer flags (pp_apply_sanitizers, tests/cmake/PinPointTests.cmake), when it is the one
#    building the library.

get_filename_component(PP_RINGS_SRC "${CMAKE_CURRENT_LIST_DIR}/../src" ABSOLUTE)
set(PP_RINGS_SRC "${PP_RINGS_SRC}" CACHE INTERNAL "The source root the ring libraries name their files from")
set(PP_RINGS_DIR "${CMAKE_CURRENT_LIST_DIR}/rings" CACHE INTERNAL "The ring libraries' build files")

function(pp_require_ring name)
    if(TARGET pp_${name})
        return()
    endif()
    if(NOT EXISTS "${PP_RINGS_DIR}/${name}/CMakeLists.txt")
        message(FATAL_ERROR "pp_require_ring(${name}): no ring library ${PP_RINGS_DIR}/${name}/CMakeLists.txt")
    endif()
    add_subdirectory("${PP_RINGS_DIR}/${name}" "${CMAKE_BINARY_DIR}/rings/${name}")
endfunction()

function(pp_ring_settings target)
    if(PP_RING_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${PP_RING_DEFINITIONS})
    endif()
    if(MSVC)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
        target_compile_options(${target} PRIVATE /bigobj)
    endif()
    if(COMMAND pp_apply_sanitizers)
        pp_apply_sanitizers(${target})
    endif()
endfunction()
