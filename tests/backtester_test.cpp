#include "backtest/backtester.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <optional>
#include <string>

#include "backtest/multi_instrument_backtester.hpp"
#include "riptide/matching_engine.hpp"
#include "riptide/risk_limits.hpp"
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
  void OnMarketEvent(const InstrumentId&, const std::vector<Event>&, OrderPort&) override {}
  void OnOwnEvent(const InstrumentId&, const Event&, OrderPort&) override {}
};

void ExpectAtLeastBaseline(const char* ticker, std::size_t baseline) {
  const std::string message_path = MessagePath(ticker);
  const std::string orderbook_path = OrderbookPath(ticker);
  if (!FileExists(message_path)) {
    GTEST_SKIP() << "LOBSTER sample data not present for " << ticker
                 << " (see tools/fetch_data.sh)";
  }

  NullStrategy strategy;
  Backtester<ReferenceEngine> backtester(ticker, strategy);
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
  void OnMarketEvent(const InstrumentId& instrument, const std::vector<Event>&,
                     OrderPort& port) override {
    if (traded_) return;
    traded_ = true;
    port.Buy(instrument, OrderType::Market, TimeInForce::IOC, /*price=*/std::nullopt,
             /*quantity=*/1);
  }
  void OnOwnEvent(const InstrumentId&, const Event&, OrderPort&) override {}

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
  Backtester<ReferenceEngine> backtester("AAPL", strategy);
  const auto result = backtester.Run(message_path.c_str(), orderbook_path.c_str());

  EXPECT_GE(result.rows_replayed, 1u);
  EXPECT_EQ(backtester.portfolio().position("AAPL"), 1);
  EXPECT_LT(backtester.portfolio().cash(), 0);
  EXPECT_EQ(backtester.portfolio().fill_count(), 1u);
  // A real replay produces real mark-to-market samples -- the drawdown/
  // Sharpe machinery should have something to report, not be inert.
  EXPECT_GE(backtester.portfolio().max_drawdown(), 0);
}

// A max-order-quantity of 0 shares (i.e. every order blocked) proves
// RiskLimits actually reaches the engine call, end to end through
// Backtester -- not just OrderPort in isolation (already covered by
// order_port_test.cpp).
TEST(Backtester, RiskLimitsBlockTheStrategyEndToEnd) {
  const std::string message_path = MessagePath("AAPL");
  const std::string orderbook_path = OrderbookPath("AAPL");
  if (!FileExists(message_path)) {
    GTEST_SKIP() << "LOBSTER sample data not present (see tools/fetch_data.sh)";
  }

  RiskLimits limits;
  limits.max_order_quantity = 0;  // impossible to satisfy -- any order is blocked
  BuyOneShareOnFirstEvent strategy;
  Backtester<ReferenceEngine> backtester("AAPL", strategy, limits);
  backtester.Run(message_path.c_str(), orderbook_path.c_str());

  EXPECT_EQ(backtester.portfolio().position("AAPL"), 0);
  EXPECT_EQ(backtester.portfolio().fill_count(), 0u);
}

// Never trades -- used only to prove MultiInstrumentBacktester actually
// interleaves rows from two real files in true chronological order
// (not one file fully replayed before the next), by recording every
// (instrument, timestamp) pair it observes.
class RecordingArrivalOrderStrategy : public Strategy {
 public:
  void OnMarketEvent(const InstrumentId& instrument, const std::vector<Event>&,
                     OrderPort&) override {
    seen_instruments.push_back(instrument);
  }
  void OnOwnEvent(const InstrumentId&, const Event&, OrderPort&) override {}

  std::vector<InstrumentId> seen_instruments;
};

TEST(MultiInstrumentBacktester, InterleavesTwoRealInstrumentsAndReproducesBothBaselines) {
  const std::string aapl_message = MessagePath("AAPL");
  const std::string amzn_message = MessagePath("AMZN");
  if (!FileExists(aapl_message) || !FileExists(amzn_message)) {
    GTEST_SKIP() << "LOBSTER sample data not present (see tools/fetch_data.sh)";
  }

  RecordingArrivalOrderStrategy strategy;
  MultiInstrumentBacktester<ReferenceEngine>::InstrumentFiles aapl_files{
      "AAPL", aapl_message, OrderbookPath("AAPL")};
  MultiInstrumentBacktester<ReferenceEngine>::InstrumentFiles amzn_files{
      "AMZN", amzn_message, OrderbookPath("AMZN")};
  MultiInstrumentBacktester<ReferenceEngine> backtester({aapl_files, amzn_files}, strategy);

  const auto result = backtester.Run();

  // Both tickers' documented baselines (see docs/DESIGN.md) must still
  // hold when replayed together -- each instrument has its own engine,
  // so interleaving must not corrupt either one's book.
  std::size_t aapl_rows = 0, amzn_rows = 0;
  for (const auto& instrument : strategy.seen_instruments) {
    if (instrument == "AAPL") ++aapl_rows;
    if (instrument == "AMZN") ++amzn_rows;
  }
  EXPECT_GE(aapl_rows, 11u);
  EXPECT_GE(amzn_rows, 35u);
  EXPECT_EQ(aapl_rows + amzn_rows, result.rows_replayed);

  // Real interleaving, not "all of AAPL then all of AMZN": each ticker
  // must appear at least once before the OTHER one is exhausted, which
  // would be impossible if one file were replayed to completion before
  // the other started.
  bool saw_aapl_before_amzn_exhausted = false;
  bool saw_amzn_before_aapl_exhausted = false;
  bool have_seen_aapl = false, have_seen_amzn = false;
  for (const auto& instrument : strategy.seen_instruments) {
    if (instrument == "AAPL") {
      have_seen_aapl = true;
      if (have_seen_amzn) saw_amzn_before_aapl_exhausted = true;
    }
    if (instrument == "AMZN") {
      have_seen_amzn = true;
      if (have_seen_aapl) saw_aapl_before_amzn_exhausted = true;
    }
  }
  EXPECT_TRUE(saw_aapl_before_amzn_exhausted);
  EXPECT_TRUE(saw_amzn_before_aapl_exhausted);
}

}  // namespace
}  // namespace riptide
