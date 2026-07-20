#include "riptide/matching_engine.hpp"

#include <algorithm>
#include <cstdint>

namespace riptide {

std::optional<RejectReason> MatchingEngine::Validate(const NewOrderRequest& request) const {
  if (request.quantity == 0) return RejectReason::InvalidQuantity;

  if (request.type == OrderType::Limit) {
    if (!request.price.has_value() || *request.price <= 0) return RejectReason::InvalidPrice;
  } else {
    // Market: no limit price, and it can never rest, so GTC makes no sense.
    if (request.price.has_value()) return RejectReason::InvalidPrice;
    if (request.tif == TimeInForce::GTC) return RejectReason::InvalidTimeInForce;
  }

  if (book_.find(request.id) != nullptr) return RejectReason::DuplicateOrderId;

  return std::nullopt;
}

bool MatchingEngine::Crosses(Side side, Price limit_price, Price opposite_price) {
  return side == Side::Buy ? limit_price >= opposite_price : limit_price <= opposite_price;
}

bool MatchingEngine::HasSufficientLiquidity(Side side, std::optional<Price> price_limit,
                                             Quantity needed) const {
  const Side opposite = (side == Side::Buy) ? Side::Sell : Side::Buy;
  const std::uint64_t target = needed;
  std::uint64_t accumulated = 0;

  // Accumulate in a 64-bit total even though Quantity is 32-bit: summing
  // many levels' worth of resting size must not silently wrap around.
  auto scan = [&](const auto& levels) {
    for (const auto& [price, level] : levels) {
      if (price_limit.has_value() && !Crosses(side, *price_limit, price)) break;
      accumulated += level.total_quantity;
      if (accumulated >= target) return true;
    }
    return false;
  };

  return (opposite == Side::Buy) ? scan(book_.bid_levels()) : scan(book_.ask_levels());
}

void MatchingEngine::MatchAgainstBook(OrderId aggressor_id, Side side,
                                       std::optional<Price> price_limit, Quantity& remaining,
                                       std::vector<Event>& events) {
  const Side opposite = (side == Side::Buy) ? Side::Sell : Side::Buy;

  while (remaining > 0) {
    const std::optional<Price> best = book_.best_price(opposite);
    if (!best.has_value()) break;
    if (price_limit.has_value() && !Crosses(side, *price_limit, *best)) break;

    const Price level_price = *best;
    while (remaining > 0) {
      Order* resting = book_.front(opposite, level_price);
      if (resting == nullptr) break;  // level fully consumed

      const Quantity fill_qty = std::min(remaining, resting->remaining);
      const OrderId resting_id = resting->id;

      // Always at the resting (maker) order's price: the aggressor gets
      // price improvement, the passive side gets no worse than it quoted.
      events.push_back(Trade{.resting_id = resting_id,
                              .aggressor_id = aggressor_id,
                              .aggressor_side = side,
                              .price = level_price,
                              .quantity = fill_qty});

      book_.fill_front(opposite, level_price, fill_qty);
      remaining -= fill_qty;
    }
  }
}

std::vector<Event> MatchingEngine::new_order(NewOrderRequest request) {
  std::vector<Event> events;

  if (const auto reason = Validate(request); reason.has_value()) {
    events.push_back(Rejected{.id = request.id, .reason = *reason});
    return events;
  }

  // FOK must never partially execute, so the liquidity check has to
  // happen — and fail closed — before any trade is committed.
  if (request.tif == TimeInForce::FOK &&
      !HasSufficientLiquidity(request.side, request.price, request.quantity)) {
    events.push_back(Rejected{.id = request.id, .reason = RejectReason::FokUnfilled});
    return events;
  }

  const Sequence sequence = next_sequence_++;
  events.push_back(Accepted{.id = request.id, .sequence = sequence});

  Quantity remaining = request.quantity;
  MatchAgainstBook(request.id, request.side, request.price, remaining, events);

  if (remaining > 0) {
    if (request.tif == TimeInForce::GTC) {
      book_.insert(Order{.id = request.id,
                          .side = request.side,
                          .type = request.type,
                          .tif = request.tif,
                          .price = request.price,
                          .quantity = request.quantity,
                          .remaining = remaining,
                          .sequence = sequence});
    } else {
      // IOC remainder (or, in principle, an FOK remainder — the pre-check
      // above should make that unreachable, but the event still reports
      // reality rather than assuming the invariant holds).
      events.push_back(Cancelled{.id = request.id,
                                  .reason = CancelReason::IocRemainder,
                                  .remaining_cancelled = remaining});
    }
  }

  return events;
}

std::vector<Event> MatchingEngine::cancel(OrderId id) {
  std::vector<Event> events;

  const Order* existing = book_.find(id);
  if (existing == nullptr) {
    events.push_back(CancelRejected{.id = id, .reason = RejectReason::UnknownOrderId});
    return events;
  }

  const Quantity remaining = existing->remaining;
  book_.remove(id);
  events.push_back(
      Cancelled{.id = id, .reason = CancelReason::UserRequested, .remaining_cancelled = remaining});
  return events;
}

std::vector<Event> MatchingEngine::modify(ModifyRequest request) {
  std::vector<Event> events;

  const Order* existing = book_.find(request.id);
  if (existing == nullptr) {
    events.push_back(ModifyRejected{.id = request.id, .reason = RejectReason::UnknownOrderId});
    return events;
  }

  // Only Limit GTC orders ever rest, so existing->price is always set.
  const Price new_price = request.price.value_or(*existing->price);
  const Quantity new_quantity = request.quantity.value_or(existing->remaining);

  if (new_price <= 0) {
    events.push_back(ModifyRejected{.id = request.id, .reason = RejectReason::InvalidPrice});
    return events;
  }
  if (new_quantity == 0) {
    events.push_back(ModifyRejected{.id = request.id, .reason = RejectReason::InvalidQuantity});
    return events;
  }

  const bool price_changed = new_price != *existing->price;
  const bool quantity_increased = new_quantity > existing->remaining;
  const bool lost_priority = price_changed || quantity_increased;

  if (!lost_priority) {
    // Quantity decrease (or no change) at the same price can't newly
    // cross the book — a resting order never crosses by construction, and
    // shrinking it only makes that less true — so there's nothing to
    // match here, just an in-place size update that keeps FIFO position.
    const Sequence sequence = existing->sequence;
    book_.set_remaining_in_place(request.id, new_quantity);
    events.push_back(Modified{.id = request.id,
                               .new_price = new_price,
                               .new_quantity = new_quantity,
                               .lost_priority = false,
                               .sequence = sequence});
    return events;
  }

  // Price change or quantity increase: loses time priority. Implemented as
  // remove + re-submit at the back of the (possibly new) price level's
  // queue — which also means a replace that reprices into crossing
  // territory executes immediately, exactly like a new marketable limit
  // order would.
  const Side side = existing->side;
  const TimeInForce tif = existing->tif;
  book_.remove(request.id);

  const Sequence sequence = next_sequence_++;
  events.push_back(Modified{.id = request.id,
                             .new_price = new_price,
                             .new_quantity = new_quantity,
                             .lost_priority = true,
                             .sequence = sequence});

  Quantity remaining = new_quantity;
  MatchAgainstBook(request.id, side, new_price, remaining, events);

  if (remaining > 0) {
    book_.insert(Order{.id = request.id,
                        .side = side,
                        .type = OrderType::Limit,
                        .tif = tif,
                        .price = new_price,
                        .quantity = new_quantity,
                        .remaining = remaining,
                        .sequence = sequence});
  }

  return events;
}

}  // namespace riptide
