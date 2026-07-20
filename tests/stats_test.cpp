#include "stats.hpp"

#include <gtest/gtest.h>

#include <regex>
#include <sstream>
#include <vector>

namespace riptide::bench {
namespace {

std::vector<std::uint64_t> OneToN(std::uint64_t n) {
  std::vector<std::uint64_t> values;
  values.reserve(n);
  for (std::uint64_t v = 1; v <= n; ++v) values.push_back(v);
  return values;
}

TEST(Stats, PercentilesOfOneToOneHundred) {
  const Percentiles p = ComputePercentiles(OneToN(100));

  EXPECT_EQ(p.count, 100u);
  EXPECT_EQ(p.p50, 50u);
  EXPECT_EQ(p.p90, 90u);
  EXPECT_EQ(p.p99, 99u);
  EXPECT_EQ(p.p999, 100u);   // rounds up to the max with only 100 samples
  EXPECT_EQ(p.p9999, 100u);
  EXPECT_EQ(p.max, 100u);
}

TEST(Stats, EmptyInputReturnsZeroedResultWithZeroCount) {
  const Percentiles p = ComputePercentiles({});
  EXPECT_EQ(p.count, 0u);
  EXPECT_EQ(p.p50, 0u);
  EXPECT_EQ(p.max, 0u);
}

TEST(Stats, SingleElementIsEveryPercentile) {
  const Percentiles p = ComputePercentiles({42});
  EXPECT_EQ(p.count, 1u);
  EXPECT_EQ(p.p50, 42u);
  EXPECT_EQ(p.p99, 42u);
  EXPECT_EQ(p.max, 42u);
}

TEST(Stats, UnsortedInputIsSortedInternally) {
  const Percentiles p = ComputePercentiles({5, 1, 3, 2, 4});
  EXPECT_EQ(p.count, 5u);
  EXPECT_EQ(p.p50, 3u);  // median of {1,2,3,4,5}
  EXPECT_EQ(p.max, 5u);
}

TEST(Stats, HistogramBucketCountsSumToTotalSampleCount) {
  std::ostringstream out;
  PrintHistogram(out, OneToN(1000), /*num_buckets=*/10);

  // Don't over-specify exact bucket boundaries (an implementation detail);
  // just verify no samples were silently dropped or double-counted across
  // buckets, which is the failure mode that would actually matter.
  std::size_t total = 0;
  std::smatch match;
  const std::string text = out.str();
  auto begin = text.cbegin();
  const std::regex count_pattern(R"(\((\d+)\))");
  while (std::regex_search(begin, text.cend(), match, count_pattern)) {
    total += std::stoul(match[1].str());
    begin = match.suffix().first;
  }

  EXPECT_EQ(total, 1000u);
}

TEST(Stats, HistogramOnEmptyInputDoesNotCrash) {
  std::ostringstream out;
  PrintHistogram(out, {}, 10);
  EXPECT_TRUE(out.str().empty());
}

}  // namespace
}  // namespace riptide::bench
