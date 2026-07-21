#pragma once

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "riptide/event.hpp"
#include "riptide/order.hpp"
#include "riptide/portfolio.hpp"
#include "riptide/risk_limits.hpp"
#include "riptide/strategy.hpp"
#include "riptide/types.hpp"

namespace riptide {

// The only way a Strategy submits orders, on any instrument the backing
// Backtester/MultiInstrumentBacktester knows about. Not a template over
// Engine — unlike Backtester<Engine>, this is the user-facing boundary,
// and binding it to a concrete engine type would force every Strategy
// subclass to become a template too. Instead it holds type-erased,
// instrument-parameterized callbacks (bound by the backtester driver to
// the right engine for whatever InstrumentId it's given) so any
// MatchingEngine<Book> instantiation, single- or multi-instrument, sits
// behind the same concrete OrderPort/Strategy pair.
//
// Every submission is registered with Portfolio automatically — a
// Strategy can't forget to register an ID and silently lose a fill from
// its own P&L — and every resulting event is fed to both Portfolio and
// Strategy::OnOwnEvent, since submitting is the only place those events
// are ever produced. RiskLimits (optional, default = no limits) are
// enforced here too: a breach means the order is never submitted at
// all — engine, Portfolio, and Strategy::OnOwnEvent all see nothing.
class OrderPort {
 public:
  OrderPort(std::function<std::vector<Event>(const InstrumentId&, NewOrderRequest)> new_order,
            std::function<std::vector<Event>(const InstrumentId&, OrderId)> cancel,
            std::function<std::vector<Event>(const InstrumentId&, ModifyRequest)> modify,
            std::function<OrderId(const InstrumentId&)> reserve_id,
            std::function<std::optional<Price>(const InstrumentId&, Side)> best_price,
            Portfolio& portfolio, Strategy& strategy, RiskLimits limits = RiskLimits{})
      : new_order_(std::move(new_order)),
        cancel_(std::move(cancel)),
        modify_(std::move(modify)),
        reserve_id_(std::move(reserve_id)),
        best_price_(std::move(best_price)),
        portfolio_(portfolio),
        strategy_(strategy),
        limits_(limits) {}

  // Read-only market data: the current best price for `instrument` on
  // `side`, or nullopt if that side of the book is empty. Without this
  // a Strategy can only ever submit orders blind (e.g. one
  // unconditional market order) — any price-aware decision (quoting
  // around the touch, only trading when the spread is tight, etc.)
  // needs it.
  std::optional<Price> BestPrice(const InstrumentId& instrument, Side side) const {
    return best_price_(instrument, side);
  }

  std::vector<Event> Buy(const InstrumentId& instrument, OrderType type, TimeInForce tif,
                          std::optional<Price> price, Quantity quantity) {
    return Submit(instrument, Side::Buy, type, tif, price, quantity);
  }

  std::vector<Event> Sell(const InstrumentId& instrument, OrderType type, TimeInForce tif,
                           std::optional<Price> price, Quantity quantity) {
    return Submit(instrument, Side::Sell, type, tif, price, quantity);
  }

  std::vector<Event> Cancel(const InstrumentId& instrument, OrderId id) {
    const auto events = cancel_(instrument, id);
    Notify(instrument, events);
    return events;
  }

  std::vector<Event> Modify(const InstrumentId& instrument, ModifyRequest request) {
    const auto events = modify_(instrument, request);
    Notify(instrument, events);
    return events;
  }

  // Lets a Strategy check before attempting a submission, e.g. to try a
  // smaller size instead of just having Buy/Sell silently no-op.
  bool WouldBreachRiskLimits(const InstrumentId& instrument, Side side, Quantity quantity) const {
    return BreachesRiskLimits(instrument, side, quantity);
  }

  // Count of submissions blocked by RiskLimits so far — for reporting
  // (e.g. tools/backtest's CLI summary), not decision-making.
  std::uint64_t risk_block_count() const { return risk_block_count_; }

 private:
  std::vector<Event> Submit(const InstrumentId& instrument, Side side, OrderType type,
                             TimeInForce tif, std::optional<Price> price, Quantity quantity) {
    if (BreachesRiskLimits(instrument, side, quantity)) {
      ++risk_block_count_;
      return {};
    }

    const OrderId id = reserve_id_(instrument);
    portfolio_.RegisterOrderId(instrument, id);
    const auto events = new_order_(
        instrument, NewOrderRequest{
                        .id = id, .side = side, .type = type, .tif = tif, .price = price, .quantity = quantity});
    Notify(instrument, events);
    return events;
  }

  bool BreachesRiskLimits(const InstrumentId& instrument, Side side, Quantity quantity) const {
    if (limits_.max_order_quantity.has_value() && quantity > *limits_.max_order_quantity) {
      return true;
    }
    if (limits_.max_abs_position.has_value()) {
      const auto qty = static_cast<std::int64_t>(quantity);
      const std::int64_t projected = portfolio_.position(instrument) + (side == Side::Buy ? qty : -qty);
      if (std::abs(projected) > *limits_.max_abs_position) return true;
    }
    return false;
  }

  void Notify(const InstrumentId& instrument, const std::vector<Event>& events) {
    for (const Event& event : events) {
      portfolio_.ApplyEvent(instrument, event);
      strategy_.OnOwnEvent(instrument, event, *this);
    }
  }

  std::function<std::vector<Event>(const InstrumentId&, NewOrderRequest)> new_order_;
  std::function<std::vector<Event>(const InstrumentId&, OrderId)> cancel_;
  std::function<std::vector<Event>(const InstrumentId&, ModifyRequest)> modify_;
  std::function<OrderId(const InstrumentId&)> reserve_id_;
  std::function<std::optional<Price>(const InstrumentId&, Side)> best_price_;
  Portfolio& portfolio_;
  Strategy& strategy_;
  RiskLimits limits_;
  std::uint64_t risk_block_count_ = 0;
};

}  // namespace riptide
