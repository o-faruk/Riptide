#include "riptide/order_book.hpp"

#include <gtest/gtest.h>

namespace riptide {
namespace {

// All tests here exercise OrderBook as a pure data structure — no
// crossing/matching policy is involved, only insert/remove/lookup and the
// bookkeeping (price-level cleanup, aggregate quantity, FIFO order) that
// MatchingEngine will build on top of in later tasks.

Order MakeRestingOrder(OrderId id, Side side, Price price, Quantity qty, Sequence seq) {
  return Order{.id = id,
               .side = side,
               .type = OrderType::Limit,
               .tif = TimeInForce::GTC,
               .price = price,
               .quantity = qty,
               .remaining = qty,
               .sequence = seq};
}

TEST(OrderBook, InsertMakesOrderFindableAndSetsBestPrice) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));

  Order* found = book.find(1);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->id, 1u);
  EXPECT_EQ(found->remaining, 10u);

  EXPECT_EQ(book.best_price(Side::Buy), std::optional<Price>{1000});
  EXPECT_EQ(book.best_price(Side::Sell), std::nullopt);
}

TEST(OrderBook, InsertRejectsDuplicateOrderId) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));
  EXPECT_FALSE(book.insert(MakeRestingOrder(1, Side::Buy, 1001, 5, 2)));
}

TEST(OrderBook, BestPriceIsHighestBidAndLowestAsk) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(2, Side::Buy, 1005, 10, 2)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(3, Side::Buy, 995, 10, 3)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(4, Side::Sell, 1010, 10, 4)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(5, Side::Sell, 1020, 10, 5)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(6, Side::Sell, 1008, 10, 6)));

  EXPECT_EQ(book.best_price(Side::Buy), std::optional<Price>{1005});
  EXPECT_EQ(book.best_price(Side::Sell), std::optional<Price>{1008});
}

TEST(OrderBook, OrdersAtSamePriceLevelStayInArrivalOrder) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(2, Side::Buy, 1000, 5, 2)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(3, Side::Buy, 1000, 7, 3)));

  Order* front = book.front(Side::Buy, 1000);
  ASSERT_NE(front, nullptr);
  EXPECT_EQ(front->id, 1u);

  book.fill_front(Side::Buy, 1000, 10);  // fully fills order 1, removes it

  front = book.front(Side::Buy, 1000);
  ASSERT_NE(front, nullptr);
  EXPECT_EQ(front->id, 2u);
}

TEST(OrderBook, LevelQuantityAggregatesAllOrdersAtThatPrice) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Sell, 1000, 10, 1)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(2, Side::Sell, 1000, 5, 2)));

  EXPECT_EQ(book.level_quantity(Side::Sell, 1000), std::optional<Quantity>{15});
  EXPECT_EQ(book.level_quantity(Side::Sell, 999), std::nullopt);
}

TEST(OrderBook, RemoveErasesOrderAndCleansUpEmptyLevel) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));

  EXPECT_TRUE(book.remove(1));
  EXPECT_EQ(book.find(1), nullptr);
  EXPECT_EQ(book.best_price(Side::Buy), std::nullopt);  // level removed, not left empty
}

TEST(OrderBook, RemoveLeavesOtherOrdersAtSameLevelIntact) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(2, Side::Buy, 1000, 5, 2)));

  EXPECT_TRUE(book.remove(1));
  EXPECT_EQ(book.level_quantity(Side::Buy, 1000), std::optional<Quantity>{5});
  EXPECT_EQ(book.best_price(Side::Buy), std::optional<Price>{1000});
}

TEST(OrderBook, RemoveOfUnknownIdReturnsFalse) {
  OrderBook book;
  EXPECT_FALSE(book.remove(999));
}

TEST(OrderBook, FindOfUnknownIdReturnsNullptr) {
  OrderBook book;
  EXPECT_EQ(book.find(999), nullptr);
}

TEST(OrderBook, FillFrontPartialFillKeepsOrderAtFrontOfQueue) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Sell, 1000, 10, 1)));

  book.fill_front(Side::Sell, 1000, 4);

  Order* front = book.front(Side::Sell, 1000);
  ASSERT_NE(front, nullptr);
  EXPECT_EQ(front->id, 1u);
  EXPECT_EQ(front->remaining, 6u);
  EXPECT_EQ(book.level_quantity(Side::Sell, 1000), std::optional<Quantity>{6});
}

TEST(OrderBook, FillFrontFullFillRemovesOrderAndEmptyLevel) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Sell, 1000, 10, 1)));

  book.fill_front(Side::Sell, 1000, 10);

  EXPECT_EQ(book.find(1), nullptr);
  EXPECT_EQ(book.best_price(Side::Sell), std::nullopt);
}

TEST(OrderBook, SetRemainingInPlaceUpdatesAggregateWithoutMovingPosition) {
  OrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(2, Side::Buy, 1000, 5, 2)));

  ASSERT_TRUE(book.set_remaining_in_place(1, 3));

  Order* front = book.front(Side::Buy, 1000);
  ASSERT_NE(front, nullptr);
  EXPECT_EQ(front->id, 1u);  // still first in queue, despite the size cut
  EXPECT_EQ(front->remaining, 3u);
  EXPECT_EQ(book.level_quantity(Side::Buy, 1000), std::optional<Quantity>{8});
}

TEST(OrderBook, SetRemainingInPlaceOfUnknownIdReturnsFalse) {
  OrderBook book;
  EXPECT_FALSE(book.set_remaining_in_place(999, 1));
}

}  // namespace
}  // namespace riptide
