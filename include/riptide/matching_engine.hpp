#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <optional>
#include <vector>

#include "riptide/event.hpp"
#include "riptide/order.hpp"
#include "riptide/order_book.hpp"
#include "riptide/types.hpp"

namespace riptide {

// The contract any book implementation must satisfy to back a
// MatchingEngine<Book> — deliberately mirrors OrderBook's own public
// interface exactly, since that's the only implementation that exists so
// far. This exists for two reasons: better compile errors when a future
// Book type is missing something, and as a single place documenting
// exactly what MatchingEngine depends on, so a new Book implementation
// knows precisely what it needs to provide.
template <typename Book>
concept OrderBookLike = requires(Book& book, const Book& const_book, Order order, OrderId id,
                                  Side side, Price price, Quantity qty) {
  { book.insert(order) } -> std::same_as<bool>;
  { book.remove(id) } -> std::same_as<bool>;
  { book.find(id) } -> std::same_as<Order*>;
  { const_book.find(id) } -> std::same_as<const Order*>;
  { const_book.best_price(side) } -> std::same_as<std::optional<Price>>;
  { book.front(side, price) } -> std::same_as<Order*>;
  { const_book.level_quantity(side, price) } -> std::same_as<std::optional<Quantity>>;
  { book.fill_front(side, price, qty) } -> std::same_as<void>;
  { book.set_remaining_in_place(id, qty) } -> std::same_as<bool>;
  const_book.bid_levels();
  const_book.ask_levels();
};

// Single-instrument matching engine: applies order operations to a Book
// and returns the resulting event stream. The matching *logic* here
// (crossing, validation, FIFO walk) is what Phase 1 built and Phase 2
// validated against LOBSTER — it doesn't change for a data-structure
// optimization, only the Book underneath it does. That's the whole
// reason this is a template: ReferenceEngine (see below) and every
// Phase 4 optimized engine share this exact code, instantiated over a
// different Book, rather than each optimization re-deriving (and
// risking re-breaking) the same ~150 lines of matching semantics.
template <typename Book>
  requires OrderBookLike<Book>
class MatchingEngine {
 public:
  std::vector<Event> new_order(NewOrderRequest request) {
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
        // IOC remainder (or, in principle, an FOK remainder — the
        // pre-check above should make that unreachable, but the event
        // still reports reality rather than assuming the invariant
        // holds).
        events.push_back(Cancelled{.id = request.id,
                                    .reason = CancelReason::IocRemainder,
                                    .remaining_cancelled = remaining});
      }
    }

    return events;
  }

  std::vector<Event> cancel(OrderId id) {
    std::vector<Event> events;

    const Order* existing = book_.find(id);
    if (existing == nullptr) {
      events.push_back(CancelRejected{.id = id, .reason = RejectReason::UnknownOrderId});
      return events;
    }

    const Quantity remaining = existing->remaining;
    book_.remove(id);
    events.push_back(Cancelled{
        .id = id, .reason = CancelReason::UserRequested, .remaining_cancelled = remaining});
    return events;
  }

  std::vector<Event> modify(ModifyRequest request) {
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
      // cross the book — a resting order never crosses by construction,
      // and shrinking it only makes that less true — so there's nothing
      // to match here, just an in-place size update that keeps FIFO
      // position.
      const Sequence sequence = existing->sequence;
      book_.set_remaining_in_place(request.id, new_quantity);
      events.push_back(Modified{.id = request.id,
                                 .new_price = new_price,
                                 .new_quantity = new_quantity,
                                 .lost_priority = false,
                                 .sequence = sequence});
      return events;
    }

    // Price change or quantity increase: loses time priority. Implemented
    // as remove + re-submit at the back of the (possibly new) price
    // level's queue — which also means a replace that reprices into
    // crossing territory executes immediately, exactly like a new
    // marketable limit order would.
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

  const Book& book() const { return book_; }

 private:
  std::optional<RejectReason> Validate(const NewOrderRequest& request) const {
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

  static bool Crosses(Side side, Price limit_price, Price opposite_price) {
    return side == Side::Buy ? limit_price >= opposite_price : limit_price <= opposite_price;
  }

  // Non-mutating check: does the opposite side hold at least `needed`
  // aggregate quantity at prices that cross `price_limit` (nullopt = no
  // limit, i.e. a Market order)? Walks price levels in the same order
  // MatchAgainstBook would, so it's exactly the liquidity that a real
  // match would consume — this is what makes FOK's all-or-nothing
  // guarantee correct rather than approximate.
  bool HasSufficientLiquidity(Side side, std::optional<Price> price_limit, Quantity needed) const {
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

  // Matches an aggressor against the opposite side of the book: walks
  // price levels in price order, FIFO within each level, emitting a
  // Trade per fill (always priced at the resting/maker order) and
  // decrementing `remaining`. Stops when `remaining` hits 0 or the book
  // is no longer crossable at `price_limit` (or is empty).
  void MatchAgainstBook(OrderId aggressor_id, Side side, std::optional<Price> price_limit,
                        Quantity& remaining, std::vector<Event>& events) {
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

  Book book_;
  Sequence next_sequence_ = 1;
};

// From Phase 4 onward, this is what optimized engines get diffed against
// (see tests/differential_test.cpp and docs/OPTIMIZATION_LOG.md).
// OrderBook itself is frozen as of this point: bug fixes only, never an
// optimization — the moment its behavior can change, "diff against the
// reference" stops meaning anything. Any Phase 4 structural change (order
// pool, intrusive lists, flat price-level array, etc.) belongs in a new,
// separate Book type, instantiated as its own MatchingEngine<NewBook>.
using ReferenceEngine = MatchingEngine<OrderBook>;

}  // namespace riptide
