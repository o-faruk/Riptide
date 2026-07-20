#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

namespace riptide::bench {

// Nearest-rank percentiles, not interpolated: for percentile p (0-100),
// index = ceil(p/100 * n) - 1 into the sorted sample array, clamped to
// [0, n-1]. This is the standard convention for latency reporting (what
// most APM/benchmarking tools report) — simpler than interpolation and
// avoids introducing interpolation error exactly where it matters least
// to blur, the tail.
struct Percentiles {
  std::uint64_t p50 = 0;
  std::uint64_t p90 = 0;
  std::uint64_t p99 = 0;
  std::uint64_t p999 = 0;
  std::uint64_t p9999 = 0;
  std::uint64_t max = 0;
  std::size_t count = 0;
};

// Sorts a copy of `samples` and computes nearest-rank percentiles.
// Returns a zeroed Percentiles (count == 0) if `samples` is empty.
Percentiles ComputePercentiles(std::vector<std::uint64_t> samples);

// Prints a text histogram of `samples` to `out`. Buckets are linear across
// [min, p99] — the tail beyond p99 is already reported precisely by
// ComputePercentiles (p99/p99.9/p99.99/max), so the histogram's job is to
// show the bulk shape without a few extreme outliers flattening every
// other bucket into invisibility; anything beyond p99 is folded into one
// final "+tail" bucket instead of being dropped.
void PrintHistogram(std::ostream& out, std::vector<std::uint64_t> samples, int num_buckets = 20);

}  // namespace riptide::bench
