# PinPointOpenCV.cmake — build the pinned OpenCV once, and point find_package at it.
#
# Included by the app's top-level CMakeLists.txt and by tests/cmake/PinPointTests.cmake,
# so the app and every test suite link the same OpenCV.
#
# Why not Homebrew: its opencv@4 is deprecated and disabled on 2027-07-09, its plain
# opencv is 5.x, and a Homebrew upgrade of one of its many dependencies can stop the app
# at launch. That happened on 3 Nov 2026, when OpenEXR moved on and opencv@4 did not.
# It also ships OpenBLAS and a second libomp, neither of which the app needs.
#
# What is built: only the four modules the code includes, as static libraries, with only
# PNG and JPEG for image files. PinPoint needs no LAPACK, TBB, OpenCL, OpenEXR, TIFF or WebP.
#
# ⚠ FFmpeg is required, and its absence must be an error, not a smaller build. Without it
# videoio still builds, cv::CAP_FFMPEG then fails at run time, and re-analysing a swing
# from its MP4 stops working with nothing at build time to say so.
#
# macOS only for now. Elsewhere this does nothing and the platform's existing OpenCV is
# used. -DPP_OPENCV_NO_PIN=ON opts out and honours -DOpenCV_DIR=.
include_guard(GLOBAL)

option(PP_OPENCV_NO_PIN "Do not build the pinned OpenCV; honour OpenCV_DIR instead" OFF)
if(NOT APPLE OR PP_OPENCV_NO_PIN)
    return()
endif()

set(_pp_ocv_version 4.14.0)
set(_pp_ocv_sha256  ee8fb9b30eb60850431b4656447080e3737b56e45719c92b67f245950609f86e)
set(PP_OPENCV_JOBS 6 CACHE STRING "Parallel jobs for the one-off OpenCV build")

get_filename_component(_pp_ocv_repo "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PP_OPENCV_ROOT "${_pp_ocv_repo}/build/_opencv" CACHE PATH
    "Where the pinned OpenCV is built; shared by every build tree")

# FFmpeg comes from Homebrew and is linked by the app as well. OpenCV records FFmpeg's
# versioned Cellar folder in its CMake config, so that folder name is part of the key
# below: a Homebrew rebuild can change it without changing FFmpeg's own version.
find_program(_pp_ocv_brew brew HINTS /opt/homebrew/bin /usr/local/bin)
find_program(_pp_ocv_pkgconfig NAMES pkg-config pkgconf HINTS /opt/homebrew/bin /usr/local/bin)
set(_pp_ocv_ffmpeg "")
if(_pp_ocv_brew)
    execute_process(COMMAND "${_pp_ocv_brew}" --prefix ffmpeg
        OUTPUT_VARIABLE _pp_ocv_ffmpeg OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT EXISTS "${_pp_ocv_ffmpeg}/lib/pkgconfig/libavcodec.pc" OR NOT _pp_ocv_pkgconfig)
    message(FATAL_ERROR "OpenCV: needs FFmpeg and pkg-config. Run: brew install ffmpeg pkgconf")
endif()
get_filename_component(_pp_ocv_ffmpeg_real "${_pp_ocv_ffmpeg}" REALPATH)
get_filename_component(_pp_ocv_ffmpeg_ver "${_pp_ocv_ffmpeg_real}" NAME)

if(CMAKE_OSX_ARCHITECTURES)
    set(_pp_ocv_arch "${CMAKE_OSX_ARCHITECTURES}")
