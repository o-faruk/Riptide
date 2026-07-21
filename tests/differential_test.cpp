// Reference-vs-Pooled: replaying the identical operation sequence
// through ReferenceEngine (Phase 1's unoptimized std::list<Order>) and
// PooledMatchingEngine (optimization #1 — docs/OPTIMIZATION_LOG.md, a
// pooled allocator for the same FIFO queue's nodes) must produce
// byte-for-byte identical event streams. This is what "an optimization
// isn't real until it matches the reference" means operationally: if
// this test ever fails, the corresponding optimization gets reverted,
// full stop, before anything else happens (see the loop in
// docs/OPTIMIZATION_LOG.md).
//
// As more optimized engines are added, extend RunSequence/the test
// cases below to cover each one against ReferenceEngine, rather than
// only ever comparing the two most recent — a regression in an earlier
// optimization wouldn't be caught by a test that stopped checking it.

#include "random_sequence.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "riptide/pooled_order_book.hpp"

namespace riptide::fuzzing {
namespace {

template <typename Engine>
std::vector<Event> RunSequence(const std::vector<Operation>& ops) {
  Engine engine;
  std::vector<Event> all_events;
  for (const Operation& op : ops) {
    std::vector<Event> events = Apply(engine, op);
    all_events.insert(all_events.end(), events.begin(), events.end());
  }
  return all_events;
}

class DifferentialTest : public ::testing::TestWithParam<std::uint64_t> {};

TEST_P(DifferentialTest, PooledMatchesReferenceOnTheSameSequence) {
  const std::vector<Operation> ops = GenerateRandomSequence(GetParam(), 500);

  const std::vector<Event> reference = RunSequence<ReferenceEngine>(ops);
  const std::vector<Event> pooled = RunSequence<PooledMatchingEngine>(ops);

  EXPECT_EQ(reference, pooled);
}

// A spread of seeds, not just one — different seeds exercise different
// mixes of crossing depth, reject paths, and modify priority-loss cases.
INSTANTIATE_TEST_SUITE_P(ManySeeds, DifferentialTest,
                          ::testing::Values(1ULL, 2ULL, 3ULL, 42ULL, 1337ULL, 8675309ULL,
                                             999999937ULL, 0xC0FFEEULL));

}  // namespace
}  // namespace riptide::fuzzing
