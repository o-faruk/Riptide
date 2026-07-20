#pragma once

#include <optional>

#include "riptide/types.hpp"

namespace riptide {

// What a caller submits. The engine assigns `sequence` on acceptance —
// callers cannot influence FIFO priority by claiming an early timestamp.
struct NewOrderRequest {
  OrderId id;
  Side side;
  OrderType type;
  TimeInForce tif;
  std::optional<Price> price;  // nullopt iff type == Market
  Quantity quantity;
};

// A cancel/replace. Only price and/or quantity may change; side and type
// are immutable — a change to either is a new order in exchange semantics,
// not a replace. `quantity` is the new remaining quantity directly (not a
// delta), matching how real cancel/replace requests work.
struct ModifyRequest {
  OrderId id;
  std::optional<Price> price;  // nullopt = leave price unchanged
  std::optional<Quantity> quantity;  // nullopt = leave quantity unchanged
};

// The engine's internal representation of a live order. `remaining` is
// mutated in place as fills occur; `quantity` retains the original size
// for reference (e.g. computing filled amount).
struct Order {
  OrderId id;
  Side side;
  OrderType type;
  TimeInForce tif;
  std::optional<Price> price;
  Quantity quantity;
  Quantity remaining;
  Sequence sequence;
};

}  // namespace riptide
