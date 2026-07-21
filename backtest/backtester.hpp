#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "lobster/adapter.hpp"
#include "lobster/lobster_book.hpp"
#include "lobster/lobster_message.hpp"
#include "riptide/order_port.hpp"
#include "riptide/portfolio.hpp"
#include "riptide/strategy.hpp"

namespace riptide {

// Drives a Strategy through a real LOBSTER file. `lobster::Adapter<Engine>`
// (Phase 2) supplies every OTHER market participant's order flow into a
// live engine; the strategy trades in that exact same engine through an
// OrderPort. See docs/DESIGN.md's Phase 5 section for why fills are real
// match results rather than approximated against the historical tape,
// and why that means the book can honestly diverge from LOBSTER's own
// orderbook file the moment the strategy's first fill happens — Phase
// 5 doesn't re-run Phase 2's validation gate, it uses LOBSTER as a
// source of realistic other-participant flow, not a target the strategy
// must reproduce.
template <typename Engine>
class Backtester {
 public:
  struct Result {
    std::size_t rows_replayed = 0;
    // Set iff the adapter reported a divergence (or the files ran out
    // of sync) before end of file — same failure mode tools/validate
    // surfaces, since this reuses that exact Adapter.
    std::optional<std::string> stopped_early_reason;
  };

  explicit Backtester(Strategy& strategy)
      : adapter_(engine_),
        strategy_(strategy),
        port_(
            [this](NewOrderRequest request) { return engine_.new_order(std::move(request)); },
            [this](OrderId id) { return engine_.cancel(id); },
            [this](ModifyRequest request) { return engine_.modify(std::move(request)); },
            [this]() { return adapter_.ReserveOrderId(); }, portfolio_, strategy_) {}

  Result Run(const char* message_path, const char* orderbook_path) {
    lobster::MessageReader messages(message_path);
    lobster::OrderBookFileReader orderbook(orderbook_path);

    lobster::Message message;
    lobster::BookRow row;

    // Row 1 / message 1 together represent pre-existing book state (see
    // Adapter's docs) — consumed to bootstrap, not treated as a row the
    // strategy observes.
    if (!orderbook.Next(row) || !messages.Next(message)) {
      return Result{.rows_replayed = 0,
                     .stopped_early_reason = "file(s) empty or too short to bootstrap"};
    }
    adapter_.SeedFromInitialBookState(row);

    Result result;
    std::vector<Event> events;
    while (messages.Next(message)) {
      events.clear();
      if (const auto error = adapter_.ApplyMessage(message, &events); error.has_value()) {
        result.stopped_early_reason = error;
        break;
      }
      // Feed the market row's events to Portfolio directly (not just via
      // OrderPort's own Notify): a market participant's order can trade
      // against the strategy's resting order, and that Trade is produced
      // right here, not through a strategy submission.
      for (const Event& event : events) portfolio_.ApplyEvent(event);
      strategy_.OnMarketEvent(events, port_);
      ++result.rows_replayed;
    }
    return result;
  }

  const Engine& engine() const { return engine_; }
  const Portfolio& portfolio() const { return portfolio_; }

 private:
  Engine engine_;
  lobster::Adapter<Engine> adapter_;
  Strategy& strategy_;
  Portfolio portfolio_;
  OrderPort port_;
};

}  // namespace riptide