else()
    set(_pp_ocv_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()
if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(_pp_ocv_min "${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
    set(_pp_ocv_min "default")
endif()

# Bump _pp_ocv_recipe when the options below change, so existing builds are replaced.
set(_pp_ocv_recipe 1)
set(_pp_ocv_key "${_pp_ocv_version}-r${_pp_ocv_recipe}-${_pp_ocv_arch}-${_pp_ocv_min}-ffmpeg${_pp_ocv_ffmpeg_ver}")
set(_pp_ocv_dir     "${PP_OPENCV_ROOT}/${_pp_ocv_key}")
set(_pp_ocv_install "${_pp_ocv_dir}/install")
set(_pp_ocv_stamp   "${_pp_ocv_dir}/complete.stamp")

# Two configures at once (Qt Creator and a terminal) must not build it twice.
file(MAKE_DIRECTORY "${PP_OPENCV_ROOT}")
file(LOCK "${PP_OPENCV_ROOT}/.lock" GUARD FILE TIMEOUT 1800)

if(NOT EXISTS "${_pp_ocv_stamp}")
    message(STATUS "OpenCV: building ${_pp_ocv_version} once for ${_pp_ocv_arch}; "
                   "this takes a few minutes and is shared by every build tree")
    file(REMOVE_RECURSE "${_pp_ocv_dir}")
    file(MAKE_DIRECTORY "${_pp_ocv_dir}/src")

    set(_pp_ocv_tar "${PP_OPENCV_ROOT}/opencv-${_pp_ocv_version}.tar.gz")
    file(DOWNLOAD "https://github.com/opencv/opencv/archive/refs/tags/${_pp_ocv_version}.tar.gz"
        "${_pp_ocv_tar}" EXPECTED_HASH SHA256=${_pp_ocv_sha256} STATUS _pp_ocv_dl)
    list(GET _pp_ocv_dl 0 _pp_ocv_dl_rc)
    if(NOT _pp_ocv_dl_rc EQUAL 0)
        message(FATAL_ERROR "OpenCV: download failed: ${_pp_ocv_dl}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_pp_ocv_tar}"
        WORKING_DIRECTORY "${_pp_ocv_dir}/src" RESULT_VARIABLE _pp_ocv_rc)
    if(NOT _pp_ocv_rc EQUAL 0)
        message(FATAL_ERROR "OpenCV: could not unpack ${_pp_ocv_tar}")
    endif()

    set(_pp_ocv_args
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_INSTALL_PREFIX=${_pp_ocv_install}"
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        -DBUILD_LIST=core,imgproc,imgcodecs,videoio
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF
        -DBUILD_opencv_apps=OFF -DBUILD_JAVA=OFF -DBUILD_opencv_python3=OFF
        -DOPENCV_GENERATE_PKGCONFIG=OFF -DENABLE_PRECOMPILED_HEADERS=OFF
        # Image files: PNG and JPEG, from OpenCV's own bundled copies.
        -DBUILD_ZLIB=ON -DBUILD_PNG=ON -DBUILD_JPEG=ON
        -DWITH_TIFF=OFF -DWITH_WEBP=OFF -DWITH_OPENEXR=OFF -DWITH_OPENJPEG=OFF -DWITH_JASPER=OFF
        -DWITH_AVIF=OFF -DWITH_JPEGXL=OFF -DWITH_SPNG=OFF -DWITH_GDAL=OFF -DWITH_GDCM=OFF
        -DWITH_IMGCODEC_HDR=OFF -DWITH_IMGCODEC_SUNRASTER=OFF
        -DWITH_IMGCODEC_PXM=OFF -DWITH_IMGCODEC_PFM=OFF
        # Maths and threads: OpenCV's own code, and Grand Central Dispatch on macOS.
        -DWITH_LAPACK=OFF -DWITH_EIGEN=OFF -DWITH_OPENCL=OFF -DWITH_TBB=OFF -DWITH_IPP=OFF
        -DWITH_ITT=OFF -DWITH_PROTOBUF=OFF -DWITH_ADE=OFF -DWITH_QUIRC=OFF -DWITH_OBSENSOR=OFF
        # Video: FFmpeg linked in, nothing else, no run-time plugins.
        -DWITH_FFMPEG=ON -DVIDEOIO_ENABLE_PLUGINS=OFF -DWITH_AVFOUNDATION=OFF
        -DWITH_GSTREAMER=OFF -DWITH_1394=OFF -DWITH_V4L=OFF
        "-DPKG_CONFIG_EXECUTABLE=${_pp_ocv_pkgconfig}")
    if(CMAKE_MAKE_PROGRAM)
        list(APPEND _pp_ocv_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
        list(APPEND _pp_ocv_args "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND _pp_ocv_args "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    if(CMAKE_OSX_SYSROOT)
        list(APPEND _pp_ocv_args "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
    endif()

    set(_pp_ocv_env "PKG_CONFIG_PATH=${_pp_ocv_ffmpeg}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "${_pp_ocv_env}"
                "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}"
                -S "${_pp_ocv_dir}/src/opencv-${_pp_ocv_version}" -B "${_pp_ocv_dir}/build"
                ${_pp_ocv_args}
        OUTPUT_FILE "${_pp_ocv_dir}/configure.log" ERROR_FILE "${_pp_ocv_dir}/configure.log"
        RESULT_VARIABLE _pp_ocv_rc)
    if(NOT _pp_ocv_rc EQUAL 0)
        message(FATAL_ERROR "OpenCV: configure failed, see ${_pp_ocv_dir}/configure.log")
    endif()

    # See the FFmpeg warning at the top of this file.
    file(STRINGS "${_pp_ocv_dir}/configure.log" _pp_ocv_ff REGEX "FFMPEG: +YES")
    if(NOT _pp_ocv_ff)
        message(FATAL_ERROR "OpenCV: FFmpeg was not picked up, see ${_pp_ocv_dir}/configure.log")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${_pp_ocv_dir}/build" --parallel ${PP_OPENCV_JOBS}
        OUTPUT_FILE "${_pp_ocv_dir}/build.log" ERROR_FILE "${_pp_ocv_dir}/build.log"
        RESULT_VARIABLE _pp_ocv_rc)
    if(NOT _pp_ocv_rc EQUAL 0)
        message(FATAL_ERROR "OpenCV: build failed, see ${_pp_ocv_dir}/build.log")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --install "${_pp_ocv_dir}/build"
        OUTPUT_QUIET RESULT_VARIABLE _pp_ocv_rc)
    if(NOT _pp_ocv_rc EQUAL 0)
        message(FATAL_ERROR "OpenCV: install failed")
    endif()

    # The sources and objects are only needed once.
    file(REMOVE_RECURSE "${_pp_ocv_dir}/src" "${_pp_ocv_dir}/build")
    file(WRITE "${_pp_ocv_stamp}" "${_pp_ocv_key}\n")
endif()

file(LOCK "${PP_OPENCV_ROOT}/.lock" RELEASE)

# FORCE on purpose: a tree configured before this file carries a Homebrew OpenCV_DIR in
# its cache, and that is exactly the state that has to repair itself.
set(OpenCV_DIR "${_pp_ocv_install}/lib/cmake/opencv4" CACHE PATH
    "Pinned OpenCV, built by cmake/PinPointOpenCV.cmake" FORCE)
message(STATUS "OpenCV: using pinned build ${_pp_ocv_key}")
