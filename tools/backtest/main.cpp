#include <charconv>
#include <iostream>
#include <string>
#include <string_view>

#include "backtest/backtester.hpp"
#include "riptide/matching_engine.hpp"
#include "rest_at_best_bid_strategy.hpp"

namespace {

void PrintUsage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " <message.csv> <orderbook.csv> [--instrument NAME] [--quantity N]"
         " [--max-position N] [--max-order-quantity N]\n"
      << "\n"
      << "Runs docs/DESIGN.md's Phase 5 demo strategy (RestAtBestBidStrategy --\n"
      << "see tools/backtest/rest_at_best_bid_strategy.hpp) against a real LOBSTER\n"
      << "file: lobster::Adapter supplies every other participant's order flow into\n"
      << "a live MatchingEngine, and the demo strategy rests one real order in that\n"
      << "same engine. Prints the resulting fill count, P&L, drawdown, and per-\n"
      << "sample Sharpe -- all real match results/derived stats, not approximated\n"
      << "against the historical tape or fabricated.\n"
      << "\n"
      << "--max-position / --max-order-quantity: optional RiskLimits (see\n"
      << "include/riptide/risk_limits.hpp) enforced on the demo strategy's own\n"
      << "order submissions before they ever reach the engine.\n";
}

bool ParsePositiveLong(std::string_view digits, long& out) {
  return std::from_chars(digits.data(), digits.data() + digits.size(), out).ec == std::errc{} &&
         out > 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    PrintUsage(argv[0]);
    return 2;
  }

  riptide::Quantity quantity = 100;
  std::string instrument = "SYMBOL";
  riptide::RiskLimits limits;

  for (int i = 3; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--instrument" && i + 1 < argc) {
      instrument = argv[++i];
      continue;
    }
    if ((arg == "--quantity" || arg == "--max-position" || arg == "--max-order-quantity") &&
        i + 1 < argc) {
      long value = 0;
      if (!ParsePositiveLong(argv[++i], value)) {
        PrintUsage(argv[0]);
        return 2;
      }
      if (arg == "--quantity") {
        quantity = static_cast<riptide::Quantity>(value);
      } else if (arg == "--max-position") {
        limits.max_abs_position = value;
      } else {
        limits.max_order_quantity = static_cast<riptide::Quantity>(value);
      }
      continue;
    }
    PrintUsage(argv[0]);
    return 2;
  }

  riptide::RestAtBestBidStrategy strategy(quantity);
  riptide::Backtester<riptide::ReferenceEngine> backtester(instrument, strategy, limits);
  const auto result = backtester.Run(argv[1], argv[2]);

  std::cout << "Rows replayed: " << result.rows_replayed << "\n";
  if (result.stopped_early_reason.has_value()) {
    std::cout << "Stopped early: " << *result.stopped_early_reason << "\n";
  }

  const auto& portfolio = backtester.portfolio();
  std::cout << "Fills: " << portfolio.fill_count() << "\n"
            << "Position: " << portfolio.position(instrument) << "\n"
            << "Cash: " << portfolio.cash() << "\n"
            << "Max |position|: " << portfolio.max_abs_position(instrument) << "\n"
            << "Max drawdown: " << portfolio.max_drawdown() << "\n";

  if (const auto sharpe = portfolio.sharpe_ratio_per_sample(); sharpe.has_value()) {
    std::cout << "Per-sample Sharpe (NOT annualized -- see risk_limits.hpp/portfolio.hpp): "
              << *sharpe << "\n";
  } else {
    // Undefined for two different reasons Portfolio doesn't distinguish
    // externally: fewer than 2 recorded samples, or zero variance in
    // the recorded deltas (e.g. a strategy that never traded) -- both
    // are "n/a", neither is worth guessing at here.
    std::cout << "Per-sample Sharpe: n/a (undefined -- too few samples or zero variance)\n";
  }

  const auto& book = backtester.engine().book();
  const auto best_bid = book.best_price(riptide::Side::Buy);
  const auto best_ask = book.best_price(riptide::Side::Sell);
  if (best_bid.has_value() && best_ask.has_value()) {
    const riptide::Price mid = (*best_bid + *best_ask) / 2;
    std::cout << "Mark-to-market P&L (mid=" << mid
               << "): " << portfolio.mark_to_market(instrument, mid) << "\n";
  } else {
    std::cout << "Mark-to-market P&L: n/a (one side of the book is empty at end of run)\n";
  }

  return result.stopped_early_reason.has_value() ? 1 : 0;
}
