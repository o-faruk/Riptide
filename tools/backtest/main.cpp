#include <charconv>
#include <iostream>
#include <string_view>

#include "backtest/backtester.hpp"
#include "riptide/matching_engine.hpp"
#include "rest_at_best_bid_strategy.hpp"

namespace {

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " <message.csv> <orderbook.csv> [--quantity N]\n"
            << "\n"
            << "Runs docs/DESIGN.md's Phase 5 demo strategy (RestAtBestBidStrategy --\n"
            << "see tools/backtest/rest_at_best_bid_strategy.hpp) against a real LOBSTER\n"
            << "file: lobster::Adapter supplies every other participant's order flow into\n"
            << "a live MatchingEngine, and the demo strategy rests one real order in that\n"
            << "same engine. Prints the resulting fill count and P&L -- both real match\n"
            << "results, not approximated against the historical tape.\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    PrintUsage(argv[0]);
    return 2;
  }

  riptide::Quantity quantity = 100;
  for (int i = 3; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--quantity" && i + 1 < argc) {
      long value = 0;
      const std::string_view digits = argv[++i];
      if (std::from_chars(digits.data(), digits.data() + digits.size(), value).ec != std::errc{} ||
          value <= 0) {
        PrintUsage(argv[0]);
        return 2;
      }
      quantity = static_cast<riptide::Quantity>(value);
    } else {
      PrintUsage(argv[0]);
      return 2;
    }
  }

  riptide::RestAtBestBidStrategy strategy(quantity);
  riptide::Backtester<riptide::ReferenceEngine> backtester(strategy);
  const auto result = backtester.Run(argv[1], argv[2]);

  std::cout << "Rows replayed: " << result.rows_replayed << "\n";
  if (result.stopped_early_reason.has_value()) {
    std::cout << "Stopped early: " << *result.stopped_early_reason << "\n";
  }

  const auto& portfolio = backtester.portfolio();
  std::cout << "Fills: " << portfolio.fill_count() << "\n"
            << "Position: " << portfolio.position() << "\n"
            << "Cash: " << portfolio.cash() << "\n"
            << "Max |position|: " << portfolio.max_abs_position() << "\n";

  const auto& book = backtester.engine().book();
  const auto best_bid = book.best_price(riptide::Side::Buy);
  const auto best_ask = book.best_price(riptide::Side::Sell);
  if (best_bid.has_value() && best_ask.has_value()) {
    const riptide::Price mid = (*best_bid + *best_ask) / 2;
    std::cout << "Mark-to-market P&L (mid=" << mid << "): " << portfolio.mark_to_market(mid)
              << "\n";
  } else {
    std::cout << "Mark-to-market P&L: n/a (one side of the book is empty at end of run)\n";
  }

  return result.stopped_early_reason.has_value() ? 1 : 0;
}
