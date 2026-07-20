# riptide_bench_stats has zero external dependencies (just percentile/
# histogram computation over std::vector<uint64_t>) and is unit-tested by
# tests/, so it always builds -- unlike a Google-Benchmark-dependent
# target, it costs nothing to keep on by default.
add_library(riptide_bench_stats
  stats.cpp
)
target_include_directories(riptide_bench_stats
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(riptide_bench_stats
  PRIVATE riptide_warnings
)
