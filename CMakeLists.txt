cmake_minimum_required(VERSION 3.16)

project(PinPoint VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Quick3D SerialPort Bluetooth Multimedia Network WebSockets Concurrent ShaderTools)

qt_standard_project_setup(REQUIRES 6.10)

include(FetchContent)

FetchContent_Declare(
  whisper
  GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
  GIT_TAG        v1.7.2
  GIT_SHALLOW    TRUE
)

set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)

# ── GPU acceleration for whisper.cpp (Linux / Windows) ─────────────────────
# Vulkan works on NVIDIA, AMD, and Intel without proprietary drivers.
# CUDA is tried as a fallback when Vulkan headers are not present.
if(NOT APPLE)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        set(GGML_VULKAN ON CACHE BOOL "" FORCE)
        set(_whisper_gpu_backend "Vulkan")
        message(STATUS "Whisper: Vulkan GPU acceleration enabled")
    else()
        # check_language(CUDA) is verbose even on failure; guard with a silent
        # find_program first so machines without nvcc don't get noisy output.
        find_program(_nvcc_path nvcc QUIET)
        if(_nvcc_path)
            include(CheckLanguage)
            check_language(CUDA)
        endif()
        if(CMAKE_CUDA_COMPILER)
            enable_language(CUDA)
            set(GGML_CUDA ON CACHE BOOL "" FORCE)
            # Target only the GPU(s) present; avoids compiling for old architectures
            # (e.g. compute_52) that CUDA 12.x dropped support for.
            set(CMAKE_CUDA_ARCHITECTURES native CACHE STRING "" FORCE)
            if(MSVC)
                # CUDA 13+ CCCL headers require the MSVC standard-conforming
                # preprocessor; the traditional one triggers a hard #error.
                string(APPEND CMAKE_CUDA_FLAGS " -Xcompiler /Zc:preprocessor")
            endif()
            # CUDA 13+ CUB/CCCL headers require C++17.
            set(CMAKE_CUDA_STANDARD 17 CACHE STRING "" FORCE)
            set(CMAKE_CUDA_STANDARD_REQUIRED ON CACHE BOOL "" FORCE)
            set(_whisper_gpu_backend "CUDA")
            message(STATUS "Whisper: CUDA GPU acceleration enabled")
        else()
            message(STATUS "Whisper: CPU only (install Vulkan SDK or CUDA Toolkit for GPU acceleration)")
        endif()
    endif()
endif()

FetchContent_MakeAvailable(whisper)

FetchContent_Declare(
  libsamplerate
  GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
  GIT_TAG        0.2.2
  GIT_SHALLOW    TRUE
  PATCH_COMMAND
    ${CMAKE_COMMAND}
    "-DFILE=${CMAKE_BINARY_DIR}/_deps/libsamplerate-src/CMakeLists.txt"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchLibsamplerate.cmake"
)
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libsamplerate)

qt_add_executable(PinPoint
    src/Gui/main.cpp
    src/Gui/imu_controller.h           src/Gui/imu_controller.cpp
    src/Gui/transcription_controller.h src/Gui/transcription_controller.cpp
    src/Gui/tts_controller.h           src/Gui/tts_controller.cpp
    src/IMU/wt9011dcl_base.h src/IMU/wt9011dcl_base.cpp
    src/IMU/wt9011dcl.h      src/IMU/wt9011dcl.cpp
    src/IMU/wt9011dcl_ble.h  src/IMU/wt9011dcl_ble.cpp
    src/Audio/audio_processor_base.h src/Audio/audio_processor_base.cpp
    src/Audio/audio_input_base.h     src/Audio/audio_input_base.cpp
    src/Audio/audio_input.h          src/Audio/audio_input.cpp
    src/Audio/audio_output_base.h    src/Audio/audio_output_base.cpp
    src/Audio/audio_output.h         src/Audio/audio_output.cpp
    src/Audio/audio_stream_saver.h   src/Audio/audio_stream_saver.cpp
    src/Video/video_input_base.h           src/Video/video_input_base.cpp
    src/Video/video_input.h                src/Video/video_input.cpp
    src/Video/video_input_factory.h        src/Video/video_input_factory.cpp
    src/Video/video_preprocessor_base.h   src/Video/video_preprocessor_base.cpp
    src/Pose/pose_estimator_base.h        src/Pose/pose_estimator_base.cpp
    src/Core/device_enumerator.h           src/Core/device_enumerator.cpp
    src/Video/VideoInputAravis.h           src/Video/VideoInputAravis.cpp
    src/Gui/video_controller.h             src/Gui/video_controller.cpp
    src/STT/stt_processor.h          src/STT/stt_processor.cpp
    src/TTS/TTSEngine.h              src/TTS/TTSEngine.cpp
    src/TTS/TTSEngineFactory.h       src/TTS/TTSEngineFactory.cpp
    src/TTS/AzureTTSEngine.h         src/TTS/AzureTTSEngine.cpp
    src/TTS/KokoroTTSEngine.h        src/TTS/KokoroTTSEngine.cpp
    src/TTS/PhonemeTokenizer.h       src/TTS/PhonemeTokenizer.cpp
    src/TTS/TtsWorker.h              src/TTS/TtsWorker.cpp
    src/TTS/ModelDownloader.h        src/TTS/ModelDownloader.cpp
)

