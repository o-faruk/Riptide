#pragma once

#include <list>
#include <map>
#include <optional>
#include <unordered_map>

#include "riptide/order.hpp"
#include "riptide/types.hpp"

namespace riptide {

// All orders resting at one price, oldest first. `total_quantity` is
// maintained incrementally by OrderBook rather than summed on demand —
// Phase 2's book validator and FOK's liquidity check both query aggregate
// level size, and recomputing it by walking the list every time would be
// needless O(n) work even for a "deliberately unoptimized" reference
// implementation.
struct PriceLevel {
  std::list<Order> orders;
  Quantity total_quantity = 0;
};

// Holds resting orders for one instrument's book. Pure mechanism: knows
// how to store, find, and remove orders and keep price levels consistent.
// It has no opinion about when two orders should trade — that policy
// belongs to MatchingEngine, which is the only thing that should ever
// call fill_front().
//
// Bids are ordered highest-first, asks lowest-first, so in both maps
// begin() is the best (most competitive) price — the two different
// comparators are the reason bid/ask levels can't share one map type.
class OrderBook {
 public:
  using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
  using AskLevels = std::map<Price, PriceLevel>;

  // False (no mutation) if order.id is already live in the book.
  // Precondition: order.price.has_value() — only orders that actually
  // rest (GTC limit orders, or a GTC limit's unfilled remainder) ever
  // reach the book; Market/IOC/FOK orders are fully resolved by the
  // engine before insert() would be called for any leftover quantity.
  bool insert(Order order);

  // False if id is not live. On success, removes the order from its
  // price level and erases the level itself if it's now empty.
  bool remove(OrderId id);

  // Mutable so the engine can adjust remaining quantity in place (used by
  // set_remaining_in_place and by the matching loop). nullptr if not live.
  Order* find(OrderId id);
  const Order* find(OrderId id) const;

  std::optional<Price> best_price(Side side) const;

  // Oldest order resting at (side, price), or nullptr if that level
  // doesn't exist.
  Order* front(Side side, Price price);

  // Aggregate resting quantity at (side, price), or nullopt if that level
  // doesn't exist.
  std::optional<Quantity> level_quantity(Side side, Price price) const;

  // Applies a fill of fill_qty to the front order at (side, price):
  // decrements its remaining quantity and the level's aggregate. If the
  // front order is now fully filled, removes it (and the level, if that
  // empties it). Precondition: front(side, price) is non-null and
  // fill_qty <= its remaining quantity.
  void fill_front(Side side, Price price, Quantity fill_qty);

  // Sets a live order's remaining quantity directly without changing its
  // position in the FIFO queue. Only valid for the "keeps time priority"
  // modify case (quantity decrease, price unchanged) — a price change or
  // quantity increase must go through remove() + insert() instead, since
  // those lose priority and belong at the back of the queue.
  bool set_remaining_in_place(OrderId id, Quantity new_remaining);

  const BidLevels& bid_levels() const { return bids_; }
  const AskLevels& ask_levels() const { return asks_; }

 private:
  struct OrderLocation {
    Side side;
    Price price;
    std::list<Order>::iterator it;
  };

  PriceLevel* find_level(Side side, Price price);
  const PriceLevel* find_level(Side side, Price price) const;
  void erase_level_if_empty(Side side, Price price);

  BidLevels bids_;
  AskLevels asks_;
  std::unordered_map<OrderId, OrderLocation> index_;
};

}  // namespace riptide
