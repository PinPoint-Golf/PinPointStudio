include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# source_ring_test
add_executable(source_ring_test source_ring_test.cpp)
target_link_libraries(source_ring_test PRIVATE pinpoint_buffer GTest::gtest_main)
pinpoint_apply_sanitizers(source_ring_test)
add_test(NAME source_ring_test COMMAND source_ring_test)

# adversarial_fuzz_test
add_executable(adversarial_fuzz_test adversarial_fuzz_test.cpp)
target_link_libraries(adversarial_fuzz_test PRIVATE pinpoint_buffer GTest::gtest_main)
pinpoint_apply_sanitizers(adversarial_fuzz_test)
add_test(NAME adversarial_fuzz_test COMMAND adversarial_fuzz_test)

# timeline_index_test
add_executable(timeline_index_test timeline_index_test.cpp)
target_link_libraries(timeline_index_test PRIVATE pinpoint_buffer GTest::gtest_main)
pinpoint_apply_sanitizers(timeline_index_test)
add_test(NAME timeline_index_test COMMAND timeline_index_test)

# wait_flag_test
add_executable(wait_flag_test wait_flag_test.cpp)
target_link_libraries(wait_flag_test PRIVATE pinpoint_buffer GTest::gtest_main)
pinpoint_apply_sanitizers(wait_flag_test)
add_test(NAME wait_flag_test COMMAND wait_flag_test)

# event_buffer_test
add_executable(event_buffer_test event_buffer_test.cpp)
target_link_libraries(event_buffer_test PRIVATE pinpoint_buffer GTest::gtest_main)
pinpoint_apply_sanitizers(event_buffer_test)
add_test(NAME event_buffer_test COMMAND event_buffer_test)

# swing_window_test
add_executable(swing_window_test swing_window_test.cpp)
target_link_libraries(swing_window_test PRIVATE pinpoint_buffer GTest::gtest_main)
pinpoint_apply_sanitizers(swing_window_test)
add_test(NAME swing_window_test COMMAND swing_window_test)
