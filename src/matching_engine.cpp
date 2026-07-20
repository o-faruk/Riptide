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

}  // namespace riptide
