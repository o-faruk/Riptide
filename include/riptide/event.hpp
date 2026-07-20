#pragma once

#include <variant>

#include "riptide/types.hpp"

namespace riptide {

// Every event struct below defaults operator== (a defaulted comparison
// doesn't disqualify a struct from being an aggregate in C++20, so
// designated initializers still work everywhere). This is what makes
// std::vector<Event> comparable, which two things depend on: Phase 1's
// determinism requirement (replay the same input twice, assert identical
// output) and Phase 4's differential test (assert the optimized engine
// produces the same event stream as this reference engine).

// Order accepted validation and, if applicable, entered the book.
// Always the first event for an order that isn't rejected outright.
struct Accepted {
  OrderId id;
  Sequence sequence;

  bool operator==(const Accepted&) const = default;
};

// Shared across new_order/cancel/modify: it's the same underlying question
// each time ("why didn't this request go through"), and cancel/modify need
// the same InvalidQuantity/InvalidPrice checks new_order does, so one enum
// beats three overlapping ones.
enum class RejectReason {
  InvalidQuantity,     // quantity == 0
  InvalidPrice,        // Limit order with price <= 0, or Market order with a price set
  DuplicateOrderId,    // new_order: id already belongs to a live order
  InvalidTimeInForce,  // e.g. Market + GTC: a market order can never rest
  FokUnfilled,         // FOK could not be fully satisfied; no trades occurred
  UnknownOrderId,      // cancel/modify: id is not a live order
};

// Order never entered the book at all. No Accepted event precedes this.
struct Rejected {
  OrderId id;
  RejectReason reason;

  bool operator==(const Rejected&) const = default;
};

// One fill. Always printed at the resting (maker) order's price — the
// aggressor gets price improvement, never the passive side.
struct Trade {
  OrderId resting_id;
  OrderId aggressor_id;
  Side aggressor_side;
  Price price;
  Quantity quantity;

  bool operator==(const Trade&) const = default;
};

enum class CancelReason {
  UserRequested,  // explicit cancel request
  IocRemainder,   // IOC (or Market) unfilled remainder auto-cancelled
};

// Order fully removed from the book with `remaining_cancelled` unfilled.
struct Cancelled {
  OrderId id;
  CancelReason reason;
  Quantity remaining_cancelled;

  bool operator==(const Cancelled&) const = default;
};

// A successful cancel/replace. `lost_priority` tells consumers whether the
// order moved to the back of its (possibly new) price level's queue —
// true iff price changed or quantity increased, per the documented rule.
// new_price/new_quantity are always well-defined: only Limit GTC orders
// ever rest, so a live order being modified always has a real price.
struct Modified {
  OrderId id;
  Price new_price;
  Quantity new_quantity;
  bool lost_priority;
  Sequence sequence;  // unchanged if priority kept, newly assigned if lost

  bool operator==(const Modified&) const = default;
};

struct CancelRejected {
  OrderId id;
  RejectReason reason;  // always UnknownOrderId in practice

  bool operator==(const CancelRejected&) const = default;
};

struct ModifyRejected {
  OrderId id;
  RejectReason reason;  // UnknownOrderId, InvalidPrice, or InvalidQuantity

  bool operator==(const ModifyRejected&) const = default;
};

using Event = std::variant<Accepted, Rejected, Trade, Cancelled, Modified, CancelRejected,
                            ModifyRejected>;

}  // namespace riptide
