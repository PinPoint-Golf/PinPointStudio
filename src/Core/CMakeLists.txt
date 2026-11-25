# pp_platform — the Platform ring's compiled files of src/Core, as one static library (platform refactor B1.4, the
# pilot of Part 2's ring libraries; its shape is proposed, for the Solution Architect). The app links it, and so does
# src/Core/tests; every other target still compiles the files it needs itself until Part 2.
#
# It links only Qt Core, threads and the system. The Platform files that are not here, and why:
#  - pp_debug.cpp: it includes whisper.h, ggml.h and QMediaPlayer (it silences whisper/ggml and captures FFmpeg's
#    av_log), so it stays in the app's own list. ppInfo()/ppWarn() themselves are pp_log_stream.cpp, which is here.
#  - the header-only ones (atomic_write.h, post_shot_trace.h, cpu_topology.h, pp_model_path.h, ort_log.h) compile
#    nothing; they are listed where they were. ort_log.h needs ONNX Runtime's header, so it could not be here.
# No source file here reads PINPOINT_DEBUG_LEVEL or PINPOINT_PROFILE*: those change only their callers' macros, so
# one build of the library serves every debug level and profiler tier a consumer compiles with.
#
# Include path: the library's own sources include their neighbours by quoted name. src/Core is exported
# (INTERFACE) because its users write #include "pp_debug.h"; that also shows them Core's other headers.

if(NOT TARGET Qt6::Core)
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()
find_package(Threads REQUIRED)

add_library(pp_platform STATIC
    pp_debug.h            # declares PpLogStream; its definition is pp_log_stream.cpp
    pp_log_stream.cpp
    PpMessageLog.h        PpMessageLog.cpp
    PpStatsLog.h          PpStatsLog.cpp
    AnalysisProfileLog.h  AnalysisProfileLog.cpp
    pp_profiler.h         pp_profiler.cpp
    pp_os_metrics.h       pp_os_metrics.cpp
    pp_gpu_metrics.h      pp_gpu_metrics.cpp
    folder_lock.h         folder_lock.cpp
    work_pool.h           work_pool.cpp
)

if(APPLE)
    # The Metal backend of pp_gpu_metrics (ObjC++). ARC manages the MTLDevice from MTLCreateSystemDefaultDevice().
    target_sources(pp_platform PRIVATE pp_gpu_metrics_mac.mm)
    set_source_files_properties(pp_gpu_metrics_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(pp_platform PUBLIC "-framework Metal" "-framework Foundation")
elseif(WIN32)
    # dxgi: the per-process / per-adapter VRAM query (IDXGIAdapter3). pp_os_metrics' K32GetProcessMemoryInfo is a
    # kernel32 export, so no psapi.lib.
    target_link_libraries(pp_platform PUBLIC dxgi)
else()
    # pp_gpu_metrics dlopen()s NVML (glibc < 2.34 keeps dlopen in libdl).
    target_link_libraries(pp_platform PUBLIC ${CMAKE_DL_LIBS})
endif()

target_include_directories(pp_platform
    PRIVATE   ${CMAKE_CURRENT_SOURCE_DIR}
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pp_platform PUBLIC Qt6::Core Threads::Threads)

if(MSVC)
    # The app's own MSVC definitions, so these files compile as they did inside it.
    target_compile_definitions(pp_platform PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
endif()

# The test tree's sanitizer flags (tests/cmake/PinPointTests.cmake), when it is the one building the library.
if(COMMAND pp_apply_sanitizers)
    pp_apply_sanitizers(pp_platform)
endif()
