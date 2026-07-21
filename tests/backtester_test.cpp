#include "backtest/backtester.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <optional>
#include <string>

#include "riptide/matching_engine.hpp"
#include "riptide/strategy.hpp"

#ifndef RIPTIDE_DATA_DIR
#define RIPTIDE_DATA_DIR "data"
#endif

namespace riptide {
namespace {

// Real LOBSTER sample data is fetched separately (tools/fetch_data.sh)
// and gitignored -- these tests skip rather than fail on a checkout that
// hasn't fetched it, same as tools/validate is a manual, not
// ctest-gated, correctness check.
bool FileExists(const std::string& path) { return std::ifstream(path).good(); }

std::string MessagePath(const char* ticker) {
  return std::string(RIPTIDE_DATA_DIR) + "/" + ticker + "/" + ticker +
         "_2012-06-21_34200000_57600000_message_10.csv";
}

std::string OrderbookPath(const char* ticker) {
  return std::string(RIPTIDE_DATA_DIR) + "/" + ticker + "/" + ticker +
         "_2012-06-21_34200000_57600000_orderbook_10.csv";
}

// Never trades -- used to check that routing LOBSTER rows through
// Backtester (via the same lobster::Adapter Phase 2 already validated)
// doesn't itself change how far the replay gets before the known,
// documented data-limitation divergence (see docs/DESIGN.md).
class NullStrategy : public Strategy {
 public:
  void OnMarketEvent(const std::vector<Event>&, OrderPort&) override {}
  void OnOwnEvent(const Event&, OrderPort&) override {}
};

void ExpectAtLeastBaseline(const char* ticker, std::size_t baseline) {
  const std::string message_path = MessagePath(ticker);
  const std::string orderbook_path = OrderbookPath(ticker);
  if (!FileExists(message_path)) {
    GTEST_SKIP() << "LOBSTER sample data not present for " << ticker
                 << " (see tools/fetch_data.sh)";
  }

  NullStrategy strategy;
  Backtester<ReferenceEngine> backtester(strategy);
  const auto result = backtester.Run(message_path.c_str(), orderbook_path.c_str());

  EXPECT_GE(result.rows_replayed, baseline)
      << "reason: " << result.stopped_early_reason.value_or("(none -- full file replayed)");
  // An inert strategy should never itself be the reason for a fill.
  EXPECT_EQ(backtester.portfolio().fill_count(), 0u);
}

TEST(Backtester, NullStrategyMatchesDocumentedAaplBaseline) { ExpectAtLeastBaseline("AAPL", 11); }
TEST(Backtester, NullStrategyMatchesDocumentedAmznBaseline) { ExpectAtLeastBaseline("AMZN", 35); }
TEST(Backtester, NullStrategyMatchesDocumentedMsftBaseline) { ExpectAtLeastBaseline("MSFT", 6); }

// Submits a single marketable IOC buy the first time it observes a
// market event -- by then SeedFromInitialBookState has already run, so
// real resting ask liquidity exists and this is guaranteed to fill
// exactly once, deterministically, regardless of which ticker's data is
// used. This is the test that exercises the actual point of Phase 5's
// design (docs/DESIGN.md): a strategy genuinely trading in the same
// live engine as the replayed market, not against an approximated tape.
class BuyOneShareOnFirstEvent : public Strategy {
 public:
  void OnMarketEvent(const std::vector<Event>&, OrderPort& port) override {
    if (traded_) return;
    traded_ = true;
    port.Buy(OrderType::Market, TimeInForce::IOC, /*price=*/std::nullopt, /*quantity=*/1);
  }
  void OnOwnEvent(const Event&, OrderPort&) override {}

 private:
  bool traded_ = false;
};

TEST(Backtester, StrategyReallyTradesAgainstReplayedMarketLiquidity) {
  const std::string message_path = MessagePath("AAPL");
  const std::string orderbook_path = OrderbookPath("AAPL");
  if (!FileExists(message_path)) {
    GTEST_SKIP() << "LOBSTER sample data not present (see tools/fetch_data.sh)";
  }

  BuyOneShareOnFirstEvent strategy;
  Backtester<ReferenceEngine> backtester(strategy);
  const auto result = backtester.Run(message_path.c_str(), orderbook_path.c_str());

  EXPECT_GE(result.rows_replayed, 1u);
  EXPECT_EQ(backtester.portfolio().position(), 1);
  EXPECT_LT(backtester.portfolio().cash(), 0);
  EXPECT_EQ(backtester.portfolio().fill_count(), 1u);
}

}  // namespace
}  // namespace riptide
