# Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.

cmake_minimum_required(VERSION 3.21)
project(pinpoint_event_buffer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Sanitizer options ---
option(PINPOINT_ENABLE_ASAN  "Enable AddressSanitizer"            OFF)
option(PINPOINT_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer"  OFF)
option(PINPOINT_ENABLE_TSAN  "Enable ThreadSanitizer"             OFF)

# ASAN and TSAN are mutually exclusive.
if(PINPOINT_ENABLE_ASAN AND PINPOINT_ENABLE_TSAN)
    message(FATAL_ERROR "ASAN and TSAN cannot be enabled simultaneously")
endif()

# Gather sanitizer flags into an interface target so both lib and tests inherit them.
add_library(pinpoint_sanitizers INTERFACE)

if(PINPOINT_ENABLE_ASAN OR PINPOINT_ENABLE_UBSAN OR PINPOINT_ENABLE_TSAN)
    if(MSVC)
        if(PINPOINT_ENABLE_ASAN)
            target_compile_options(pinpoint_sanitizers INTERFACE /fsanitize=address)
            target_link_options(pinpoint_sanitizers INTERFACE /fsanitize=address)
        endif()
    else()
        set(_san_flags "")
        if(PINPOINT_ENABLE_ASAN)
            string(APPEND _san_flags "-fsanitize=address,leak ")
        endif()
        if(PINPOINT_ENABLE_UBSAN)
            string(APPEND _san_flags "-fsanitize=undefined ")
        endif()
        if(PINPOINT_ENABLE_TSAN)
            string(APPEND _san_flags "-fsanitize=thread ")
        endif()
        string(APPEND _san_flags "-fno-omit-frame-pointer -g")
        # Separate into list for target_compile/link_options
        separate_arguments(_san_list NATIVE_COMMAND "${_san_flags}")
        target_compile_options(pinpoint_sanitizers INTERFACE ${_san_list})
        target_link_options(pinpoint_sanitizers INTERFACE ${_san_list})
    endif()
endif()

# --- Core library ---
add_library(pinpoint_event_buffer STATIC
    src/source_ring.cpp
)

target_include_directories(pinpoint_event_buffer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(pinpoint_event_buffer PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

target_link_libraries(pinpoint_event_buffer PUBLIC pinpoint_sanitizers)

# --- Tests ---
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(tests)
