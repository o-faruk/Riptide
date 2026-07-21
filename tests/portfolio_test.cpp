#include "riptide/portfolio.hpp"

#include <gtest/gtest.h>

namespace riptide {
namespace {

TEST(Portfolio, IgnoresTradeBetweenTwoUnownedIds) {
  Portfolio portfolio;
  portfolio.ApplyEvent(Trade{
      .resting_id = 1, .aggressor_id = 2, .aggressor_side = Side::Buy, .price = 100, .quantity = 5});

  EXPECT_EQ(portfolio.position(), 0);
  EXPECT_EQ(portfolio.cash(), 0);
  EXPECT_EQ(portfolio.fill_count(), 0u);
}

TEST(Portfolio, AggressorBuyIncreasesPositionAndSpendsCash) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(/*aggressor_id=*/2);
  portfolio.ApplyEvent(Trade{
      .resting_id = 1, .aggressor_id = 2, .aggressor_side = Side::Buy, .price = 100, .quantity = 5});

  EXPECT_EQ(portfolio.position(), 5);
  EXPECT_EQ(portfolio.cash(), -500);
  EXPECT_EQ(portfolio.fill_count(), 1u);
}

TEST(Portfolio, AggressorSellDecreasesPositionAndReceivesCash) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(/*aggressor_id=*/2);
  portfolio.ApplyEvent(Trade{
      .resting_id = 1, .aggressor_id = 2, .aggressor_side = Side::Sell, .price = 100, .quantity = 5});

  EXPECT_EQ(portfolio.position(), -5);
  EXPECT_EQ(portfolio.cash(), 500);
}

// A resting order takes the OPPOSITE side of whatever hit it -- an
// aggressor Buy filled our resting Sell, so we sold, not bought.
TEST(Portfolio, RestingSideIsOppositeOfAggressorSide) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(/*resting_id=*/1);
  portfolio.ApplyEvent(Trade{
      .resting_id = 1, .aggressor_id = 2, .aggressor_side = Side::Buy, .price = 100, .quantity = 5});

  EXPECT_EQ(portfolio.position(), -5);
  EXPECT_EQ(portfolio.cash(), 500);
}

TEST(Portfolio, AccumulatesAcrossMultipleFills) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(2);
  portfolio.ApplyEvent(Trade{
      .resting_id = 1, .aggressor_id = 2, .aggressor_side = Side::Buy, .price = 100, .quantity = 5});
  portfolio.ApplyEvent(Trade{
      .resting_id = 3, .aggressor_id = 2, .aggressor_side = Side::Buy, .price = 110, .quantity = 3});

  EXPECT_EQ(portfolio.position(), 8);
  EXPECT_EQ(portfolio.cash(), -500 - 330);
  EXPECT_EQ(portfolio.fill_count(), 2u);
}

TEST(Portfolio, MarkToMarketCombinesCashAndPositionAtGivenPrice) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(2);
  portfolio.ApplyEvent(Trade{
      .resting_id = 1, .aggressor_id = 2, .aggressor_side = Side::Buy, .price = 100, .quantity = 5});

  // Bought 5 @ 100 (cash -500), marking at 120: 5*120 - 500 = 100.
  EXPECT_EQ(portfolio.mark_to_market(120), 100);
}

TEST(Portfolio, TracksMaxAbsolutePositionAcrossSignFlips) {
  Portfolio portfolio;
  portfolio.RegisterOrderId(2);
  portfolio.ApplyEvent(Trade{
      .resting_id = 1, .aggressor_id = 2, .aggressor_side = Side::Buy, .price = 100, .quantity = 5});
  portfolio.ApplyEvent(Trade{
      .resting_id = 3, .aggressor_id = 2, .aggressor_side = Side::Sell, .price = 100, .quantity = 20});

  // Position path: 0 -> +5 -> -15. Max absolute value seen is 15, not 5.
  EXPECT_EQ(portfolio.position(), -15);
  EXPECT_EQ(portfolio.max_abs_position(), 15u);
}

}  // namespace
}  // namespace riptide
