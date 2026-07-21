#include "riptide/order_port.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "riptide/matching_engine.hpp"
#include "riptide/portfolio.hpp"
#include "riptide/strategy.hpp"

namespace riptide {
namespace {

// Records every OnOwnEvent/OnMarketEvent call it receives, for assertions
// -- never submits anything itself, so tests control submissions directly
// through the OrderPort under test.
class RecordingStrategy : public Strategy {
 public:
  void OnMarketEvent(const std::vector<Event>& events, OrderPort&) override {
    for (const auto& event : events) market_events.push_back(event);
  }
  void OnOwnEvent(const Event& event, OrderPort&) override { own_events.push_back(event); }

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

  OrderPort MakePort() {
    return OrderPort(
        [this](NewOrderRequest request) { return engine.new_order(std::move(request)); },
        [this](OrderId id) { return engine.cancel(id); },
        [this](ModifyRequest request) { return engine.modify(std::move(request)); },
        [this]() { return next_id++; }, portfolio, strategy);
  }
};

TEST(OrderPort, BuySubmitsThroughEngineAndRegistersIdWithPortfolio) {
  Fixture fixture;
  OrderPort port = fixture.MakePort();

  const auto events =
      port.Buy(OrderType::Limit, TimeInForce::GTC, /*price=*/100, /*quantity=*/10);

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
  port.Buy(OrderType::Limit, TimeInForce::GTC, /*price=*/100, /*quantity=*/10);

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
  for (const auto& event : market_events) fixture.portfolio.ApplyEvent(event);

  // Strategy bought 10 @ 100 as the resting side: position +10, cash -1000.
  EXPECT_EQ(fixture.portfolio.position(), 10);
  EXPECT_EQ(fixture.portfolio.cash(), -1000);
  EXPECT_EQ(fixture.portfolio.fill_count(), 1u);
}

TEST(OrderPort, CancelAndModifyRouteThroughEngineAndNotifyStrategy) {
  Fixture fixture;
  OrderPort port = fixture.MakePort();

  port.Buy(OrderType::Limit, TimeInForce::GTC, /*price=*/100, /*quantity=*/10);
  const auto cancel_events = port.Cancel(/*id=*/1);

  ASSERT_EQ(cancel_events.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<Cancelled>(cancel_events[0]));
  EXPECT_EQ(fixture.strategy.own_events.size(), 2u);  // Accepted, then Cancelled
}

}  // namespace
}  // namespace riptide
