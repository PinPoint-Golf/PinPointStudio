# spinnaker_settings_probe — standalone hardware probe for the FLIR settings
# writer (src/Video/spinnaker_settings.cpp). Windows + Spinnaker SDK only; not
# part of the app build. docs/design/flir_camera_settings.md §7.
#
#   cmake -S tools/camera/spinnaker_settings_probe -B build/spinnaker_settings_probe ^
#         -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release ^
#         -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
#   cmake --build build/spinnaker_settings_probe
#
# Run with the Qt and Spinnaker bin directories on PATH, PPS closed.
cmake_minimum_required(VERSION 3.21)
project(spinnaker_settings_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

set(SPINNAKER_ROOT "C:/Program Files/Teledyne/Spinnaker" CACHE PATH "Spinnaker SDK root")
find_path(SPINNAKER_INCLUDE_DIRS NAMES Spinnaker.h HINTS ${SPINNAKER_ROOT}/include REQUIRED)
find_library(SPINNAKER_LIBRARIES NAMES Spinnaker_v140 HINTS ${SPINNAKER_ROOT}/lib64/vs2015 REQUIRED)

set(PPS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)

add_executable(spinnaker_settings_probe
    spinnaker_settings_probe.cpp
    ${PPS_SRC}/Video/spinnaker_settings.cpp)
target_compile_definitions(spinnaker_settings_probe PRIVATE HAVE_SPINNAKER)
target_include_directories(spinnaker_settings_probe PRIVATE ${PPS_SRC}/Video ${SPINNAKER_INCLUDE_DIRS})
target_link_libraries(spinnaker_settings_probe PRIVATE Qt6::Core Qt6::Gui ${SPINNAKER_LIBRARIES})
