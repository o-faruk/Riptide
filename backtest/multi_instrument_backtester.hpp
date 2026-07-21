#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lobster/adapter.hpp"
#include "lobster/lobster_book.hpp"
#include "lobster/lobster_message.hpp"
#include "riptide/order_port.hpp"
#include "riptide/portfolio.hpp"
#include "riptide/risk_limits.hpp"
#include "riptide/strategy.hpp"

namespace riptide {

// Per-instrument state: its own engine, its own Adapter (LOBSTER order
// IDs are only unique WITHIN an instrument's file, so each instrument
// needs an independent engine/book), its own file readers, and the next
// unconsumed message peeked ahead for MultiInstrumentBacktester's
// timestamp-interleaved replay loop below. Held behind unique_ptr in a
// vector (not std::unordered_map<InstrumentId, InstrumentState<Engine>>)
// so Adapter<Engine>'s `Engine&` reference member is never at risk of
// being invalidated by a container rehash/move — each InstrumentState
// is constructed exactly once, in place, and never relocated.
template <typename Engine>
struct InstrumentState {
  InstrumentId id;
  Engine engine;
  lobster::Adapter<Engine> adapter;
  lobster::MessageReader messages;
  lobster::OrderBookFileReader orderbook;
  std::optional<lobster::Message> pending;  // next unconsumed message, if any

  InstrumentState(InstrumentId id_in, const std::string& message_path,
                  const std::string& orderbook_path)
      : id(std::move(id_in)), adapter(engine), messages(message_path), orderbook(orderbook_path) {}
};

// Drives a single Strategy through SEVERAL LOBSTER files at once, real
// order flow interleaved in true chronological order (by each row's
// timestamp) rather than one instrument's entire file replayed before
// the next starts — that ordering is what makes a multi-instrument
// backtest's cross-instrument decisions (e.g. "I'm long AAPL, so skip
// this MSFT signal") mean anything at all. Otherwise identical to
// Backtester<Engine>'s single-instrument model: each instrument gets
// its own live engine via its own Adapter<Engine>, and the strategy's
// own orders on that instrument trade in that same engine (see
// docs/DESIGN.md's Phase 5 section on real participation vs. tape
// approximation, which applies per instrument here).
template <typename Engine>
class MultiInstrumentBacktester {
 public:
  struct InstrumentFiles {
    InstrumentId instrument;
    std::string message_path;
    std::string orderbook_path;
  };

  struct Result {
    std::size_t rows_replayed = 0;
    // Prefixed with the instrument it happened on, since a divergence
    // is now necessarily specific to one of several files.
    std::optional<std::string> stopped_early_reason;
  };

  MultiInstrumentBacktester(std::vector<InstrumentFiles> files, Strategy& strategy,
                            RiskLimits limits = RiskLimits{})
      : strategy_(strategy),
        port_(
            [this](const InstrumentId& instrument, NewOrderRequest request) {
              return StateFor(instrument).engine.new_order(std::move(request));
            },
            [this](const InstrumentId& instrument, OrderId id) {
              return StateFor(instrument).engine.cancel(id);
            },
            [this](const InstrumentId& instrument, ModifyRequest request) {
              return StateFor(instrument).engine.modify(std::move(request));
            },
            [this](const InstrumentId& instrument) {
              return StateFor(instrument).adapter.ReserveOrderId();
            },
            [this](const InstrumentId& instrument, Side side) {
              return StateFor(instrument).engine.book().best_price(side);
            },
            portfolio_, strategy_, limits) {
    instruments_.reserve(files.size());
    for (auto& file : files) {
      index_of_[file.instrument] = instruments_.size();
      instruments_.push_back(std::make_unique<InstrumentState<Engine>>(
          file.instrument, file.message_path, file.orderbook_path));
    }
  }

  Result Run() {
    for (auto& state : instruments_) {
      lobster::BookRow seed_row;
      lobster::Message seed_message;
      // Row 1 / message 1 together represent pre-existing book state
      // (see Adapter's docs) — consumed to bootstrap this instrument,
      // not treated as a row the strategy observes.
      if (!state->orderbook.Next(seed_row) || !state->messages.Next(seed_message)) {
        return Result{.rows_replayed = 0,
                       .stopped_early_reason =
                           state->id + ": file(s) empty or too short to bootstrap"};
      }
      state->adapter.SeedFromInitialBookState(seed_row);
      Advance(*state);
    }

    Result result;
    std::vector<Event> events;
    while (true) {
      InstrumentState<Engine>* next = EarliestPending();
      if (next == nullptr) break;  // every instrument's file is exhausted

      const lobster::Message message = *next->pending;
      events.clear();
      if (const auto error = next->adapter.ApplyMessage(message, &events); error.has_value()) {
        result.stopped_early_reason = next->id + ": " + *error;
        break;
      }
      // Feed the market row's events to Portfolio directly (not just via
      // OrderPort's own Notify): a market participant's order can trade
      // against the strategy's resting order, and that Trade is produced
      // right here, not through a strategy submission.
      for (const Event& event : events) portfolio_.ApplyEvent(next->id, event);
      strategy_.OnMarketEvent(next->id, events, port_);
      RecordMarkToMarket();
      ++result.rows_replayed;

      Advance(*next);
    }
    return result;
  }

  const Portfolio& portfolio() const { return portfolio_; }
  const Engine& engine(const InstrumentId& instrument) const { return StateFor(instrument).engine; }

 private:
  InstrumentState<Engine>& StateFor(const InstrumentId& instrument) {
    return *instruments_[index_of_.at(instrument)];
  }
  const InstrumentState<Engine>& StateFor(const InstrumentId& instrument) const {
    return *instruments_[index_of_.at(instrument)];
  }

  // Whichever instrument's next pending message has the earliest
  // timestamp -- the actual mechanics of "true chronological order"
  // across files. nullptr once every instrument is exhausted.
  InstrumentState<Engine>* EarliestPending() {
    InstrumentState<Engine>* earliest = nullptr;
    for (auto& state : instruments_) {
      if (!state->pending.has_value()) continue;
      if (earliest == nullptr || state->pending->time < earliest->pending->time) {
        earliest = state.get();
      }
    }
    return earliest;
  }

  void Advance(InstrumentState<Engine>& state) {
    lobster::Message message;
    state.pending = state.messages.Next(message) ? std::optional(message) : std::nullopt;
  }

  void RecordMarkToMarket() {
    std::unordered_map<InstrumentId, Price> marks;
    for (const auto& state : instruments_) {
      const auto best_bid = state->engine.book().best_price(Side::Buy);
      const auto best_ask = state->engine.book().best_price(Side::Sell);
      if (best_bid.has_value() && best_ask.has_value()) {
        marks[state->id] = (*best_bid + *best_ask) / 2;
      }
    }
    portfolio_.RecordMarkToMarket(portfolio_.mark_to_market(marks));
  }

  std::vector<std::unique_ptr<InstrumentState<Engine>>> instruments_;
  std::unordered_map<InstrumentId, std::size_t> index_of_;
  Strategy& strategy_;
  Portfolio portfolio_;
  OrderPort port_;
};

}  // namespace riptide
