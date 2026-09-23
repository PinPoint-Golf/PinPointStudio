# pinpoint_swingstore — see cmake/PinPointSwingStore.cmake for what it is and who links it.
#
# Its own directory for ONE reason: it is compiled optimised in every configuration, and on MSVC
# /O2 cannot be combined with the /RTC runtime checks a Debug build injects (cl D8016). Stripping
# /RTC from CMAKE_CXX_FLAGS_DEBUG here changes this directory's copy only, so no other target of
# the consumer's loses its checks.
#
# ⚠ WHY OPTIMISED. A Debug build is what runs in the studio, and the store sits under GUI-thread
# work: rating a shot rewrites its document. Measured on a 65 MB two-camera swing, saving took
# 1.5 s with the adapter and libppswing at -O0 (the library now optimises itself when embedded).
if(MSVC)
    string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Od" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")   # else D9025 per file
endif()

add_library(pinpoint_swingstore STATIC
    ${PP_SWINGSTORE_ROOT}/src/Export/swing_store.h
    ${PP_SWINGSTORE_ROOT}/src/Export/swing_store.cpp
    ${PP_SWINGSTORE_ROOT}/src/Export/ppsw_qt.h
    ${PP_SWINGSTORE_ROOT}/src/Export/ppsw_qt.cpp)
target_include_directories(pinpoint_swingstore PUBLIC ${PP_SWINGSTORE_ROOT}/src/Export)
target_link_libraries(pinpoint_swingstore PUBLIC Qt6::Core PRIVATE ppswing::ppswing)
target_compile_options(pinpoint_swingstore PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2>)
set_target_properties(pinpoint_swingstore PROPERTIES POSITION_INDEPENDENT_CODE ON)
