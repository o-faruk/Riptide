#pragma once

#include <list>
#include <map>
#include <optional>
#include <unordered_map>

#include "riptide/matching_engine.hpp"
#include "riptide/order.hpp"
#include "riptide/pool_allocator.hpp"
#include "riptide/types.hpp"

namespace riptide {

// Identical to OrderBook's PriceLevel (see order_book.hpp) except
// `orders` draws its list nodes from a pooled allocator instead of the
// default allocator — see docs/OPTIMIZATION_LOG.md entry #1 for why.
// Separately named (not a template parameterizing OrderBook's own
// PriceLevel) because OrderBook is frozen: see
// include/riptide/matching_engine.hpp's ReferenceEngine comment.
struct PooledPriceLevel {
  std::list<Order, PoolAllocator<Order>> orders;
  Quantity total_quantity = 0;
};

// A byte-for-byte behavioral duplicate of OrderBook (same public
// interface, same semantics — verified by tests/differential_test.cpp
// diffing MatchingEngine<OrderBook> against MatchingEngine<PooledOrderBook>
// on randomized sequences) with exactly one change: PooledPriceLevel's
// FIFO queue nodes come from a pre-allocated slab via PoolAllocator
// instead of malloc/free per node. bids_/asks_ (std::map) and index_
// (std::unordered_map) are untouched — those are separate, later
// candidates (price-level lookup, order-ID map), not this one.
class PooledOrderBook {
 public:
  using BidLevels = std::map<Price, PooledPriceLevel, std::greater<Price>>;
  using AskLevels = std::map<Price, PooledPriceLevel>;

  bool insert(Order order);
  bool remove(OrderId id);

  Order* find(OrderId id);
  const Order* find(OrderId id) const;

  std::optional<Price> best_price(Side side) const;

  Order* front(Side side, Price price);

  std::optional<Quantity> level_quantity(Side side, Price price) const;

  void fill_front(Side side, Price price, Quantity fill_qty);

  bool set_remaining_in_place(OrderId id, Quantity new_remaining);

  const BidLevels& bid_levels() const { return bids_; }
  const AskLevels& ask_levels() const { return asks_; }

 private:
  struct OrderLocation {
    Side side;
    Price price;
    std::list<Order, PoolAllocator<Order>>::iterator it;
  };

  PooledPriceLevel* find_level(Side side, Price price);
  const PooledPriceLevel* find_level(Side side, Price price) const;
  void erase_level_if_empty(Side side, Price price);

  BidLevels bids_;
  AskLevels asks_;
  std::unordered_map<OrderId, OrderLocation> index_;
};

// Optimization #1 (docs/OPTIMIZATION_LOG.md): identical matching logic to
// ReferenceEngine, instantiated over PooledOrderBook instead of
// OrderBook. This is what tests/differential_test.cpp diffs against
// ReferenceEngine on randomized sequences.
using PooledMatchingEngine = MatchingEngine<PooledOrderBook>;

}  // namespace riptide