target_include_directories(PinPoint PRIVATE src/Gui src/IMU src/Audio src/Video src/Pose src/STT src/TTS src/Secrets src/Core)

qt_add_qml_module(PinPoint
    URI PinPoint
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/PinPoint
    QML_FILES
        src/Gui/Main.qml
        src/Gui/AudioPage.qml
        src/Gui/CapturePage.qml
        src/Gui/VideoPage.qml
)

qt6_add_shaders(PinPoint "debayer_shaders"
    BATCHABLE
    PRECOMPILE
    PREFIX "/shaders"
    FILES
        "src/Gui/debayer.vert"
        "src/Gui/debayer.frag"
)

target_compile_definitions(PinPoint PRIVATE
    QT_APPLICATION_NAME="PinPoint"
)

set_target_properties(PinPoint PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# Qt's CMake infrastructure ignores MACOSX_BUNDLE_INFO_PLIST in some
# configurations.  Generate the plist at configure time and copy it into
# the bundle with a POST_BUILD step so it always wins.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    COPYONLY
)


target_link_libraries(PinPoint
    PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Quick3D Qt6::SerialPort Qt6::Bluetooth Qt6::Multimedia Qt6::Network Qt6::WebSockets Qt6::Concurrent
)


target_sources(PinPoint PRIVATE
    src/Audio/AudioConverter.h
    src/Audio/AudioConverter.cpp
    src/STT/STTBackend.h
    src/STT/STTBackendFactory.h
    src/STT/STTBackendFactory.cpp
    src/STT/STTBackendWhisperCpp.h
    src/STT/STTBackendWhisperCpp.cpp
    src/STT/STTBackendAssemblyAI.h
    src/STT/STTBackendAssemblyAI.cpp
    src/STT/STTBackendAzure.h
    src/STT/STTBackendAzure.cpp
    src/STT/STTWorker.h
    src/STT/STTWorker.cpp
)

target_sources(PinPoint PRIVATE
    src/Secrets/SecretsManager.h
    src/Secrets/SecretsManager.cpp
)

# ── Azure cloud STT fallback ──────────────────────────────────────────────────
# The Azure STT backend uses the same Cognitive Services Speech resource as the
# TTS backend, so AZURE_TTS_API_KEY is checked automatically as a fallback when
# AZURE_STT_API_KEY is not set.  Set either env var before the first launch and
# the value is saved to QSettings so the env var is not required on future runs.
message(STATUS "Azure STT: key supplied via AZURE_STT_API_KEY env var or shared AZURE_TTS_API_KEY")

# ── AssemblyAI cloud STT (kept for reference, not used as fallback) ───────────
# Pass -DASSEMBLYAI_API_KEY=<key> at cmake configure time to seed the key into
# QSettings on first run. The key can also be supplied via the
# ASSEMBLYAI_API_KEY environment variable (takes priority over QSettings).
set(ASSEMBLYAI_API_KEY "" CACHE STRING
    "AssemblyAI API key — written to QSettings on first run, not needed after that")
