#pragma once

#include <optional>
#include <vector>

#include "riptide/event.hpp"
#include "riptide/order.hpp"
#include "riptide/order_book.hpp"
#include "riptide/types.hpp"

namespace riptide {

// Single-instrument matching engine: applies order operations to an
// OrderBook and returns the resulting event stream. Deliberately
// unoptimized — std::map/std::list throughout, no allocator or intrusive
// structures. This is the Phase 1 reference implementation; Phase 4's
// optimized engines must match it event-for-event via differential
// testing, and Phase 2 validates it directly against LOBSTER.
class MatchingEngine {
 public:
  std::vector<Event> new_order(NewOrderRequest request);

  const OrderBook& book() const { return book_; }

 private:
  std::optional<RejectReason> Validate(const NewOrderRequest& request) const;

  static bool Crosses(Side side, Price limit_price, Price opposite_price);

  // Non-mutating check: does the opposite side hold at least `needed`
  // aggregate quantity at prices that cross `price_limit` (nullopt = no
  // limit, i.e. a Market order)? Walks price levels in the same order
  // MatchAgainstBook would, so it's exactly the liquidity that a real
  // match would consume — this is what makes FOK's all-or-nothing
  // guarantee correct rather than approximate.
  bool HasSufficientLiquidity(Side side, std::optional<Price> price_limit,
                               Quantity needed) const;

  // Matches an aggressor against the opposite side of the book: walks
  // price levels in price order, FIFO within each level, emitting a
  // Trade per fill (always priced at the resting/maker order) and
  // decrementing `remaining`. Stops when `remaining` hits 0 or the book
  // is no longer crossable at `price_limit` (or is empty).
  void MatchAgainstBook(OrderId aggressor_id, Side side, std::optional<Price> price_limit,
                        Quantity& remaining, std::vector<Event>& events);

  OrderBook book_;
  Sequence next_sequence_ = 1;
};

}  // namespace riptide
