// Right now this is Reference-vs-itself: a broad, randomized extension
// of Phase 1's hand-written determinism test (edge_cases_test.cpp) —
// replaying the identical operation sequence through two fresh engines
// must produce byte-for-byte identical event streams. The moment Phase 4
// produces a second, optimized engine, this becomes a TRUE differential
// test: change RunSequence's engine type on one side to the optimized
// engine, keep the other as ReferenceEngine, same assertion. That's the
// whole point of building this now, before there's anything to diff
// against — the harness is proven correct on a case with a known answer
// (two identical engines must match) before it has to catch a real bug.

#include "random_sequence.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace riptide::fuzzing {
namespace {

std::vector<Event> RunSequence(const std::vector<Operation>& ops) {
  ReferenceEngine engine;
  std::vector<Event> all_events;
  for (const Operation& op : ops) {
    std::vector<Event> events = Apply(engine, op);
    all_events.insert(all_events.end(), events.begin(), events.end());
  }
  return all_events;
}

class DifferentialTest : public ::testing::TestWithParam<std::uint64_t> {};

TEST_P(DifferentialTest, TwoEnginesOnTheSameSequenceProduceIdenticalEvents) {
  const std::vector<Operation> ops = GenerateRandomSequence(GetParam(), 500);

  const std::vector<Event> first = RunSequence(ops);
  const std::vector<Event> second = RunSequence(ops);

  EXPECT_EQ(first, second);
}

// A spread of seeds, not just one — different seeds exercise different
// mixes of crossing depth, reject paths, and modify priority-loss cases.
INSTANTIATE_TEST_SUITE_P(ManySeeds, DifferentialTest,
                          ::testing::Values(1ULL, 2ULL, 3ULL, 42ULL, 1337ULL, 8675309ULL,
                                             999999937ULL, 0xC0FFEEULL));

}  // namespace
}  // namespace riptide::fuzzing
