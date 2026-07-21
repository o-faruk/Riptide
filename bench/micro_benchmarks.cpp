// Google Benchmark micro-benchmarks, isolating specific operations under
// controlled, synthetic conditions -- complementary to
// bench/replay_harness.cpp's real-data end-to-end measurement. The most
// important thing measured here is crossing cost: a real LOBSTER type-1
// row only ever shows an order's resting remainder (see
// docs/DESIGN.md), so real-data replay essentially never exercises the
// crossing path. This file constructs that scenario directly instead.
//
// Every benchmark is templated on Book/Engine type and instantiated for
// both OrderBook/ReferenceEngine and PooledOrderBook/PooledMatchingEngine
// -- same reasoning as MatchingEngine<Book> itself: one definition, run
// against whichever type is being compared, rather than a hand-copied
// twin benchmark per optimization that can drift out of sync.

#include <benchmark/benchmark.h>

#include "riptide/matching_engine.hpp"
#include "riptide/order_book.hpp"
#include "riptide/pooled_order_book.hpp"

namespace {

using riptide::NewOrderRequest;
using riptide::Order;
using riptide::OrderBook;
using riptide::OrderId;
using riptide::OrderType;
using riptide::PooledOrderBook;
using riptide::Quantity;
using riptide::ReferenceEngine;
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
template <typename Book>
void BM_OrderBookInsert(benchmark::State& state) {
  OrderId next_id = 1;
  for (auto _ : state) {
    state.PauseTiming();  // fresh book per iteration excluded from timing
    Book book;
    state.ResumeTiming();
    book.insert(MakeRestingOrder(next_id, Side::Buy, 1000, 10, next_id));
    ++next_id;
  }
}
BENCHMARK_TEMPLATE(BM_OrderBookInsert, OrderBook);
BENCHMARK_TEMPLATE(BM_OrderBookInsert, PooledOrderBook);

// Insert immediately followed by remove: the resting-then-cancel path.
template <typename Book>
void BM_OrderBookInsertThenRemove(benchmark::State& state) {
  OrderId next_id = 1;
  for (auto _ : state) {
    state.PauseTiming();
    Book book;
    const OrderId id = next_id++;
    book.insert(MakeRestingOrder(id, Side::Buy, 1000, 10, id));
    state.ResumeTiming();
    benchmark::DoNotOptimize(book.remove(id));
  }
}
BENCHMARK_TEMPLATE(BM_OrderBookInsertThenRemove, OrderBook);
BENCHMARK_TEMPLATE(BM_OrderBookInsertThenRemove, PooledOrderBook);

// MatchingEngine::new_order, resting (non-crossing): a GTC limit order on
// an empty book -- the "add order" cost paid for every quote that
// doesn't immediately trade, which real replay data is dominated by.
template <typename Engine>
void BM_EngineNewOrderResting(benchmark::State& state) {
  OrderId next_id = 1;
  for (auto _ : state) {
    state.PauseTiming();
    Engine engine;
    state.ResumeTiming();
    engine.new_order(NewOrderRequest{.id = next_id++,
                                      .side = Side::Buy,
                                      .type = OrderType::Limit,
                                      .tif = TimeInForce::GTC,
                                      .price = 1000,
                                      .quantity = 10});
  }
}
BENCHMARK_TEMPLATE(BM_EngineNewOrderResting, ReferenceEngine);
BENCHMARK_TEMPLATE(BM_EngineNewOrderResting, riptide::PooledMatchingEngine);

// MatchingEngine::new_order, crossing: pre-populate `sweep_depth` resting
// sell orders at consecutive price levels, then submit one marketable
// buy that walks and fully consumes all of them in a single call.
// Parameterized so the log shows how crossing cost scales with the
// number of price levels/orders swept.
template <typename Engine>
void BM_EngineNewOrderCrossing(benchmark::State& state) {
  const auto sweep_depth = static_cast<int>(state.range(0));
  OrderId next_id = 1;

  for (auto _ : state) {
    state.PauseTiming();  // book setup excluded from timing
    Engine engine;
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
BENCHMARK_TEMPLATE(BM_EngineNewOrderCrossing, ReferenceEngine)->Arg(1)->Arg(10)->Arg(50)->Arg(100);
BENCHMARK_TEMPLATE(BM_EngineNewOrderCrossing, riptide::PooledMatchingEngine)
    ->Arg(1)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100);

}  // namespace
