#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <variant>

#include "riptide/event.hpp"
#include "riptide/types.hpp"

namespace riptide {

// Tracks a single strategy's own cash and position from Trade events —
// whether they came from the strategy's own order submissions or from
// some other market participant's order trading against the strategy's
// resting order. See docs/DESIGN.md's Phase 5 section for why both are
// possible: the strategy is a real participant in a live MatchingEngine,
// not a passive observer approximating fills against a historical tape.
class Portfolio {
 public:
  // Every ID the strategy's own orders are submitted under must be
  // registered here first (OrderPort does this automatically) —
  // otherwise ApplyEvent has no way to tell "this is one of my fills"
  // from "this is two other participants trading with each other".
  void RegisterOrderId(OrderId id) { owned_ids_.insert(id); }

  // Call once per event this engine produces, from any source (market
  // row or strategy submission) — events for order IDs this portfolio
  // doesn't own are ignored.
  void ApplyEvent(const Event& event) {
    const auto* trade = std::get_if<Trade>(&event);
    if (trade == nullptr) return;

    const bool own_resting = owned_ids_.contains(trade->resting_id);
    const bool own_aggressor = owned_ids_.contains(trade->aggressor_id);
    if (!own_resting && !own_aggressor) return;

    const auto qty = static_cast<std::int64_t>(trade->quantity);
    const auto notional = qty * trade->price;

    // Both being true would double-count a fill against ourselves — not
    // reachable here: new_order rejects a duplicate ID outright, and
    // every ID minted for the strategy (via OrderPort) is disjoint from
    // every other participant's, so a trade can own at most one side.
    if (own_aggressor) ApplySide(trade->aggressor_side, qty, notional);
    if (own_resting) {
      const Side resting_side = (trade->aggressor_side == Side::Buy) ? Side::Sell : Side::Buy;
      ApplySide(resting_side, qty, notional);
    }

    ++fill_count_;
    max_abs_position_ = std::max(max_abs_position_, static_cast<std::uint64_t>(std::abs(position_)));
  }

  std::int64_t position() const { return position_; }
  std::int64_t cash() const { return cash_; }
  std::uint64_t fill_count() const { return fill_count_; }
  std::uint64_t max_abs_position() const { return max_abs_position_; }

  // cash + position * mark_price — the caller supplies mark_price (e.g.
  // the book's current mid) since Portfolio has no book access of its
  // own. Same tick-scaled units as riptide::Price throughout.
  std::int64_t mark_to_market(Price mark_price) const { return cash_ + position_ * mark_price; }

 private:
  void ApplySide(Side side, std::int64_t qty, std::int64_t notional) {
    if (side == Side::Buy) {
      position_ += qty;
      cash_ -= notional;
    } else {
      position_ -= qty;
      cash_ += notional;
    }
  }

  std::unordered_set<OrderId> owned_ids_;
  std::int64_t position_ = 0;
  std::int64_t cash_ = 0;
  std::uint64_t fill_count_ = 0;
  std::uint64_t max_abs_position_ = 0;
};

}  // namespace riptide