if(ASSEMBLYAI_API_KEY)
    target_compile_definitions(PinPoint PRIVATE
        ASSEMBLYAI_API_KEY_DEFAULT="${ASSEMBLYAI_API_KEY}")
    message(STATUS "AssemblyAI: API key provided (will be seeded to QSettings on first run)")
else()
    message(STATUS "AssemblyAI: no API key configured")
endif()

target_link_libraries(PinPoint PRIVATE whisper samplerate)
target_include_directories(PinPoint PRIVATE
    ${whisper_SOURCE_DIR}/include
    ${whisper_SOURCE_DIR}/ggml/include
    ${libsamplerate_SOURCE_DIR}/include
)

# ── ONNX Runtime (auto-fetched prebuilt) ─────────────────────────────────────
# Three independent EP flags — each defaults ON; platform guards below control
# which ones are actually wired up and which GPU binary is downloaded.
# CPU EP is always the implicit fallback at runtime regardless of these flags.
option(WITH_CUDA     "Enable CUDA execution provider (Linux/Windows)"    ON)
option(WITH_DIRECTML "Enable DirectML execution provider (Windows only)" ON)
option(WITH_COREML   "Enable CoreML execution provider (macOS ARM64)"    ON)

set(ORT_VERSION "1.25.1")

# Select prebuilt binary.
# Windows has two mutually exclusive GPU packages:
#   gpu package  — CUDA + TensorRT (no DirectML header/runtime)
#   std package  — CPU + DirectML  (no CUDA)
# Therefore WITH_CUDA and WITH_DIRECTML are mutually exclusive on Windows.
# CUDA wins when both are requested; DirectML is silently skipped.
if(WIN32)
    if(WITH_CUDA)
        # The ORT CUDA provider DLL is compiled against a specific CUDA major version
        # and imports e.g. cublasLt64_12.dll.  A different CUDA major on the host
        # means that DLL is absent and ORT fails to load the provider at runtime.
        # Detect the installed CUDA major version and fall back to the standard
        # Windows package (DirectML) when the toolkit is a different major than ORT's.
        find_package(CUDAToolkit QUIET)
        set(_ort_cuda_major 12)  # ORT ${ORT_VERSION} CUDA EP was built against CUDA 12
        if(CUDAToolkit_FOUND AND NOT CUDAToolkit_VERSION_MAJOR EQUAL _ort_cuda_major)
            message(STATUS "ORT CUDA EP: installed CUDA ${CUDAToolkit_VERSION} != "
                "ORT build CUDA ${_ort_cuda_major} — using standard package (DirectML)")
            set(WITH_CUDA OFF CACHE BOOL "" FORCE)
            set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip")
        else()
            set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-gpu-${ORT_VERSION}.zip")
        endif()
    else()
        # Standard Windows package includes DirectML support.
        set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip")
    endif()
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-arm64-${ORT_VERSION}.tgz")
        # CoreML is included in every macOS ARM64 prebuilt — no separate download needed.
    else()
        # ONNX Runtime 1.21+ dropped Intel macOS (x86_64) prebuilts; pin to the last available.
        set(ORT_VERSION "1.20.1")
        set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-x86_64-${ORT_VERSION}.tgz")
        # CoreML is present in the x86_64 dylib but the Kokoro model has fully
        # dynamic sequence lengths throughout — CoreML on Intel rejects virtually
        # every subgraph ("unbounded dimension"), then hangs or fails to compile
        # what remains. CPU is faster and more correct here.
    endif()
else()  # Linux
    if(WITH_CUDA)
        set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-gpu-${ORT_VERSION}.tgz")
    else()
        set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz")
    endif()
endif()

