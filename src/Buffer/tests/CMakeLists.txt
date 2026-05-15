# Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.

add_executable(source_ring_test
    source_ring_test.cpp
)
target_link_libraries(source_ring_test PRIVATE
    pinpoint_event_buffer
    pinpoint_sanitizers
    GTest::gtest_main
)
target_compile_options(source_ring_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(adversarial_fuzz_test
    adversarial_fuzz_test.cpp
)
target_link_libraries(adversarial_fuzz_test PRIVATE
    pinpoint_event_buffer
    pinpoint_sanitizers
    GTest::gtest_main
)
target_compile_options(adversarial_fuzz_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

include(GoogleTest)
gtest_discover_tests(source_ring_test)
gtest_discover_tests(adversarial_fuzz_test)
