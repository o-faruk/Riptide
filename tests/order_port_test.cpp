#include "riptide/order_port.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "riptide/matching_engine.hpp"
#include "riptide/portfolio.hpp"
#include "riptide/risk_limits.hpp"
#include "riptide/strategy.hpp"

namespace riptide {
namespace {

constexpr const char* kAapl = "AAPL";

// Records every OnOwnEvent/OnMarketEvent call it receives, for assertions
// -- never submits anything itself, so tests control submissions directly
// through the OrderPort under test.
class RecordingStrategy : public Strategy {
 public:
  void OnMarketEvent(const InstrumentId&, const std::vector<Event>& events, OrderPort&) override {
    for (const auto& event : events) market_events.push_back(event);
  }
  void OnOwnEvent(const InstrumentId&, const Event& event, OrderPort&) override {
    own_events.push_back(event);
  }

  std::vector<Event> market_events;
  std::vector<Event> own_events;
};

// Wires an OrderPort directly against a real ReferenceEngine -- no
// Backtester/Adapter/file plumbing involved, just the OrderPort <->
// engine <-> Portfolio <-> Strategy contract in isolation.
struct Fixture {
  ReferenceEngine engine;
  Portfolio portfolio;
  RecordingStrategy strategy;
  OrderId next_id = 1;

  OrderPort MakePort(RiskLimits limits = RiskLimits{}) {
    return OrderPort(
        [this](const InstrumentId&, NewOrderRequest request) {
          return engine.new_order(std::move(request));
        },
        [this](const InstrumentId&, OrderId id) { return engine.cancel(id); },
        [this](const InstrumentId&, ModifyRequest request) {
          return engine.modify(std::move(request));
        },
        [this](const InstrumentId&) { return next_id++; },
        [this](const InstrumentId&, Side side) { return engine.book().best_price(side); },
        portfolio, strategy, limits);
  }
};

TEST(OrderPort, BuySubmitsThroughEngineAndRegistersIdWithPortfolio) {
  Fixture fixture;
  OrderPort port = fixture.MakePort();

  const auto events =
      port.Buy(kAapl, OrderType::Limit, TimeInForce::GTC, /*price=*/100, /*quantity=*/10);

  ASSERT_EQ(events.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<Accepted>(events[0]));
  // Own submission events are routed to OnOwnEvent, not OnMarketEvent.
  EXPECT_EQ(fixture.strategy.own_events.size(), 1u);
  EXPECT_TRUE(fixture.strategy.market_events.empty());
}

TEST(OrderPort, RestingOrderLaterFilledByAnotherEngineCallIsAttributedToPortfolio) {
  Fixture fixture;
  OrderPort port = fixture.MakePort();

  // Strategy rests a buy at 100 for 10 shares.
  port.Buy(kAapl, OrderType::Limit, TimeInForce::GTC, /*price=*/100, /*quantity=*/10);

  // Simulate "the market": some OTHER participant's order goes straight
  // to engine.new_order (bypassing OrderPort entirely, the same way
  // lobster::Adapter's market-flow rows do), crossing the strategy's
  // resting order.
  const auto market_events = fixture.engine.new_order(NewOrderRequest{.id = 999,
                                                                        .side = Side::Sell,
                                                                        .type = OrderType::Limit,
                                                                        .tif = TimeInForce::IOC,
                                                                        .price = 100,
                                                                        .quantity = 10});
  ASSERT_TRUE(std::any_of(market_events.begin(), market_events.end(),
                          [](const Event& e) { return std::holds_alternative<Trade>(e); }));

  // This is the piece OrderPort itself can't do: Backtester's Run() loop
  // feeds market-row events to Portfolio directly. Do that here too.
  for (const auto& event : market_events) fixture.portfolio.ApplyEvent(kAapl, event);

  // Strategy bought 10 @ 100 as the resting side: position +10, cash -1000.
  EXPECT_EQ(fixture.portfolio.position(kAapl), 10);
  EXPECT_EQ(fixture.portfolio.cash(), -1000);
  EXPECT_EQ(fixture.portfolio.fill_count(), 1u);
}

TEST(OrderPort, BestPriceReflectsCurrentBookAndNulloptOnEmptySide) {
  Fixture fixture;
  OrderPort port = fixture.MakePort();

  EXPECT_FALSE(port.BestPrice(kAapl, Side::Buy).has_value());

  port.Buy(kAapl, OrderType::Limit, TimeInForce::GTC, /*price=*/100, /*quantity=*/10);
  ASSERT_TRUE(port.BestPrice(kAapl, Side::Buy).has_value());
  EXPECT_EQ(*port.BestPrice(kAapl, Side::Buy), 100);
  EXPECT_FALSE(port.BestPrice(kAapl, Side::Sell).has_value());
}

TEST(OrderPort, CancelAndModifyRouteThroughEngineAndNotifyStrategy) {
  Fixture fixture;
  OrderPort port = fixture.MakePort();

  port.Buy(kAapl, OrderType::Limit, TimeInForce::GTC, /*price=*/100, /*quantity=*/10);
  const auto cancel_events = port.Cancel(kAapl, /*id=*/1);

  ASSERT_EQ(cancel_events.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<Cancelled>(cancel_events[0]));
  EXPECT_EQ(fixture.strategy.own_events.size(), 2u);  // Accepted, then Cancelled
}

TEST(OrderPort, OrderExceedingMaxOrderQuantityIsBlockedBeforeReachingEngine) {
  Fixture fixture;
  RiskLimits limits;
  limits.max_order_quantity = 5;
  OrderPort port = fixture.MakePort(limits);

  const auto events =
      port.Buy(kAapl, OrderType::Limit, TimeInForce::GTC, /*price=*/100, /*quantity=*/10);

  EXPECT_TRUE(events.empty());
  EXPECT_EQ(port.risk_block_count(), 1u);
  EXPECT_EQ(fixture.strategy.own_events.size(), 0u);  // never reached the engine, no notification
  EXPECT_EQ(fixture.portfolio.fill_count(), 0u);
}

TEST(OrderPort, OrderProjectedToBreachMaxAbsPositionIsBlocked) {
  Fixture fixture;
  RiskLimits limits;
  limits.max_abs_position = 8;
  OrderPort port = fixture.MakePort(limits);

  // 10 shares would leave position at 10, breaching the limit of 8.
  const auto events =
      port.Buy(kAapl, OrderType::Limit, TimeInForce::GTC, /*price=*/100, /*quantity=*/10);

  EXPECT_TRUE(events.empty());
  EXPECT_EQ(port.risk_block_count(), 1u);
  EXPECT_EQ(fixture.portfolio.position(kAapl), 0);
}

TEST(OrderPort, WouldBreachRiskLimitsLetsAStrategyCheckBeforeSubmitting) {
  Fixture fixture;
  RiskLimits limits;
  limits.max_order_quantity = 5;
  OrderPort port = fixture.MakePort(limits);

  EXPECT_TRUE(port.WouldBreachRiskLimits(kAapl, Side::Buy, 10));
  EXPECT_FALSE(port.WouldBreachRiskLimits(kAapl, Side::Buy, 5));
}

}  // namespace
}  // namespace riptide
