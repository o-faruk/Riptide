#pragma once

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "riptide/event.hpp"
#include "riptide/order.hpp"
#include "riptide/portfolio.hpp"
#include "riptide/strategy.hpp"
#include "riptide/types.hpp"

namespace riptide {

// The only way a Strategy submits orders. Not a template over Engine —
// unlike Backtester<Engine>, this is the user-facing boundary, and
// binding it to a concrete engine type would force every Strategy
// subclass to become a template too. Instead it holds type-erased
// callbacks (bound by Backtester<Engine> to its own engine_) so any
// MatchingEngine<Book> instantiation can sit behind the same concrete
// OrderPort/Strategy pair.
//
// Every submission is registered with Portfolio automatically — a
// Strategy can't forget to register an ID and silently lose a fill from
// its own P&L — and every resulting event is fed to both Portfolio and
// Strategy::OnOwnEvent, since submitting is the only place those events
// are ever produced.
class OrderPort {
 public:
  OrderPort(std::function<std::vector<Event>(NewOrderRequest)> new_order,
            std::function<std::vector<Event>(OrderId)> cancel,
            std::function<std::vector<Event>(ModifyRequest)> modify,
            std::function<OrderId()> reserve_id, Portfolio& portfolio, Strategy& strategy)
      : new_order_(std::move(new_order)),
        cancel_(std::move(cancel)),
        modify_(std::move(modify)),
        reserve_id_(std::move(reserve_id)),
        portfolio_(portfolio),
        strategy_(strategy) {}

  std::vector<Event> Buy(OrderType type, TimeInForce tif, std::optional<Price> price,
                          Quantity quantity) {
    return Submit(Side::Buy, type, tif, price, quantity);
  }

  std::vector<Event> Sell(OrderType type, TimeInForce tif, std::optional<Price> price,
                           Quantity quantity) {
    return Submit(Side::Sell, type, tif, price, quantity);
  }

  std::vector<Event> Cancel(OrderId id) {
    const auto events = cancel_(id);
    Notify(events);
    return events;
  }

  std::vector<Event> Modify(ModifyRequest request) {
    const auto events = modify_(request);
    Notify(events);
    return events;
  }

 private:
  std::vector<Event> Submit(Side side, OrderType type, TimeInForce tif,
                             std::optional<Price> price, Quantity quantity) {
    const OrderId id = reserve_id_();
    portfolio_.RegisterOrderId(id);
    const auto events = new_order_(NewOrderRequest{
        .id = id, .side = side, .type = type, .tif = tif, .price = price, .quantity = quantity});
    Notify(events);
    return events;
  }

  void Notify(const std::vector<Event>& events) {
    for (const Event& event : events) {
      portfolio_.ApplyEvent(event);
      strategy_.OnOwnEvent(event, *this);
    }
  }

  std::function<std::vector<Event>(NewOrderRequest)> new_order_;
  std::function<std::vector<Event>(OrderId)> cancel_;
  std::function<std::vector<Event>(ModifyRequest)> modify_;
  std::function<OrderId()> reserve_id_;
  Portfolio& portfolio_;
  Strategy& strategy_;
};

}  // namespace riptide
