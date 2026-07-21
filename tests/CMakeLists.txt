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

add_executable(riptide_tests
  test_version.cpp
  event_test.cpp
  order_book_test.cpp
  matching_engine_test.cpp
  edge_cases_test.cpp
  stats_test.cpp
  differential_test.cpp
  fixed_size_pool_test.cpp
  pooled_order_book_test.cpp
)

target_link_libraries(riptide_tests
  PRIVATE
    riptide_bench_stats
    riptide_fuzz_support  # transitively brings in riptide_core (PUBLIC link)
    riptide_warnings
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(riptide_tests)
