#include "random_sequence.hpp"

#include <random>
#include <type_traits>

namespace riptide::fuzzing {

namespace {

// A handful of representative Type x TimeInForce combinations. Doesn't
// need to be exhaustive — Phase 1's own test suite already covers every
// combination in isolation; this generator's job is realistic *mixed*
// sequences, not combinatorial completeness.
NewOrderRequest MakeNewOrderRequest(OrderId id, std::mt19937_64& rng) {
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<int> price_level_dist(1, 20);
  std::uniform_int_distribution<std::uint32_t> qty_dist(1, 50);
  std::uniform_int_distribution<int> combo_dist(0, 4);

  const Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
  const Price price = static_cast<Price>(price_level_dist(rng)) * 100;
  const Quantity qty = qty_dist(rng);

  switch (combo_dist(rng)) {
    case 0:
      return NewOrderRequest{.id = id, .side = side, .type = OrderType::Limit,
                              .tif = TimeInForce::GTC, .price = price, .quantity = qty};
    case 1:
      return NewOrderRequest{.id = id, .side = side, .type = OrderType::Limit,
                              .tif = TimeInForce::IOC, .price = price, .quantity = qty};
    case 2:
      return NewOrderRequest{.id = id, .side = side, .type = OrderType::Limit,
                              .tif = TimeInForce::FOK, .price = price, .quantity = qty};
    case 3:
      return NewOrderRequest{.id = id, .side = side, .type = OrderType::Market,
                              .tif = TimeInForce::IOC, .price = std::nullopt, .quantity = qty};
    default:
      return NewOrderRequest{.id = id, .side = side, .type = OrderType::Market,
                              .tif = TimeInForce::FOK, .price = std::nullopt, .quantity = qty};
  }
}

}  // namespace

std::vector<Operation> GenerateRandomSequence(std::uint64_t seed, int count) {
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> op_kind(0, 99);
  std::uniform_int_distribution<int> reference_known(0, 99);

  std::vector<Operation> ops;
  ops.reserve(static_cast<std::size_t>(count));

  std::vector<OrderId> known_ids;
  OrderId next_id = 1;

  for (int i = 0; i < count; ++i) {
    const int kind = op_kind(rng);

    if (kind < 70 || known_ids.empty()) {
      const OrderId id = next_id++;
      ops.push_back(NewOp{MakeNewOrderRequest(id, rng)});
      known_ids.push_back(id);
      continue;
    }

    const bool use_known = reference_known(rng) < 80;
    OrderId target = 0;
    if (use_known) {
      std::uniform_int_distribution<std::size_t> idx_dist(0, known_ids.size() - 1);
      target = known_ids[idx_dist(rng)];
    } else {
      target = next_id + 1'000'000;  // well outside any id ever issued
    }

    if (kind < 90) {
      ops.push_back(CancelOp{target});
    } else {
      std::uniform_int_distribution<int> price_level_dist(1, 20);
      std::uniform_int_distribution<std::uint32_t> qty_dist(1, 50);
      std::uniform_int_distribution<int> field_choice(0, 2);

      ModifyRequest request{.id = target, .price = std::nullopt, .quantity = std::nullopt};
      const int fields = field_choice(rng);
      if (fields == 0 || fields == 2) {
        request.price = static_cast<Price>(price_level_dist(rng)) * 100;
      }
      if (fields == 1 || fields == 2) {
        request.quantity = qty_dist(rng);
      }
      ops.push_back(ModifyOp{request});
    }
  }

  return ops;
}

std::vector<Event> Apply(MatchingEngine& engine, const Operation& operation) {
  return std::visit(
      [&engine](const auto& op) -> std::vector<Event> {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, NewOp>) {
          return engine.new_order(op.request);
        } else if constexpr (std::is_same_v<T, CancelOp>) {
          return engine.cancel(op.id);
        } else {
          return engine.modify(op.request);
        }
      },
      operation);
}

}  // namespace riptide::fuzzing
