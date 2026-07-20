#include "riptide/event.hpp"

#include <gtest/gtest.h>

#include <variant>

namespace riptide {
namespace {

// Overload set for std::visit — mirrors how downstream consumers (the
// validator, the backtester) will handle the event stream exhaustively.
// If a new Event alternative is added and a visitor forgets to handle it,
// this fails to compile rather than silently doing nothing at runtime.
template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

TEST(Event, VisitDispatchesToTradeAlternative) {
  Event event = Trade{.resting_id = 1, .aggressor_id = 2, .aggressor_side = Side::Buy,
                       .price = 1000, .quantity = 5};

  bool visited_trade = false;
  std::visit(Overloaded{
                 [&](const Trade& t) {
                   visited_trade = true;
                   EXPECT_EQ(t.resting_id, 1u);
                   EXPECT_EQ(t.quantity, 5u);
                 },
                 [](const auto&) { FAIL() << "expected Trade alternative"; },
             },
             event);

  EXPECT_TRUE(visited_trade);
}

TEST(Event, RejectedCarriesReason) {
  Event event = Rejected{.id = 42, .reason = RejectReason::InvalidQuantity};
  const auto* rejected = std::get_if<Rejected>(&event);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason, RejectReason::InvalidQuantity);
}

}  // namespace
}  // namespace riptide
