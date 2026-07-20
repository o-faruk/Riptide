#include "stats.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>

namespace riptide::bench {

namespace {

std::uint64_t NearestRank(const std::vector<std::uint64_t>& sorted, double percentile) {
  const auto n = static_cast<double>(sorted.size());
  auto index = static_cast<std::size_t>(std::ceil(percentile / 100.0 * n));
  index = index == 0 ? 0 : index - 1;
  index = std::min(index, sorted.size() - 1);
  return sorted[index];
}

}  // namespace

Percentiles ComputePercentiles(std::vector<std::uint64_t> samples) {
  Percentiles result;
  result.count = samples.size();
  if (samples.empty()) return result;

  std::sort(samples.begin(), samples.end());
  result.p50 = NearestRank(samples, 50.0);
  result.p90 = NearestRank(samples, 90.0);
  result.p99 = NearestRank(samples, 99.0);
  result.p999 = NearestRank(samples, 99.9);
  result.p9999 = NearestRank(samples, 99.99);
  result.max = samples.back();
  return result;
}

void PrintHistogram(std::ostream& out, std::vector<std::uint64_t> samples, int num_buckets) {
  if (samples.empty() || num_buckets <= 0) return;

  std::sort(samples.begin(), samples.end());
  const std::uint64_t lo = samples.front();
  const std::size_t p99_index =
      std::min(samples.size() - 1, static_cast<std::size_t>(samples.size() * 0.99));
  const std::uint64_t hi = samples[p99_index];

  std::vector<std::size_t> buckets(static_cast<std::size_t>(num_buckets), 0);
  std::size_t tail_count = 0;
  const std::uint64_t span = (hi > lo) ? (hi - lo) : 1;

  for (std::uint64_t sample : samples) {
    if (sample > hi) {
      ++tail_count;
      continue;
    }
    std::size_t bucket = static_cast<std::size_t>((sample - lo) * static_cast<std::uint64_t>(num_buckets) / span);
    bucket = std::min(bucket, static_cast<std::size_t>(num_buckets - 1));
    ++buckets[bucket];
  }

  const std::size_t max_count = std::max(*std::max_element(buckets.begin(), buckets.end()), tail_count);
  constexpr int kBarWidth = 50;

  for (int i = 0; i < num_buckets; ++i) {
    const std::uint64_t bucket_lo = lo + (span * static_cast<std::uint64_t>(i)) / static_cast<std::uint64_t>(num_buckets);
    const std::uint64_t bucket_hi =
        lo + (span * static_cast<std::uint64_t>(i + 1)) / static_cast<std::uint64_t>(num_buckets);
    const int bar_len = max_count == 0
                             ? 0
                             : static_cast<int>(buckets[static_cast<std::size_t>(i)] * kBarWidth / max_count);
    out << std::setw(10) << bucket_lo << " - " << std::setw(10) << bucket_hi << " | "
        << std::string(static_cast<std::size_t>(bar_len), '#') << " (" << buckets[static_cast<std::size_t>(i)]
        << ")\n";
  }
  if (tail_count > 0) {
    const int bar_len = max_count == 0 ? 0 : static_cast<int>(tail_count * kBarWidth / max_count);
    out << std::setw(10) << ("> p99") << "              | " << std::string(static_cast<std::size_t>(bar_len), '#')
        << " (" << tail_count << ")\n";
  }
}

}  // namespace riptide::bench
