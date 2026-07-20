// Google Benchmark micro-benchmarks, isolating specific operations under
// controlled, synthetic conditions -- complementary to
// bench/replay_harness.cpp's real-data end-to-end measurement. The most
// important thing measured here is crossing cost: a real LOBSTER type-1
// row only ever shows an order's resting remainder (see
// docs/DESIGN.md), so real-data replay essentially never exercises the
// crossing path. This file constructs that scenario directly instead.

#include <benchmark/benchmark.h>

#include "riptide/matching_engine.hpp"
#include "riptide/order_book.hpp"

namespace {

using riptide::MatchingEngine;
using riptide::NewOrderRequest;
using riptide::Order;
using riptide::OrderBook;
using riptide::OrderId;
using riptide::OrderType;
using riptide::Quantity;
using riptide::Sequence;
using riptide::Side;
using riptide::TimeInForce;

Order MakeRestingOrder(OrderId id, Side side, riptide::Price price, Quantity qty, Sequence seq) {
  return Order{.id = id,
               .side = side,
               .type = OrderType::Limit,
               .tif = TimeInForce::GTC,
               .price = price,
               .quantity = qty,
               .remaining = qty,
               .sequence = seq};
}

// The cheapest possible resting-order path: insert into an otherwise-
// empty price level, no crossing, no FIFO queue to walk.
void BM_OrderBookInsert(benchmark::State& state) {
  OrderId next_id = 1;
  for (auto _ : state) {
    state.PauseTiming();  // fresh book per iteration excluded from timing
    OrderBook book;
    state.ResumeTiming();
    book.insert(MakeRestingOrder(next_id, Side::Buy, 1000, 10, next_id));
    ++next_id;
  }
}
BENCHMARK(BM_OrderBookInsert);

// Insert immediately followed by remove: the resting-then-cancel path.
void BM_OrderBookInsertThenRemove(benchmark::State& state) {
  OrderId next_id = 1;
  for (auto _ : state) {
    state.PauseTiming();
    OrderBook book;
    const OrderId id = next_id++;
    book.insert(MakeRestingOrder(id, Side::Buy, 1000, 10, id));
    state.ResumeTiming();
    benchmark::DoNotOptimize(book.remove(id));
  }
}
BENCHMARK(BM_OrderBookInsertThenRemove);

// MatchingEngine::new_order, resting (non-crossing): a GTC limit order on
// an empty book -- the "add order" cost paid for every quote that
// doesn't immediately trade, which real replay data is dominated by.
void BM_EngineNewOrderResting(benchmark::State& state) {
  OrderId next_id = 1;
  for (auto _ : state) {
    state.PauseTiming();
    MatchingEngine engine;
    state.ResumeTiming();
    engine.new_order(NewOrderRequest{.id = next_id++,
                                      .side = Side::Buy,
                                      .type = OrderType::Limit,
                                      .tif = TimeInForce::GTC,
                                      .price = 1000,
                                      .quantity = 10});
  }
}
BENCHMARK(BM_EngineNewOrderResting);

// MatchingEngine::new_order, crossing: pre-populate `sweep_depth` resting
// sell orders at consecutive price levels, then submit one marketable
// buy that walks and fully consumes all of them in a single call.
// Parameterized so the log shows how crossing cost scales with the
// number of price levels/orders swept — the thing Phase 4's price-level
// lookup and intrusive-list work will actually target.
void BM_EngineNewOrderCrossing(benchmark::State& state) {
  const auto sweep_depth = static_cast<int>(state.range(0));
  OrderId next_id = 1;

  for (auto _ : state) {
    state.PauseTiming();  // book setup excluded from timing
    MatchingEngine engine;
    for (int level = 0; level < sweep_depth; ++level) {
      engine.new_order(NewOrderRequest{.id = next_id++,
                                        .side = Side::Sell,
                                        .type = OrderType::Limit,
                                        .tif = TimeInForce::GTC,
                                        .price = 1000 + level,
                                        .quantity = 10});
    }
    state.ResumeTiming();

    engine.new_order(NewOrderRequest{.id = next_id++,
                                      .side = Side::Buy,
                                      .type = OrderType::Limit,
                                      .tif = TimeInForce::IOC,
                                      .price = 1000 + sweep_depth - 1,
                                      .quantity = static_cast<Quantity>(10 * sweep_depth)});
  }
  state.SetItemsProcessed(state.iterations() * sweep_depth);
}
BENCHMARK(BM_EngineNewOrderCrossing)->Arg(1)->Arg(10)->Arg(50)->Arg(100);

}  // namespace
