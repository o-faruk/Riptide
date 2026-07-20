#include "riptide/matching_engine.hpp"

#include <gtest/gtest.h>

#include <variant>

namespace riptide {
namespace {

NewOrderRequest Limit(OrderId id, Side side, Price price, Quantity qty,
                       TimeInForce tif = TimeInForce::GTC) {
  return NewOrderRequest{
      .id = id, .side = side, .type = OrderType::Limit, .tif = tif, .price = price, .quantity = qty};
}

NewOrderRequest Market(OrderId id, Side side, Quantity qty, TimeInForce tif) {
  return NewOrderRequest{
      .id = id, .side = side, .type = OrderType::Market, .tif = tif, .price = std::nullopt,
      .quantity = qty};
}

template <typename T>
const T* EventAt(const std::vector<Event>& events, std::size_t index) {
  if (index >= events.size()) return nullptr;
  return std::get_if<T>(&events[index]);
}

// ---- Resting: nothing to cross -------------------------------------------

TEST(MatchingEngine, RestingLimitOrderOnEmptyBookOnlyAccepts) {
  MatchingEngine engine;
  auto events = engine.new_order(Limit(1, Side::Buy, 1000, 10));

  ASSERT_EQ(events.size(), 1u);
  const auto* accepted = EventAt<Accepted>(events, 0);
  ASSERT_NE(accepted, nullptr);
  EXPECT_EQ(accepted->id, 1u);

  const Order* resting = engine.book().find(1);
  ASSERT_NE(resting, nullptr);
  EXPECT_EQ(resting->remaining, 10u);
}

TEST(MatchingEngine, NonMarketableLimitRestsWithoutCrossing) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Sell, 1010, 10));  // resting ask far above

  auto events = engine.new_order(Limit(2, Side::Buy, 1000, 5));  // doesn't reach it

  ASSERT_EQ(events.size(), 1u);
  EXPECT_NE(EventAt<Accepted>(events, 0), nullptr);
  EXPECT_EQ(engine.book().best_price(Side::Buy), std::optional<Price>{1000});
}

// ---- Crossing --------------------------------------------------------------

TEST(MatchingEngine, AggressiveLimitFullyConsumesSmallerRestingOrder) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Sell, 1000, 5));

  auto events = engine.new_order(Limit(2, Side::Buy, 1000, 10));

  ASSERT_EQ(events.size(), 2u);
  EXPECT_NE(EventAt<Accepted>(events, 0), nullptr);
  const auto* trade = EventAt<Trade>(events, 1);
  ASSERT_NE(trade, nullptr);
  EXPECT_EQ(trade->resting_id, 1u);
  EXPECT_EQ(trade->aggressor_id, 2u);
  EXPECT_EQ(trade->price, 1000);  // maker's price
  EXPECT_EQ(trade->quantity, 5u);

  EXPECT_EQ(engine.book().find(1), nullptr);  // resting order fully filled, gone
  const Order* remainder = engine.book().find(2);
  ASSERT_NE(remainder, nullptr);
  EXPECT_EQ(remainder->remaining, 5u);  // aggressor's leftover rests
}

TEST(MatchingEngine, AggressiveLimitPartiallyFillsLargerRestingOrder) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Sell, 1000, 10));

  auto events = engine.new_order(Limit(2, Side::Buy, 1000, 4));

  ASSERT_EQ(events.size(), 2u);
  const auto* trade = EventAt<Trade>(events, 1);
  ASSERT_NE(trade, nullptr);
  EXPECT_EQ(trade->quantity, 4u);

  const Order* resting = engine.book().find(1);
  ASSERT_NE(resting, nullptr);
  EXPECT_EQ(resting->remaining, 6u);   // partially filled, keeps its place
  EXPECT_EQ(engine.book().find(2), nullptr);  // aggressor fully filled, nothing rests
}

