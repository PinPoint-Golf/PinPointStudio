# The capture test rig's target (PP_CAPTURE_RIG; included from the end of the root CMakeLists.txt).
#
# capture_rig_test is the app minus its main(): the app's C++ and Objective-C++ sources under src/, compiled with the
# app's include paths, definitions and options and linked with its libraries, plus the rig's own test file. What the
# app gets from its QML module (the type registration, the QML cache, the resources) is left out: the rig loads no QML.
# Read from the app target here, after every target_* call on it, so a source or a library added to the app reaches
# the rig with no edit of this file.

set(_rig_src_root "${CMAKE_CURRENT_SOURCE_DIR}/src/")
get_target_property(_rig_app_sources PinPointStudio SOURCES)
set(_rig_sources "")
foreach(_s IN LISTS _rig_app_sources)
    if(_s MATCHES "\\$<")
        continue()                                   # platform-only resources (the icon, the .rc)
    endif()
    cmake_path(ABSOLUTE_PATH _s BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE _abs)
    if(NOT _abs MATCHES "^${_rig_src_root}" OR _abs MATCHES "/src/Gui/main\\.cpp$")
        continue()                                   # generated files, and the app's main()
    endif()
    if(_abs MATCHES "\\.(cpp|cc|mm|h|hpp)$")
        list(APPEND _rig_sources "${_abs}")
    endif()
endforeach()

add_executable(capture_rig_test EXCLUDE_FROM_ALL
    src/Gui/tests/capture_rig/capture_rig_test.cpp
    ${_rig_sources})

foreach(_prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS LINK_DIRECTORIES)
    get_target_property(_v PinPointStudio ${_prop})
    if(_v)
        set_property(TARGET capture_rig_test PROPERTY ${_prop} "${_v}")
    endif()
endforeach()
get_target_property(_rig_app_links PinPointStudio LINK_LIBRARIES)
set(_rig_links "")
foreach(_l IN LISTS _rig_app_links)
    if(_l MATCHES "^::@" OR (TARGET "${_l}" AND _l MATCHES "^PinPointStudio"))
        continue()                                   # directory markers, and the app's own QML module parts
    endif()
    list(APPEND _rig_links "${_l}")
endforeach()
target_link_libraries(capture_rig_test PRIVATE ${_rig_links})
set_target_properties(capture_rig_test PROPERTIES AUTOMOC ON AUTORCC OFF AUTOUIC OFF)
# The rig finds the app's dynamic libraries (ONNX Runtime, the frameworks) where the app's own build does.
get_target_property(_rig_rpath PinPointStudio BUILD_RPATH)
if(_rig_rpath)
    set_target_properties(capture_rig_test PROPERTIES BUILD_RPATH "${_rig_rpath}")
endif()

enable_testing()   # the app tree registers no other test; the rig is run through ctest like every suite
add_test(NAME capture_rig COMMAND capture_rig_test)
set_tests_properties(capture_rig PROPERTIES TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
list(LENGTH _rig_sources _rig_n)
message(STATUS "Capture test rig (PP_CAPTURE_RIG): capture_rig_test, ${_rig_n} of the app's sources")
