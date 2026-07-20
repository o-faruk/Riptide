#include "adapter.hpp"

#include <variant>

namespace riptide::lobster {

namespace {

// LOBSTER's Direction field always describes the order named by that
// row's Order ID — for type 1 that's the new order itself; for type 4
// it's the RESTING order being executed against, never the incoming
// aggressor.
Side SideOf(int direction) { return direction == 1 ? Side::Buy : Side::Sell; }

// LOBSTER's dummy sentinels for an unoccupied price level (see
// tools/fetch_data.sh / the bundled ReadMe.txt).
constexpr Price kAskDummyPrice = 9999999999LL;
constexpr Price kBidDummyPrice = -9999999999LL;

bool AnyRejected(const std::vector<Event>& events) {
  for (const Event& event : events) {
    if (std::holds_alternative<Rejected>(event) || std::holds_alternative<CancelRejected>(event) ||
        std::holds_alternative<ModifyRejected>(event)) {
      return true;
    }
  }
  return false;
}

bool AnyCancelled(const std::vector<Event>& events) {
  for (const Event& event : events) {
    if (std::holds_alternative<Cancelled>(event)) return true;
  }
  return false;
}

bool AnyTrade(const std::vector<Event>& events) {
  for (const Event& event : events) {
    if (std::holds_alternative<Trade>(event)) return true;
  }
  return false;
}

}  // namespace

void Adapter::SeedFromInitialBookState(const BookRow& seed_row) {
  for (const BookLevel& level : seed_row.bids) {
    if (level.price == kBidDummyPrice || level.size == 0) continue;
    const OrderId id = NextSyntheticId();
    engine_.new_order(NewOrderRequest{.id = id,
                                       .side = Side::Buy,
                                       .type = OrderType::Limit,
                                       .tif = TimeInForce::GTC,
                                       .price = level.price,
                                       .quantity = level.size});
    pre_existing_buckets_[{Side::Buy, level.price}] = id;
  }
  for (const BookLevel& level : seed_row.asks) {
    if (level.price == kAskDummyPrice || level.size == 0) continue;
    const OrderId id = NextSyntheticId();
    engine_.new_order(NewOrderRequest{.id = id,
                                       .side = Side::Sell,
                                       .type = OrderType::Limit,
                                       .tif = TimeInForce::GTC,
                                       .price = level.price,
                                       .quantity = level.size});
    pre_existing_buckets_[{Side::Sell, level.price}] = id;
  }
}

std::optional<std::string> Adapter::Resolve(std::uint64_t message_order_id, Side side, Price price,
                                             Quantity fallback_size, const char* op_name,
                                             Resolved& out) {
  const Order* existing = engine_.book().find(message_order_id);
  OrderId resolved_id = message_order_id;

  if (existing == nullptr) {
    const auto bucket_it = pre_existing_buckets_.find({side, price});
    if (bucket_it != pre_existing_buckets_.end()) {
      existing = engine_.book().find(bucket_it->second);
      resolved_id = bucket_it->second;
    }
    if (existing == nullptr) {
      // Either no bucket has ever existed at (side, price), or the one
      // that did has since been fully consumed — a single price level
      // can hold multiple distinct pre-existing orders LOBSTER never
      // gave us type-1 rows for (e.g. tied to the opening auction), so
      // running out once doesn't mean there's nothing left to discover
      // there. Either way, materialize just enough to satisfy this
      // operation and (re)register the bucket.
      resolved_id = NextSyntheticId();
      engine_.new_order(NewOrderRequest{.id = resolved_id,
                                         .side = side,
                                         .type = OrderType::Limit,
                                         .tif = TimeInForce::GTC,
                                         .price = price,
                                         .quantity = fallback_size});
      pre_existing_buckets_[{side, price}] = resolved_id;
      existing = engine_.book().find(resolved_id);
    }
  }

  if (existing->side != side) {
    return std::string(op_name) + " direction does not match order " +
           std::to_string(message_order_id) + "'s side";
  }
  if (existing->price != price) {
    return std::string(op_name) + " price does not match order " + std::to_string(message_order_id) +
           "'s price";
  }

  out = Resolved{resolved_id, existing};
  return std::nullopt;
}

// A type-1 row is LOBSTER's report of what actually entered the book: per
// ITCH semantics, "Add Order" is only ever sent for the resting remainder
// of an order, after any immediate fills are already accounted for by
// separate execution messages against the resting orders it hit. So a
// type-1 row should NEVER be marketable against a correctly-mirrored
// book — if it crosses here, our book has already diverged from
// LOBSTER's (almost always traceable back to the seed/lazy-bucket
// approximating unknowable pre-existing liquidity slightly wrong), and
// surfacing that immediately beats letting it silently corrode the book
// further over every subsequent message.
std::optional<std::string> Adapter::ApplyNewOrder(const Message& message) {
  const auto events = engine_.new_order(NewOrderRequest{.id = message.order_id,
                                                          .side = SideOf(message.direction),
                                                          .type = OrderType::Limit,
                                                          .tif = TimeInForce::GTC,
                                                          .price = message.price,
                                                          .quantity = message.size});
  if (AnyRejected(events)) {
    return "new_order for order " + std::to_string(message.order_id) + " was rejected";
  }
  if (AnyTrade(events)) {
    return "new_order for order " + std::to_string(message.order_id) +
           " unexpectedly crossed the book — our book state has already diverged from LOBSTER's";
  }
  return std::nullopt;
}

// Type 2: partial cancellation. `size` is the number of shares removed
// (a delta), not the order's resulting size — this isn't stated on
// LOBSTER's docs page, only established by cross-referencing full order
// lifecycles in sample data (e.g. an order submitted with size 3,
// partially cancelled by 1, later fully deleted with size 2 — 3-1=2).
std::optional<std::string> Adapter::ApplyPartialCancellation(const Message& message) {
  Resolved resolved{};
  if (const auto error = Resolve(message.order_id, SideOf(message.direction), message.price,
                                  message.size, "partial cancellation", resolved);
      error.has_value()) {
    return error;
  }
  if (message.size > resolved.order->remaining) {
    return "partial cancellation removes more than order " + std::to_string(message.order_id) +
           "'s remaining quantity";
  }

  const Quantity new_remaining = resolved.order->remaining - message.size;
  if (new_remaining == 0) {
    // Only reachable when this is also the first sighting of the order
    // (fallback_size == message.size, so there's nothing left) — a
    // genuine partial cancellation on a previously-known order should
    // never zero it out (that's what type 3 is for). MatchingEngine's
    // modify() correctly rejects a modify-to-zero, so this goes through
    // cancel() instead.
    const auto events = engine_.cancel(resolved.id);
    if (AnyRejected(events)) {
      return "partial cancellation (as cancel, first sighting) for order " +
             std::to_string(message.order_id) + " was rejected";
    }
    return std::nullopt;
  }

  const auto events = engine_.modify(
      ModifyRequest{.id = resolved.id, .price = std::nullopt, .quantity = new_remaining});
  if (AnyRejected(events)) {
    return "partial cancellation (modify) for order " + std::to_string(message.order_id) +
           " was rejected";
  }
  return std::nullopt;
}

std::optional<std::string> Adapter::ApplyTotalDeletion(const Message& message) {
  Resolved resolved{};
  if (const auto error = Resolve(message.order_id, SideOf(message.direction), message.price,
                                  message.size, "total deletion", resolved);
      error.has_value()) {
    return error;
  }

  const auto events = engine_.cancel(resolved.id);
  if (AnyRejected(events)) {
    return "cancel for order " + std::to_string(message.order_id) + " was rejected";
  }
  return std::nullopt;
}

// Type 4: execution of a visible limit order. See adapter.hpp for why
// this has to synthesize an aggressor rather than treat the row as
// narration. The synthesized order is a Limit+IOC priced exactly at the
// resting order's price, sized exactly at the reported fill: since
// LOBSTER always reports executions in genuine price-time-priority order
// (the exchange enforces this) and every fill that had to happen first
// is already reflected in our book by the time we reach this row (rows
// are applied strictly in file order), this exactly-sized synthetic
// order can only ever match the named resting order — never anything
// else.
std::optional<std::string> Adapter::ApplyVisibleExecution(const Message& message) {
  Resolved resolved{};
  if (const auto error = Resolve(message.order_id, SideOf(message.direction), message.price,
                                  message.size, "execution", resolved);
      error.has_value()) {
    return error;
  }
  if (message.size > resolved.order->remaining) {
    return "execution size exceeds resting order " + std::to_string(message.order_id) +
           "'s remaining quantity";
  }

  const Side aggressor_side = (resolved.order->side == Side::Buy) ? Side::Sell : Side::Buy;
  const auto events = engine_.new_order(NewOrderRequest{.id = NextSyntheticId(),
                                                          .side = aggressor_side,
                                                          .type = OrderType::Limit,
                                                          .tif = TimeInForce::IOC,
                                                          .price = message.price,
                                                          .quantity = message.size});
  if (AnyRejected(events) || AnyCancelled(events)) {
    // A same-sized IOC that doesn't fill completely means our book didn't
    // actually have the liquidity this row claims — a real divergence
    // from LOBSTER, not a synthesis artifact.
    return "synthetic execution against order " + std::to_string(message.order_id) +
           " did not fill completely — book state has likely already diverged";
  }
  return std::nullopt;
}

std::optional<std::string> Adapter::ApplyMessage(const Message& message) {
  switch (message.event_type) {
    case 1:
      return ApplyNewOrder(message);
    case 2:
      return ApplyPartialCancellation(message);
    case 3:
      return ApplyTotalDeletion(message);
    case 4:
      return ApplyVisibleExecution(message);
    case 5:   // execution of a hidden order — never entered our book, no mutation
    case 6:   // cross/auction trade — no visible book mutation
    case 7:   // trading halt — no visible book mutation
      return std::nullopt;
    default:
      return "unrecognized LOBSTER event type " + std::to_string(message.event_type);
  }
}

}  // namespace riptide::lobster
