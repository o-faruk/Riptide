#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "riptide/matching_engine.hpp"

namespace riptide::fuzzing {

struct NewOp {
  NewOrderRequest request;
};
struct CancelOp {
  OrderId id;
};
struct ModifyOp {
  ModifyRequest request;
};

using Operation = std::variant<NewOp, CancelOp, ModifyOp>;

// Generates a deterministic (same `seed` -> same sequence) mix of
// new/cancel/modify operations against a single instrument: roughly 70%
// new orders, 20% cancels, 10% modifies. Cancels/modifies reference an
// order this same sequence actually submitted about 80% of the time
// (exercising the accept path) and a never-issued ID otherwise
// (exercising the reject path) — a generator that only ever produces
// valid references would miss an entire class of bugs.
//
// Prices are drawn from a small, fixed range (20 levels) on purpose:
// submitted orders collide and cross far more often than a wide
// realistic range would produce, which is exactly the behavior worth
// stress-testing (FIFO ordering within a level, multi-level sweeps,
// partial fills) — a wide range would mostly produce a sparse book where
// nothing interesting happens.
std::vector<Operation> GenerateRandomSequence(std::uint64_t seed, int count);

// Applies one Operation to `engine` and returns the resulting events —
// shared by the differential test and the fuzz harness so both drive an
// engine identically.
std::vector<Event> Apply(ReferenceEngine& engine, const Operation& operation);

}  // namespace riptide::fuzzing
