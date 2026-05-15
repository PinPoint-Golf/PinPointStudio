cmake_minimum_required(VERSION 3.21)
project(pinpoint_buffer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Headers and source live flat in this directory.
# Consumers add src/Buffer to their include path and use #include "source_ring.h" etc.
add_library(pinpoint_buffer
    source_ring.cpp
    timeline_index.cpp
    wait_flag.cpp
    event_buffer.cpp
    swing_window.cpp
    thread_policy.cpp
)

target_include_directories(pinpoint_buffer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(pinpoint_buffer PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# --- Sanitizer options ---
option(PINPOINT_ENABLE_ASAN  "Enable AddressSanitizer"           OFF)
option(PINPOINT_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(PINPOINT_ENABLE_TSAN  "Enable ThreadSanitizer"            OFF)

function(pinpoint_apply_sanitizers target)
    if(PINPOINT_ENABLE_ASAN)
        target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address)
    endif()
    if(PINPOINT_ENABLE_UBSAN)
        target_compile_options(${target} PRIVATE -fsanitize=undefined)
        target_link_options(${target} PRIVATE -fsanitize=undefined)
    endif()
    if(PINPOINT_ENABLE_TSAN)
        target_compile_options(${target} PRIVATE -fsanitize=thread)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

pinpoint_apply_sanitizers(pinpoint_buffer)

enable_testing()
add_subdirectory(tests)
