# riptide_bench_stats has zero external dependencies (just percentile/
# histogram computation over std::vector<uint64_t>) and is unit-tested by
# tests/, so it always builds -- unlike the Google-Benchmark-dependent
# target below, it costs nothing to keep on by default.
add_library(riptide_bench_stats
  stats.cpp
)
target_include_directories(riptide_bench_stats
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(riptide_bench_stats
  PRIVATE riptide_warnings
)

# The custom end-to-end replay harness -- Phase 3's primary deliverable.
# No heavy fetched deps, so this always builds too.
add_executable(riptide_bench_replay
  replay_harness.cpp
)
target_link_libraries(riptide_bench_replay
  PRIVATE
    riptide_lobster
    riptide_bench_stats
    riptide_warnings
)

# Google Benchmark microbenchmarks: gated behind RIPTIDE_BUILD_BENCH so a
# plain `cmake --build` doesn't force a Google Benchmark fetch on anyone
# just checking correctness.
if(RIPTIDE_BUILD_BENCH)
  include(FetchContent)

  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
  )
  FetchContent_MakeAvailable(googlebenchmark)

  add_executable(riptide_bench_micro
    micro_benchmarks.cpp
  )
  target_link_libraries(riptide_bench_micro
    PRIVATE
      riptide_core
      riptide_warnings
      benchmark::benchmark_main
  )
endif()
