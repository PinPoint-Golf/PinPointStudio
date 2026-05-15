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
