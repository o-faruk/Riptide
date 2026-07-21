#pragma once

#include <vector>

#include "riptide/event.hpp"
#include "riptide/order_port.hpp"
#include "riptide/strategy.hpp"
#include "riptide/types.hpp"

namespace riptide {

// A minimal, honestly-labeled demo strategy: the first time it observes
// a market event (i.e. right after the book has been seeded from
// pre-existing liquidity), it rests a single GTC limit buy of
// `quantity` shares at the current best bid, then does nothing else --
// no re-quoting, no risk limits, no inventory management. Its only job
// is to exercise Phase 5's machinery end to end with a real resting
// order that real subsequent market flow can actually fill, proving
// out OrderPort::BestPrice, Portfolio's fill attribution on the resting
// side, and Strategy::OnOwnEvent. Not a claim that resting at the touch
// and holding is a sound trading strategy.
class RestAtBestBidStrategy : public Strategy {
 public:
  explicit RestAtBestBidStrategy(Quantity quantity) : quantity_(quantity) {}

  void OnMarketEvent(const std::vector<Event>&, OrderPort& port) override {
    if (rested_) return;
    const auto best_bid = port.BestPrice(Side::Buy);
    if (!best_bid.has_value()) return;  // no bid liquidity yet -- wait for one that has some

    rested_ = true;
    port.Buy(OrderType::Limit, TimeInForce::GTC, *best_bid, quantity_);
  }

  void OnOwnEvent(const Event&, OrderPort&) override {}

 private:
  Quantity quantity_;
  bool rested_ = false;
};

}  // namespace riptide
