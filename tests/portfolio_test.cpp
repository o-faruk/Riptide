#include "riptide/portfolio.hpp"

#include <gtest/gtest.h>

#include <unordered_map>

namespace riptide {
namespace {

constexpr const char* kAapl = "AAPL";
constexpr const char* kMsft = "MSFT";

TEST(Portfolio, IgnoresTradeBetweenTwoUnownedIds) {
  Portfolio portfolio;
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 100,
                                     .quantity = 5});

  EXPECT_EQ(portfolio.position(kAapl), 0);
  EXPECT_EQ(portfolio.cash(), 0);
  EXPECT_EQ(portfolio.fill_count(), 0u);
}

TEST(Portfolio, AggressorBuyIncreasesPositionAndSpendsCash) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(kAapl, /*aggressor_id=*/2);
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 100,
                                     .quantity = 5});

  EXPECT_EQ(portfolio.position(kAapl), 5);
  EXPECT_EQ(portfolio.cash(), -500);
  EXPECT_EQ(portfolio.fill_count(), 1u);
}

TEST(Portfolio, AggressorSellDecreasesPositionAndReceivesCash) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(kAapl, /*aggressor_id=*/2);
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Sell,
                                     .price = 100,
                                     .quantity = 5});

  EXPECT_EQ(portfolio.position(kAapl), -5);
  EXPECT_EQ(portfolio.cash(), 500);
}

// A resting order takes the OPPOSITE side of whatever hit it -- an
// aggressor Buy filled our resting Sell, so we sold, not bought.
TEST(Portfolio, RestingSideIsOppositeOfAggressorSide) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(kAapl, /*resting_id=*/1);
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 100,
                                     .quantity = 5});

  EXPECT_EQ(portfolio.position(kAapl), -5);
  EXPECT_EQ(portfolio.cash(), 500);
}

TEST(Portfolio, AccumulatesAcrossMultipleFills) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(kAapl, 2);
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 100,
                                     .quantity = 5});
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 3,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 110,
                                     .quantity = 3});

  EXPECT_EQ(portfolio.position(kAapl), 8);
  EXPECT_EQ(portfolio.cash(), -500 - 330);
  EXPECT_EQ(portfolio.fill_count(), 2u);
}

TEST(Portfolio, MarkToMarketCombinesCashAndPositionAtGivenPrice) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(kAapl, 2);
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 100,
                                     .quantity = 5});

  // Bought 5 @ 100 (cash -500), marking at 120: 5*120 - 500 = 100.
  EXPECT_EQ(portfolio.mark_to_market(kAapl, 120), 100);
}

TEST(Portfolio, TracksMaxAbsolutePositionAcrossSignFlips) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(kAapl, 2);
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 100,
                                     .quantity = 5});
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 3,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Sell,
                                     .price = 100,
                                     .quantity = 20});

  // Position path: 0 -> +5 -> -15. Max absolute value seen is 15, not 5.
  EXPECT_EQ(portfolio.position(kAapl), -15);
  EXPECT_EQ(portfolio.max_abs_position(kAapl), 15u);
}

// Order IDs are only unique WITHIN an instrument (each has its own
// engine/book) -- the same numeric ID on two different instruments must
// be attributed independently, not conflated.
TEST(Portfolio, TracksPositionsIndependentlyPerInstrument) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(kAapl, /*aggressor_id=*/2);
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 100,
                                     .quantity = 5});
  // Same numeric ID (2), but never registered on kMsft -- must be
  // ignored there, not attributed as if it were the same order.
  portfolio.ApplyEvent(kMsft, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 200,
                                     .quantity = 9});

  EXPECT_EQ(portfolio.position(kAapl), 5);
  EXPECT_EQ(portfolio.position(kMsft), 0);
  EXPECT_EQ(portfolio.cash(), -500);  // only the AAPL fill affected cash
  EXPECT_EQ(portfolio.fill_count(), 1u);
}

TEST(Portfolio, MarkToMarketSumsAcrossInstrumentsPlusSharedCash) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(kAapl, 2);
  portfolio.RegisterOrderId(kMsft, 3);
  portfolio.ApplyEvent(kAapl, Trade{.resting_id = 1,
                                     .aggressor_id = 2,
                                     .aggressor_side = Side::Buy,
                                     .price = 100,
                                     .quantity = 5});  // cash -500, +5 AAPL
  portfolio.ApplyEvent(kMsft, Trade{.resting_id = 1,
                                     .aggressor_id = 3,
                                     .aggressor_side = Side::Sell,
                                     .price = 50,
                                     .quantity = 4});  // cash +200, -4 MSFT

  // cash = -500 + 200 = -300; +5 AAPL @ 110 = 550; -4 MSFT @ 60 = -240.
  const std::unordered_map<InstrumentId, Price> marks{{kAapl, 110}, {kMsft, 60}};
  EXPECT_EQ(portfolio.mark_to_market(marks), -300 + 550 - 240);
}

TEST(Portfolio, MaxDrawdownTracksWorstPeakToTroughDecline) {
  Portfolio portfolio;
  portfolio.RecordMarkToMarket(0);
  portfolio.RecordMarkToMarket(100);  // peak 100
  portfolio.RecordMarkToMarket(40);   // drawdown so far: 60
  portfolio.RecordMarkToMarket(70);   // recovers some, drawdown unchanged
  portfolio.RecordMarkToMarket(120);  // new peak 120
  portfolio.RecordMarkToMarket(90);   // drawdown from new peak: 30, still < 60

  EXPECT_EQ(portfolio.max_drawdown(), 60);
}

TEST(Portfolio, SharpeRatioPerSampleIsNulloptWithFewerThanTwoDeltas) {
  Portfolio portfolio;
  EXPECT_FALSE(portfolio.sharpe_ratio_per_sample().has_value());
  portfolio.RecordMarkToMarket(0);
  EXPECT_FALSE(portfolio.sharpe_ratio_per_sample().has_value());  // only one sample -> zero deltas
}

TEST(Portfolio, SharpeRatioPerSampleIsPositiveForConsistentGains) {
  Portfolio portfolio;
  // Deltas: +10, +15, +5 -- varying (so variance is nonzero) but all
  // positive, so the mean/stddev ratio should come out positive too.
  portfolio.RecordMarkToMarket(0);
  portfolio.RecordMarkToMarket(10);
  portfolio.RecordMarkToMarket(25);
  portfolio.RecordMarkToMarket(30);

  // Not asserting an exact value since this is deliberately an
  // illustrative statistic, not a precisely specified one (see
  // portfolio.hpp's caveat) -- just that consistent gains score positive.
  const auto sharpe = portfolio.sharpe_ratio_per_sample();
  ASSERT_TRUE(sharpe.has_value());
  EXPECT_GT(*sharpe, 0.0);
}

}  // namespace
}  // namespace riptide