TEST(MatchingEngine, FifoWithinLevelFillsOldestRestingOrderFirst) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Sell, 1000, 3));
  engine.new_order(Limit(2, Side::Sell, 1000, 3));

  auto events = engine.new_order(Limit(3, Side::Buy, 1000, 4));

  ASSERT_EQ(events.size(), 3u);  // Accepted + 2 trades (order 1 fully, order 2 partially)
  const auto* trade1 = EventAt<Trade>(events, 1);
  const auto* trade2 = EventAt<Trade>(events, 2);
  ASSERT_NE(trade1, nullptr);
  ASSERT_NE(trade2, nullptr);
  EXPECT_EQ(trade1->resting_id, 1u);
  EXPECT_EQ(trade1->quantity, 3u);
  EXPECT_EQ(trade2->resting_id, 2u);
  EXPECT_EQ(trade2->quantity, 1u);
}

TEST(MatchingEngine, AggressiveLimitWalksMultiplePriceLevelsInPriceOrder) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Sell, 1000, 5));
  engine.new_order(Limit(2, Side::Sell, 1001, 5));

  auto events = engine.new_order(Limit(3, Side::Buy, 1001, 8));

  ASSERT_EQ(events.size(), 3u);
  const auto* trade1 = EventAt<Trade>(events, 1);
  const auto* trade2 = EventAt<Trade>(events, 2);
  ASSERT_NE(trade1, nullptr);
  ASSERT_NE(trade2, nullptr);
  EXPECT_EQ(trade1->price, 1000);  // cheaper level consumed first
  EXPECT_EQ(trade1->quantity, 5u);
  EXPECT_EQ(trade2->price, 1001);
  EXPECT_EQ(trade2->quantity, 3u);

  // 5 + 3 == 8: the aggressor is fully filled, nothing rests for it. It's
  // the second resting sell that's left with 2 unfilled.
  EXPECT_EQ(engine.book().find(3), nullptr);
  const Order* leftover_resting = engine.book().find(2);
  ASSERT_NE(leftover_resting, nullptr);
  EXPECT_EQ(leftover_resting->remaining, 2u);
}

// ---- IOC ---------------------------------------------------------------

TEST(MatchingEngine, IocRemainderIsCancelledNotRested) {
  MatchingEngine engine;
  auto events = engine.new_order(Limit(1, Side::Buy, 1000, 10, TimeInForce::IOC));

  ASSERT_EQ(events.size(), 2u);
  const auto* cancelled = EventAt<Cancelled>(events, 1);
  ASSERT_NE(cancelled, nullptr);
  EXPECT_EQ(cancelled->reason, CancelReason::IocRemainder);
  EXPECT_EQ(cancelled->remaining_cancelled, 10u);
  EXPECT_EQ(engine.book().find(1), nullptr);
}

TEST(MatchingEngine, IocPartiallyFillsThenCancelsRemainder) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Sell, 1000, 4));

  auto events = engine.new_order(Limit(2, Side::Buy, 1000, 10, TimeInForce::IOC));

  ASSERT_EQ(events.size(), 3u);
  EXPECT_NE(EventAt<Trade>(events, 1), nullptr);
  const auto* cancelled = EventAt<Cancelled>(events, 2);
  ASSERT_NE(cancelled, nullptr);
  EXPECT_EQ(cancelled->remaining_cancelled, 6u);
  EXPECT_EQ(engine.book().find(2), nullptr);
}

// ---- FOK -----------------------------------------------------------------

TEST(MatchingEngine, FokRejectsAtomicallyWhenBookCannotFullyFill) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Sell, 1000, 4));

  auto events = engine.new_order(Limit(2, Side::Buy, 1000, 10, TimeInForce::FOK));

  ASSERT_EQ(events.size(), 1u);  // no Accepted at all — never entered the book
  const auto* rejected = EventAt<Rejected>(events, 0);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason, RejectReason::FokUnfilled);

  // Book must be untouched: no partial execution.
  const Order* untouched = engine.book().find(1);
  ASSERT_NE(untouched, nullptr);
  EXPECT_EQ(untouched->remaining, 4u);
}

TEST(MatchingEngine, FokFillsCompletelyWhenLiquiditySuffices) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Sell, 1000, 6));
  engine.new_order(Limit(2, Side::Sell, 1001, 4));

  auto events = engine.new_order(Limit(3, Side::Buy, 1001, 10, TimeInForce::FOK));

  ASSERT_EQ(events.size(), 3u);  // Accepted + 2 trades, no Cancelled
  EXPECT_NE(EventAt<Accepted>(events, 0), nullptr);
  EXPECT_NE(EventAt<Trade>(events, 1), nullptr);
  EXPECT_NE(EventAt<Trade>(events, 2), nullptr);
  EXPECT_EQ(engine.book().find(3), nullptr);  // fully filled, nothing rests
}

