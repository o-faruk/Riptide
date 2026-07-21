#include "riptide/pooled_order_book.hpp"

#include <gtest/gtest.h>

namespace riptide {
namespace {

// PooledOrderBook has the exact same contract as OrderBook (see
// order_book_test.cpp for the full structural test suite, and
// tests/differential_test.cpp for the event-for-event equivalence proof
// against ReferenceEngine on randomized sequences) — this file doesn't
// re-duplicate all of that. It covers the core structural behaviors
// directly, plus what's actually new here: the pool allocator under
// churn, which nothing else exercises.

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

TEST(PooledOrderBook, InsertMakesOrderFindableAndSetsBestPrice) {
  PooledOrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));

  Order* found = book.find(1);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->id, 1u);
  EXPECT_EQ(found->remaining, 10u);
  EXPECT_EQ(book.best_price(Side::Buy), std::optional<Price>{1000});
}

TEST(PooledOrderBook, InsertRejectsDuplicateOrderId) {
  PooledOrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));
  EXPECT_FALSE(book.insert(MakeRestingOrder(1, Side::Buy, 1001, 5, 2)));
}

TEST(PooledOrderBook, OrdersAtSamePriceLevelStayInArrivalOrder) {
  PooledOrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(2, Side::Buy, 1000, 5, 2)));

  Order* front = book.front(Side::Buy, 1000);
  ASSERT_NE(front, nullptr);
  EXPECT_EQ(front->id, 1u);

  book.fill_front(Side::Buy, 1000, 10);  // fully fills order 1, removes it

  front = book.front(Side::Buy, 1000);
  ASSERT_NE(front, nullptr);
  EXPECT_EQ(front->id, 2u);
}

TEST(PooledOrderBook, RemoveErasesOrderAndCleansUpEmptyLevel) {
  PooledOrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));

  EXPECT_TRUE(book.remove(1));
  EXPECT_EQ(book.find(1), nullptr);
  EXPECT_EQ(book.best_price(Side::Buy), std::nullopt);
}

TEST(PooledOrderBook, FillFrontPartialFillKeepsOrderAtFrontOfQueue) {
  PooledOrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Sell, 1000, 10, 1)));

  book.fill_front(Side::Sell, 1000, 4);

  Order* front = book.front(Side::Sell, 1000);
  ASSERT_NE(front, nullptr);
  EXPECT_EQ(front->id, 1u);
  EXPECT_EQ(front->remaining, 6u);
  EXPECT_EQ(book.level_quantity(Side::Sell, 1000), std::optional<Quantity>{6});
}

TEST(PooledOrderBook, SetRemainingInPlaceUpdatesAggregateWithoutMovingPosition) {
  PooledOrderBook book;
  ASSERT_TRUE(book.insert(MakeRestingOrder(1, Side::Buy, 1000, 10, 1)));
  ASSERT_TRUE(book.insert(MakeRestingOrder(2, Side::Buy, 1000, 5, 2)));

  ASSERT_TRUE(book.set_remaining_in_place(1, 3));

  Order* front = book.front(Side::Buy, 1000);
  ASSERT_NE(front, nullptr);
  EXPECT_EQ(front->id, 1u);
  EXPECT_EQ(front->remaining, 3u);
  EXPECT_EQ(book.level_quantity(Side::Buy, 1000), std::optional<Quantity>{8});
}

// High-churn stress test: repeatedly insert then immediately remove, well
// beyond the pool's initial slab capacity — this is what actually
// exercises PoolAllocator's allocate/deallocate/reuse cycle under load,
// which none of the structural tests above (or the differential test's
// smaller sequences) push very hard. Correctness here is "still behaves
// like a normal order book after thousands of pool recycles," not
// anything pool-specific in the assertions themselves.
TEST(PooledOrderBook, SurvivesHighChurnInsertRemoveCycles) {
  PooledOrderBook book;
  constexpr int kCycles = 5000;

  for (int i = 0; i < kCycles; ++i) {
    const OrderId id = static_cast<OrderId>(i);
    ASSERT_TRUE(book.insert(MakeRestingOrder(id, Side::Buy, 1000, 1, id)));
    ASSERT_TRUE(book.remove(id));
  }

  EXPECT_EQ(book.best_price(Side::Buy), std::nullopt);

  // One order surviving after all that churn should still behave
  // normally -- proof the pool isn't leaking state between reuses.
  ASSERT_TRUE(book.insert(MakeRestingOrder(999999, Side::Buy, 1000, 42, 1)));
  Order* found = book.find(999999);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->remaining, 42u);
}

}  // namespace
}  // namespace riptide
