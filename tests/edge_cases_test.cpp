// This file is a checklist, not just a test file: it maps every edge case
// the project spec calls out explicitly to the test that covers it, so a
// reviewer (or future me) can verify none were silently skipped.
//
//   cancel of an order already fully filled
//     -> MatchingEngine.CancelOfAlreadyFullyFilledOrderIsRejected (matching_engine_test.cpp)
//   cancel of the order currently at the top of book
//     -> MatchingEngine.CancelOfTopOfBookOrderExposesNextBestPrice (matching_engine_test.cpp)
//   cancel that empties a price level (level must be removed cleanly)
//     -> MatchingEngine.CancelThatEmptiesAPriceLevelRemovesTheLevel (matching_engine_test.cpp)
//     -> OrderBook.RemoveErasesOrderAndCleansUpEmptyLevel (order_book_test.cpp)
//   modify of a partially filled order
//     -> MatchingEngine.ModifyOfPartiallyFilledOrderOperatesOnRemainingNotOriginal
//        (matching_engine_test.cpp)
//   FOK that cannot be fully satisfied (must not partially execute)
//     -> MatchingEngine.FokRejectsAtomicallyWhenBookCannotFullyFill (matching_engine_test.cpp)
//   market order against an empty book
//     -> MatchingEngine.MarketOrderAgainstEmptyBookCancelsEntirely (matching_engine_test.cpp)
//   zero-quantity rejection
//     -> MatchingEngine.ZeroQuantityIsRejected (matching_engine_test.cpp)
//   negative-price rejection
//     -> MatchingEngine.NegativePriceIsRejected (this file — the existing
//        NonPositivePriceIsRejected test only exercised price == 0)
//   duplicate order IDs
//     -> MatchingEngine.DuplicateOrderIdIsRejectedAndLeavesOriginalIntact
//        (matching_engine_test.cpp)
//
// Also covered here: the "fully deterministic" requirement — same input
// sequence must always produce the identical event stream. Nothing in the
// implementation reads wall-clock time or iterates an unordered container
// in a way that affects output, but that claim is only worth as much as a
// test that would catch a regression of it.

#include "riptide/matching_engine.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace riptide {
namespace {

TEST(MatchingEngineEdgeCases, NegativePriceIsRejected) {
  ReferenceEngine engine;
  auto events = engine.new_order(NewOrderRequest{.id = 1,
                                                   .side = Side::Buy,
                                                   .type = OrderType::Limit,
                                                   .tif = TimeInForce::GTC,
                                                   .price = Price{-500},
                                                   .quantity = 10});

  ASSERT_EQ(events.size(), 1u);
  const auto* rejected = std::get_if<Rejected>(&events[0]);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason, RejectReason::InvalidPrice);
}

// Replays the same mixed sequence of new/cancel/modify operations through
// two independent engines and asserts the two event streams are equal
// field-for-field. This is what "fully deterministic" actually means
// operationally, and it's the same property Phase 4's differential test
// (reference engine vs. optimized engine) will lean on.
std::vector<Event> RunScenario(ReferenceEngine& engine) {
  std::vector<Event> all_events;
  auto record = [&](std::vector<Event> events) {
    all_events.insert(all_events.end(), events.begin(), events.end());
  };

  record(engine.new_order(NewOrderRequest{.id = 1,
                                           .side = Side::Sell,
                                           .type = OrderType::Limit,
                                           .tif = TimeInForce::GTC,
                                           .price = 1000,
                                           .quantity = 5}));
  record(engine.new_order(NewOrderRequest{.id = 2,
                                           .side = Side::Sell,
                                           .type = OrderType::Limit,
                                           .tif = TimeInForce::GTC,
                                           .price = 1001,
                                           .quantity = 5}));
  record(engine.new_order(NewOrderRequest{.id = 3,
                                           .side = Side::Buy,
                                           .type = OrderType::Limit,
                                           .tif = TimeInForce::GTC,
                                           .price = 999,
                                           .quantity = 10}));
  record(engine.modify(
      ModifyRequest{.id = 3, .price = Price{1001}, .quantity = std::nullopt}));
  record(engine.cancel(2));
  record(engine.new_order(NewOrderRequest{.id = 4,
                                           .side = Side::Buy,
                                           .type = OrderType::Market,
                                           .tif = TimeInForce::IOC,
                                           .price = std::nullopt,
                                           .quantity = 3}));
  record(engine.new_order(NewOrderRequest{.id = 5,
                                           .side = Side::Sell,
                                           .type = OrderType::Limit,
                                           .tif = TimeInForce::FOK,
                                           .price = 1000,
                                           .quantity = 100}));

  return all_events;
}

TEST(MatchingEngineEdgeCases, ReplayingTheSameSequenceProducesIdenticalEvents) {
  ReferenceEngine first;
  ReferenceEngine second;

  EXPECT_EQ(RunScenario(first), RunScenario(second));
}

}  // namespace
}  // namespace riptide
