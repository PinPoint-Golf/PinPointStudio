# pp_platform — the Platform ring's compiled files of src/Core, as one static library (platform refactor B1.4's pilot,
# finished by #424). Defined once for both trees by pp_require_ring(platform) (cmake/PinPointRings.cmake): the app,
# the offline tools and every test that needs a Platform file link it; none of them lists these sources.
#
# It links only Qt Core, threads and the system. The Platform files that are not here, and why:
#  - pp_debug.cpp: it includes whisper.h, ggml.h and QMediaPlayer (it silences whisper/ggml and captures FFmpeg's
#    av_log), so it stays in the app's own list. ppInfo()/ppWarn() themselves are pp_log_stream.cpp, which is here.
#  - device_enumerator.cpp: an Adapters file in src/Core (the ring tables' R4).
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

set(_core ${PP_RINGS_SRC}/Core)
add_library(pp_platform STATIC
    ${_core}/pp_debug.h            # declares PpLogStream; its definition is pp_log_stream.cpp
    ${_core}/pp_log_stream.cpp
    ${_core}/PpMessageLog.h        ${_core}/PpMessageLog.cpp
    ${_core}/PpStatsLog.h          ${_core}/PpStatsLog.cpp
    ${_core}/AnalysisProfileLog.h  ${_core}/AnalysisProfileLog.cpp
    ${_core}/pp_profiler.h         ${_core}/pp_profiler.cpp
    ${_core}/pp_os_metrics.h       ${_core}/pp_os_metrics.cpp
    ${_core}/pp_gpu_metrics.h      ${_core}/pp_gpu_metrics.cpp
    ${_core}/folder_lock.h         ${_core}/folder_lock.cpp
    ${_core}/work_pool.h           ${_core}/work_pool.cpp
)

if(APPLE)
    # The Metal backend of pp_gpu_metrics (ObjC++). ARC manages the MTLDevice from MTLCreateSystemDefaultDevice().
    target_sources(pp_platform PRIVATE ${_core}/pp_gpu_metrics_mac.mm)
    set_source_files_properties(${_core}/pp_gpu_metrics_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
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
    PRIVATE   ${_core}
    INTERFACE ${_core})

target_link_libraries(pp_platform PUBLIC Qt6::Core Threads::Threads)

pp_ring_settings(pp_platform)
