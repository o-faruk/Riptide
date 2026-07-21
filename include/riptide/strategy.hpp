#pragma once

#include <vector>

#include "riptide/event.hpp"
#include "riptide/types.hpp"

namespace riptide {

class OrderPort;  // see order_port.hpp — forward-declared to break the
                   // circular reference (OrderPort notifies Strategy,
                   // Strategy acts through OrderPort).

// User-supplied trading behavior participating in a Backtester<Engine>
// or MultiInstrumentBacktester<Engine> run. Virtual dispatch, not a
// template parameter like Book or Engine: unlike those (compile-time
// performance axes), a Strategy is behavior meant to be swapped freely
// — including several existing side by side, which docs/DESIGN.md's
// Phase 5 section expects by Phase 6.
class Strategy {
 public:
  virtual ~Strategy() = default;

  // Fired once per LOBSTER row applied to the book, before the strategy
  // reacts. `instrument` identifies which instrument this row belongs
  // to (a single-instrument Backtester always passes the same one).
  // `events` is whatever that row produced — almost always a single
  // Accepted/Cancelled/Modified, or one or more Trades if the row
  // happened to cross the strategy's own resting order. `port` is how
  // the strategy submits its own orders (on any instrument) in
  // response.
  virtual void OnMarketEvent(const InstrumentId& instrument, const std::vector<Event>& events,
                              OrderPort& port) = 0;

  // Fired once per event resulting from this strategy's own order
  // submissions on `instrument` (via `port`), including fills, rejects,
  // and cancels.
  virtual void OnOwnEvent(const InstrumentId& instrument, const Event& event, OrderPort& port) = 0;
};

}  // namespace riptide