// ---- Market ----------------------------------------------------------------

TEST(MatchingEngine, MarketOrderAgainstEmptyBookCancelsEntirely) {
  MatchingEngine engine;
  auto events = engine.new_order(Market(1, Side::Buy, 5, TimeInForce::IOC));

  ASSERT_EQ(events.size(), 2u);
  EXPECT_NE(EventAt<Accepted>(events, 0), nullptr);
  const auto* cancelled = EventAt<Cancelled>(events, 1);
  ASSERT_NE(cancelled, nullptr);
  EXPECT_EQ(cancelled->remaining_cancelled, 5u);
}

TEST(MatchingEngine, MarketOrderSweepsMultipleLevelsIgnoringPrice) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Sell, 1000, 3));
  engine.new_order(Limit(2, Side::Sell, 1005, 7));

  auto events = engine.new_order(Market(3, Side::Buy, 8, TimeInForce::IOC));

  ASSERT_EQ(events.size(), 3u);  // Accepted + 2 trades, fully filled, no Cancelled
  const auto* trade1 = EventAt<Trade>(events, 1);
  const auto* trade2 = EventAt<Trade>(events, 2);
  ASSERT_NE(trade1, nullptr);
  ASSERT_NE(trade2, nullptr);
  EXPECT_EQ(trade1->price, 1000);
  EXPECT_EQ(trade1->quantity, 3u);
  EXPECT_EQ(trade2->price, 1005);
  EXPECT_EQ(trade2->quantity, 5u);
}

TEST(MatchingEngine, MarketGtcIsRejectedAsInvalidTimeInForce) {
  MatchingEngine engine;
  auto events = engine.new_order(Market(1, Side::Buy, 5, TimeInForce::GTC));

  ASSERT_EQ(events.size(), 1u);
  const auto* rejected = EventAt<Rejected>(events, 0);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason, RejectReason::InvalidTimeInForce);
}

// ---- Validation --------------------------------------------------------

TEST(MatchingEngine, ZeroQuantityIsRejected) {
  MatchingEngine engine;
  auto events = engine.new_order(Limit(1, Side::Buy, 1000, 0));

  ASSERT_EQ(events.size(), 1u);
  const auto* rejected = EventAt<Rejected>(events, 0);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason, RejectReason::InvalidQuantity);
}

TEST(MatchingEngine, NonPositivePriceIsRejected) {
  MatchingEngine engine;
  auto events = engine.new_order(Limit(1, Side::Buy, 0, 10));

  ASSERT_EQ(events.size(), 1u);
  const auto* rejected = EventAt<Rejected>(events, 0);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason, RejectReason::InvalidPrice);
}

TEST(MatchingEngine, MarketOrderCarryingAPriceIsRejected) {
  MatchingEngine engine;
  auto events = engine.new_order(NewOrderRequest{.id = 1,
                                                   .side = Side::Buy,
                                                   .type = OrderType::Market,
                                                   .tif = TimeInForce::IOC,
                                                   .price = 1000,
                                                   .quantity = 10});

  ASSERT_EQ(events.size(), 1u);
  const auto* rejected = EventAt<Rejected>(events, 0);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason, RejectReason::InvalidPrice);
}

TEST(MatchingEngine, DuplicateOrderIdIsRejectedAndLeavesOriginalIntact) {
  MatchingEngine engine;
  engine.new_order(Limit(1, Side::Buy, 1000, 10));

  auto events = engine.new_order(Limit(1, Side::Sell, 2000, 3));

  ASSERT_EQ(events.size(), 1u);
  const auto* rejected = EventAt<Rejected>(events, 0);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason, RejectReason::DuplicateOrderId);

  const Order* original = engine.book().find(1);
  ASSERT_NE(original, nullptr);
  EXPECT_EQ(original->side, Side::Buy);
  EXPECT_EQ(original->price, std::optional<Price>{1000});
  EXPECT_EQ(original->remaining, 10u);
}

}  // namespace
}  // namespace riptide
