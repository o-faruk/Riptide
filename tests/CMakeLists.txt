include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
# Match our runtime library settings so linking against gtest is consistent
# across build types (matters on Windows; harmless elsewhere).
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Phase 1 will add order_book_test.cpp, matching_engine_test.cpp, etc.
add_executable(riptide_tests
  test_version.cpp
  event_test.cpp
)

target_link_libraries(riptide_tests
  PRIVATE
    riptide_core
    riptide_warnings
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(riptide_tests)