FetchContent_Declare(onnxruntime URL "${_ort_url}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(onnxruntime)

# The archive may or may not strip its top-level directory depending on the
# CMake version; probe for the headers to find the actual root.
if(EXISTS "${onnxruntime_SOURCE_DIR}/include/onnxruntime_cxx_api.h")
    set(_ort_root "${onnxruntime_SOURCE_DIR}")
else()
    file(GLOB _ort_candidates LIST_DIRECTORIES true "${onnxruntime_SOURCE_DIR}/onnxruntime-*")
    list(GET _ort_candidates 0 _ort_root)
endif()

target_include_directories(PinPoint PRIVATE "${_ort_root}/include")
target_compile_definitions(PinPoint PRIVATE HAVE_ONNXRUNTIME)

# Propagate each EP flag to C++ only on platforms where it is supported.
if(WITH_COREML AND APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    target_compile_definitions(PinPoint PRIVATE WITH_COREML)
endif()
if(WITH_CUDA AND NOT APPLE)
    target_compile_definitions(PinPoint PRIVATE WITH_CUDA)
endif()
if(WITH_DIRECTML AND WIN32)
    # dml_provider_factory.h ships only in the standard Windows package, not the
    # CUDA GPU package.  Guard against a stale cached CUDA download.
    if(EXISTS "${_ort_root}/include/dml_provider_factory.h")
        target_compile_definitions(PinPoint PRIVATE WITH_DIRECTML)
    else()
        message(WARNING "WITH_DIRECTML: dml_provider_factory.h not found in the cached ORT package "
            "(likely the CUDA GPU package is cached). "
            "Delete _deps/onnxruntime-src and reconfigure to fetch the standard Windows package.")
    endif()
endif()

if(WIN32)
    target_link_libraries(PinPoint PRIVATE "${_ort_root}/lib/onnxruntime.lib")
    # Collect all ORT DLLs (main + any provider DLLs present in the package).
    file(GLOB _ort_all_dlls "${_ort_root}/lib/onnxruntime*.dll")
    # POST_BUILD: copy DLLs next to the executable so they are found at runtime
    # during development builds (install() rules only run on cmake --install).
    foreach(_dll ${_ort_all_dlls})
        add_custom_command(TARGET PinPoint POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:PinPoint>"
        )
    endforeach()
    foreach(_dll ${_ort_all_dlls})
        install(FILES "${_dll}" DESTINATION bin)
    endforeach()
elseif(APPLE)
    file(GLOB _ort_dylib "${_ort_root}/lib/libonnxruntime*.dylib")
    list(GET _ort_dylib 0 _ort_dylib_path)
    target_link_libraries(PinPoint PRIVATE "${_ort_dylib_path}")
else()
    target_link_libraries(PinPoint PRIVATE "${_ort_root}/lib/libonnxruntime.so")
endif()

set(_ort_ep_list "CPU(fallback)")
if(WITH_COREML AND APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    list(PREPEND _ort_ep_list "CoreML")
endif()
if(WITH_CUDA AND NOT APPLE)
    list(PREPEND _ort_ep_list "CUDA")
endif()
if(WITH_DIRECTML AND WIN32)
    list(PREPEND _ort_ep_list "DirectML")
endif()
list(JOIN _ort_ep_list "+" _ort_ep_str)
message(STATUS "ONNX Runtime ${ORT_VERSION}: ${_ort_root} [EP=${_ort_ep_str}]")

# ── MoveNet (pose estimation via ONNX Runtime) ────────────────────────────────
# Downloads the MoveNet Lightning SinglePose ONNX model from Hugging Face.
# No additional inference library required — ONNX Runtime (above) handles it.
# Cached in _deps/movenet/ and reused across reconfigures; only lost on a full
# build-directory wipe.
#
# Model input:  [1, 192, 192, 3] float32 NHWC, normalised to [0, 1]
# Model output: [1, 1, 17, 3]   float32, keypoints as (y, x, score)

set(_movenet_model_dir  "${CMAKE_BINARY_DIR}/_deps/movenet")
set(_movenet_model_file "movenet_singlepose_lightning.onnx")
set(_movenet_model_path "${_movenet_model_dir}/${_movenet_model_file}")

if(NOT EXISTS "${_movenet_model_path}")
    message(STATUS "MoveNet: downloading model (~9 MB)...")
    file(MAKE_DIRECTORY "${_movenet_model_dir}")
    file(DOWNLOAD
        "https://huggingface.co/Xenova/movenet-singlepose-lightning/resolve/main/onnx/model.onnx"
        "${_movenet_model_path}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        STATUS _movenet_dl_status
    )
    list(GET _movenet_dl_status 0 _movenet_dl_code)
    if(NOT _movenet_dl_code EQUAL 0)
        list(GET _movenet_dl_status 1 _movenet_dl_msg)
        message(WARNING "MoveNet: download failed (${_movenet_dl_msg}) -- pose estimation disabled")
        file(REMOVE "${_movenet_model_path}")
    endif()
endif()

if(EXISTS "${_movenet_model_path}")
    message(STATUS "MoveNet: model ready (${_movenet_model_path})")
    target_compile_definitions(PinPoint PRIVATE
        HAVE_MOVENET
        MOVENET_MODEL_FILE="${_movenet_model_file}"
    )

    # Copy model into the platform-appropriate location alongside the executable.
    if(APPLE)
        add_custom_command(TARGET PinPoint POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:PinPoint>/Resources/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_movenet_model_path}"
                "$<TARGET_BUNDLE_CONTENT_DIR:PinPoint>/Resources/models/${_movenet_model_file}"
            COMMENT "Bundling MoveNet model into app"
        )
    else()
        add_custom_command(TARGET PinPoint POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:PinPoint>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_movenet_model_path}"
                "$<TARGET_FILE_DIR:PinPoint>/models/${_movenet_model_file}"
            COMMENT "Copying MoveNet model to output directory"
        )
    endif()
else()
    message(STATUS "MoveNet: model unavailable -- pose estimation disabled")
endif()

# ── espeak-ng ────────────────────────────────────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ESPEAK QUIET espeak-ng)
endif()

if(NOT ESPEAK_FOUND AND APPLE)
    # pkg-config is often absent on macOS; probe the Homebrew prefix directly.
    execute_process(
        COMMAND brew --prefix espeak-ng
        OUTPUT_VARIABLE _brew_espeak
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _brew_result
    )
    if(_brew_result EQUAL 0 AND EXISTS "${_brew_espeak}/lib/libespeak-ng.dylib")
        set(ESPEAK_FOUND     TRUE)
        set(ESPEAK_INCLUDE_DIRS "${_brew_espeak}/include")
        set(ESPEAK_LIBRARY_DIRS "${_brew_espeak}/lib")
        set(ESPEAK_LIBRARIES    "espeak-ng")
        set(ESPEAK_VERSION      "Homebrew")
    endif()
endif()

if(ESPEAK_FOUND)
    target_include_directories(PinPoint PRIVATE ${ESPEAK_INCLUDE_DIRS})
    target_link_libraries(PinPoint PRIVATE ${ESPEAK_LIBRARIES})
    target_link_directories(PinPoint PRIVATE ${ESPEAK_LIBRARY_DIRS})
    target_compile_definitions(PinPoint PRIVATE HAVE_ESPEAK_NG)
    message(STATUS "espeak-ng ${ESPEAK_VERSION}: system")
else()
    # 1.52.0 is the first espeak-ng release with a CMake build system.
    # PATCH_COMMAND changes the espeak-include link from PUBLIC to PRIVATE so
    # its compat/ shim directory does not leak into PinPoint's include path and
    # shadow system <wctype.h> on macOS.
    # EXCLUDE_FROM_ALL limits compilation to the library target only.
    FetchContent_Declare(
        espeak_ng
        GIT_REPOSITORY https://github.com/espeak-ng/espeak-ng.git
        GIT_TAG        1.52.0
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL
        PATCH_COMMAND
            ${CMAKE_COMMAND}
            "-DFILE1=${CMAKE_BINARY_DIR}/_deps/espeak_ng-src/src/CMakeLists.txt"
            "-DFILE2=${CMAKE_BINARY_DIR}/_deps/espeak_ng-src/src/libespeak-ng/CMakeLists.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchEspeakNg.cmake"
    )
    set(ESPEAK_BUILD_MANPAGES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(espeak_ng)
    target_link_libraries(PinPoint PRIVATE espeak-ng)
    target_include_directories(PinPoint PRIVATE "${espeak_ng_SOURCE_DIR}/src/include")
    target_compile_definitions(PinPoint PRIVATE HAVE_ESPEAK_NG)

    # espeak-ng's own `data` target (defined in cmake/data.cmake) compiles the
    # phoneme tables and language dictionaries using espeak-ng-bin as a
    # build-time tool.  Depending on it directly avoids cross-directory
    # custom-command scoping issues.
    add_dependencies(PinPoint data)

    if(APPLE)
        # Bundle the compiled data (+ lang/ and voices/ copied at configure time
        # by data.cmake) into the macOS app so espeak_Initialize can find it.
        add_custom_command(TARGET PinPoint POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${espeak_ng_BINARY_DIR}/espeak-ng-data"
                "$<TARGET_BUNDLE_CONTENT_DIR:PinPoint>/Resources/espeak-ng-data"
            COMMENT "Bundling espeak-ng data into app"
        )
    else()
        # On Linux (and Windows) building from source, the compiled-in PKGDATADIR
        # won't match the build tree.  Pass the actual build-dir path so
        # espeak_Initialize can find the phoneme tables at runtime.
        target_compile_definitions(PinPoint PRIVATE
            ESPEAK_DATA_PATH="${espeak_ng_BINARY_DIR}")
    endif()

    message(STATUS "espeak-ng 1.52.0: built from source")
endif()

# ── Aravis (Industrial Camera Support) ──────────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ARAVIS aravis-0.8)
endif()

if(NOT ARAVIS_FOUND)
    if(APPLE)
        # Probe Homebrew directly as a fallback
        execute_process(
            COMMAND brew --prefix aravis
            OUTPUT_VARIABLE _brew_aravis
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _brew_result
        )
        if(_brew_result EQUAL 0 AND EXISTS "${_brew_aravis}/lib/libaravis-0.8.dylib")
            set(ARAVIS_FOUND        TRUE)
            set(ARAVIS_INCLUDE_DIRS "${_brew_aravis}/include/aravis-0.8")
            set(ARAVIS_LIBRARY_DIRS "${_brew_aravis}/lib")
            set(ARAVIS_LIBRARIES    "aravis-0.8")
            set(ARAVIS_VERSION      "Homebrew")
        endif()
    elseif(WIN32)
        # Manual search fallback for Windows
        find_path(ARAVIS_INCLUDE_DIRS NAMES arv.h PATH_SUFFIXES aravis-0.8 HINTS ${ARAVIS_ROOT} $ENV{ARAVIS_ROOT})
        find_library(ARAVIS_LIBRARIES NAMES aravis-0.8 HINTS ${ARAVIS_ROOT} $ENV{ARAVIS_ROOT})
        if(ARAVIS_INCLUDE_DIRS AND ARAVIS_LIBRARIES)
            set(ARAVIS_FOUND TRUE)
            set(ARAVIS_VERSION "Manual")
        endif()
    endif()
endif()

if(ARAVIS_FOUND)
    message(STATUS "Aravis ${ARAVIS_VERSION}: found")
    target_compile_definitions(PinPoint PRIVATE HAVE_ARAVIS)
    target_include_directories(PinPoint PRIVATE ${ARAVIS_INCLUDE_DIRS})
    target_link_libraries(PinPoint PRIVATE ${ARAVIS_LIBRARIES})
    if(ARAVIS_LIBRARY_DIRS)
        target_link_directories(PinPoint PRIVATE ${ARAVIS_LIBRARY_DIRS})
    endif()
    
    # Aravis depends on GLib, GObject, and GIO. pkg-config usually handles this,
    # but for manual/brew fallbacks we need to ensure they are available.
    if(PkgConfig_FOUND)
        pkg_check_modules(GLIB REQUIRED glib-2.0 gobject-2.0 gio-2.0)
        target_include_directories(PinPoint PRIVATE ${GLIB_INCLUDE_DIRS})
        target_link_libraries(PinPoint PRIVATE ${GLIB_LIBRARIES})
    endif()
else()
    message(STATUS "Aravis 0.8: not found (industrial camera support disabled)")
endif()

# ── Spinnaker (High-Speed Camera Support, Windows Only) ──────────────────────
if(WIN32)
    set(SPINNAKER_ROOT "C:/Program Files/Teledyne/Spinnaker")
    find_path(SPINNAKER_INCLUDE_DIRS NAMES Spinnaker.h HINTS ${SPINNAKER_ROOT}/include)
    find_library(SPINNAKER_LIBRARIES NAMES Spinnaker_v140 HINTS ${SPINNAKER_ROOT}/lib64/vs2015)

    if(SPINNAKER_INCLUDE_DIRS AND SPINNAKER_LIBRARIES)
        message(STATUS "Spinnaker: found at ${SPINNAKER_ROOT}")
        target_compile_definitions(PinPoint PRIVATE HAVE_SPINNAKER)
        target_include_directories(PinPoint PRIVATE ${SPINNAKER_INCLUDE_DIRS})
        target_link_libraries(PinPoint PRIVATE ${SPINNAKER_LIBRARIES})
        
        target_sources(PinPoint PRIVATE
            src/Video/VideoInputSpinnaker.h
            src/Video/VideoInputSpinnaker.cpp
        )

        # Copy Spinnaker DLLs to output directory for development
        file(GLOB _spinnaker_dlls "${SPINNAKER_ROOT}/bin64/vs2015/*.dll")
        foreach(_dll ${_spinnaker_dlls})
            add_custom_command(TARGET PinPoint POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:PinPoint>"
            )
        endforeach()
    else()
        message(STATUS "Spinnaker: not found (high-speed camera support disabled)")
    endif()
endif()

# ── OpenCV ───────────────────────────────────────────────────────────────────
# macOS: inject Homebrew prefix so CMake can locate OpenCVConfig.cmake
if(APPLE)
    execute_process(
        COMMAND brew --prefix opencv
        OUTPUT_VARIABLE _brew_opencv
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _brew_result
    )
    if(_brew_result EQUAL 0 AND EXISTS "${_brew_opencv}")
        list(APPEND CMAKE_PREFIX_PATH "${_brew_opencv}")
    endif()
endif()

# Windows: probe common install locations; user can override with -DOpenCV_DIR=
if(WIN32 AND NOT OpenCV_DIR)
    file(GLOB _opencv_candidates LIST_DIRECTORIES true "C:/opencv/build" "C:/tools/opencv/build")
    if(_opencv_candidates)
        list(GET _opencv_candidates 0 OpenCV_DIR)
    endif()
endif()

find_package(OpenCV QUIET)

if(OpenCV_FOUND)
    message(STATUS "OpenCV ${OpenCV_VERSION}: found")
    target_compile_definitions(PinPoint PRIVATE HAVE_OPENCV)
    target_include_directories(PinPoint PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(PinPoint PRIVATE ${OpenCV_LIBS})
    target_sources(PinPoint PRIVATE
        src/Video/video_preprocessor_opencv.h
        src/Video/video_preprocessor_opencv.cpp
    )
    if(EXISTS "${_movenet_model_path}")
        target_sources(PinPoint PRIVATE
            src/Pose/pose_estimator_movenet.h
            src/Pose/pose_estimator_movenet.cpp
        )
        message(STATUS "MoveNet pose estimator: enabled")
    endif()
else()
    message(STATUS "OpenCV: not found (image processing features disabled)")
endif()

if(APPLE)
    # The Bluetooth permission plugin is a static library that must be explicitly
    # imported; without this, checkPermission(QBluetoothPermission{}) always
    # returns Denied even when the OS has granted access.
    qt_import_plugins(PinPoint INCLUDE Qt6::QDarwinBluetoothPermissionPlugin)

    target_sources(PinPoint PRIVATE
        src/Gui/macos_permissions.h
        src/Gui/macos_permissions.mm
        src/STT/STTBackendApple.h
        src/STT/STTBackendApple.mm
        src/Video/VideoInputApple.h
        src/Video/VideoInputApple.mm
    )
    add_custom_command(TARGET PinPoint POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
            "$<TARGET_BUNDLE_CONTENT_DIR:PinPoint>/Info.plist"
        COMMENT "Installing Info.plist into bundle"
    )
    target_link_libraries(PinPoint PRIVATE
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Accelerate"
        "-framework Speech"
    )
endif()

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(PinPoint PRIVATE Threads::Threads)

endif()

if(MSVC)
    target_compile_definitions(PinPoint PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

install(TARGETS PinPoint
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
