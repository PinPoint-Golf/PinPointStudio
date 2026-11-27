# Compile through ccache when it is installed (governance/controls/plan.md, BT-2), so a fix round that reverts an
# edit, a reconfigure, or a source compiled into two test targets with the same flags is served from the cache.
# Included before project() by the app's and the test umbrella's CMakeLists.txt; a launcher given on the command
# line wins, and -DPP_CCACHE=OFF turns it off. Where ccache is not installed (the studio PC) it does nothing. The
# cache's size is the machine's own setting (`ccache -M 4G` on the development Mac). Release builds never use it:
# src/Gui/app/app_info.cpp stamps __DATE__, and a cache that ignores time macros (the Mac's ccache.conf) would
# hand a release yesterday's build date.
option(PP_CCACHE "Compile through ccache when it is installed" ON)
# A multi-config generator (Xcode, Visual Studio) picks Debug or Release at build time, after this runs, so it never
# gets the cache: its release builds must not.
if(PP_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER AND NOT CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$"
   AND NOT CMAKE_GENERATOR MATCHES "Xcode|Visual Studio|Multi-Config")
    find_program(PP_CCACHE_PROGRAM ccache)
    if(PP_CCACHE_PROGRAM)
        foreach(_pp_lang C CXX OBJC OBJCXX)
            set(CMAKE_${_pp_lang}_COMPILER_LAUNCHER "${PP_CCACHE_PROGRAM}")
        endforeach()
        message(STATUS "Compiling through ccache: ${PP_CCACHE_PROGRAM}")
    endif()
endif()
