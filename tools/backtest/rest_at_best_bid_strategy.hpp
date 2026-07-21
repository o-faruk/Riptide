#pragma once

#include <vector>

#include "riptide/event.hpp"
#include "riptide/order_port.hpp"
#include "riptide/strategy.hpp"
#include "riptide/types.hpp"

namespace riptide {

// A minimal, honestly-labeled demo strategy: the first time it observes
// a market event on ANY instrument (i.e. right after that instrument's
// book has been seeded from pre-existing liquidity), it rests a single
// GTC limit buy of `quantity` shares at that instrument's current best
// bid, then does nothing else ever again -- no re-quoting, one
// instrument only even in a multi-instrument run, no risk limits, no
// inventory management. Its only job is to exercise Phase 5's machinery
// end to end with a real resting order that real subsequent market flow
// can actually fill, proving out OrderPort::BestPrice, Portfolio's fill
// attribution on the resting side, and Strategy::OnOwnEvent. Not a
// claim that resting at the touch and holding is a sound strategy.
class RestAtBestBidStrategy : public Strategy {
 public:
  explicit RestAtBestBidStrategy(Quantity quantity) : quantity_(quantity) {}

  void OnMarketEvent(const InstrumentId& instrument, const std::vector<Event>&,
                     OrderPort& port) override {
    if (rested_) return;
    const auto best_bid = port.BestPrice(instrument, Side::Buy);
    if (!best_bid.has_value()) return;  // no bid liquidity yet -- wait for one that has some

    rested_ = true;
    port.Buy(instrument, OrderType::Limit, TimeInForce::GTC, *best_bid, quantity_);
  }

  void OnOwnEvent(const InstrumentId&, const Event&, OrderPort&) override {}

 private:
  Quantity quantity_;
  bool rested_ = false;
};

}  // namespace riptide
