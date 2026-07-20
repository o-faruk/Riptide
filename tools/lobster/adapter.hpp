#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "lobster/lobster_book.hpp"
#include "lobster/lobster_message.hpp"
#include "riptide/matching_engine.hpp"

namespace riptide::lobster {

// Translates LOBSTER's reconstructed-event-stream semantics into
// MatchingEngine operations. Two non-obvious things this has to handle
// (see adapter.cpp for the reasoning behind each):
//
//  1. An order that executes fully and immediately on arrival never gets
//     its own type-1 row in LOBSTER's file — only the resting orders it
//     hits do. Type-4 rows therefore have to drive a synthesized
//     aggressor order; they can't be treated as passive narration of
//     fills our engine would otherwise already know about.
//
//  2. LOBSTER's free sample files start at 09:30 (regular market open)
//     with a book that already has resting liquidity from before that
//     window — liquidity the message file gives no type-1 rows for at
//     all, since it only covers regular trading hours. There is no way
//     to recover the *individual* pre-existing orders from the free
//     sample (the orderbook file only gives aggregate size per level),
//     so SeedFromInitialBookState() bootstraps one synthetic "bucket"
//     order per pre-existing occupied price level, and later messages
//     that reference an order ID we never individually saw are
//     redirected to the appropriate bucket by (side, price) instead of
//     failing. This preserves aggregate book-state correctness, which is
//     exactly what top-of-book validation checks — it does not (and
//     cannot, from this data) preserve individual pre-existing orders'
//     original queue positions relative to each other.
class Adapter {
 public:
  explicit Adapter(MatchingEngine& engine) : engine_(engine) {}

  void SeedFromInitialBookState(const BookRow& seed_row);

  // Returns an error message on failure — e.g. an order ID that's
  // neither live nor covered by a pre-existing bucket, meaning either
  // this adapter's model of LOBSTER's semantics is wrong, or our book
  // has already diverged from LOBSTER's. Never something to silently
  // ignore.
  std::optional<std::string> ApplyMessage(const Message& message);

 private:
  struct Resolved {
    OrderId id;
    const Order* order;
  };

  // Resolves `message_order_id` to a live order: first by direct lookup,
  // then the pre-existing bucket at (side, price), and — if neither
  // exists — by lazily materializing a fresh bucket of `fallback_size`
  // shares at (side, price). That last case handles pre-existing orders
  // LOBSTER's sample never gave us a type-1 row for *and* that weren't
  // priced within row 1's top-N seed (e.g. an order tied to the opening
  // auction that only becomes live sometime after row 1, at a price
  // better than anything row 1 showed). `fallback_size` should be
  // whatever quantity the current operation needs — since the caller
  // immediately consumes/removes what it resolves, a first-sighting
  // never leaves a lingering phantom balance: net contribution to
  // permanent book state is always zero. Cross-checks the resolved
  // order's side/price against what the message claims either way, for
  // early, precise diagnostics.
  std::optional<std::string> Resolve(std::uint64_t message_order_id, Side side, Price price,
                                      Quantity fallback_size, const char* op_name, Resolved& out);

  OrderId NextSyntheticId() { return next_synthetic_id_--; }

  std::optional<std::string> ApplyNewOrder(const Message& message);
  std::optional<std::string> ApplyPartialCancellation(const Message& message);
  std::optional<std::string> ApplyTotalDeletion(const Message& message);
  std::optional<std::string> ApplyVisibleExecution(const Message& message);

  MatchingEngine& engine_;
  std::map<std::pair<Side, Price>, OrderId> pre_existing_buckets_;
  OrderId next_synthetic_id_ = std::numeric_limits<OrderId>::max();
};

}  // namespace riptide::lobster
