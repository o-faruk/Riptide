# Riptide

[![CI](https://github.com/o-faruk/Riptide/actions/workflows/ci.yml/badge.svg)](https://github.com/o-faruk/Riptide/actions/workflows/ci.yml)

A C++20 limit-order-book matching engine and event-driven backtester,
built as a from-scratch systems project: correctness validated against
real exchange-reconstructed market data, latency measured (never
guessed at) on real hardware, and every optimization logged with its
actual before/after numbers — including the one that didn't help enough
to keep chasing further.

**Status: Phase 6 (documentation and presentation) in progress.**
Phases 0-5 are complete; see the roadmap at the bottom and
`docs/DESIGN.md` / `docs/OPTIMIZATION_LOG.md` / `bench/ENVIRONMENT.md`
for the full detail behind every claim in this file.

## What's actually in here

- **A price-time-priority matching engine** (`include/riptide/`,
  `src/`) — Limit/Market orders, GTC/IOC/FOK, partial fills,
  cancel/modify with the correct queue-priority rules, all as a class
  template (`MatchingEngine<Book>`) so a swapped-in optimized book
  reuses the exact same, already-validated matching logic.
- **Validation against real market data**, not just unit tests
  (`tools/lobster/`, `tools/validate/`) — LOBSTER's reconstructed
  NASDAQ TotalView-ITCH sample data, replayed message-by-message and
  checked against LOBSTER's own reconstructed book state.
- **Real, disclosed benchmarking** (`bench/`) — Google Benchmark
  micro-benchmarks and a custom replay harness with percentile/
  throughput reporting, timer-overhead subtraction disclosed, run on a
  named real machine, raw output committed unedited to
  `bench/results/`.
- **A logged optimization loop** (`docs/OPTIMIZATION_LOG.md`) — profile
  first, state the hypothesis before writing code, re-validate against
  both the differential test and the LOBSTER gate, re-benchmark, record
  the result whether it helped or not.
- **An event-driven backtester** (`backtest/`, `tools/backtest/`) — a
  `Strategy` trades in the exact same live engine that real historical
  order flow replays through, so fills are real match results, not
  approximated against the historical tape.
- **A real-data console** (`presentation/console.html`) — a static,
  self-contained page presenting the numbers above (throughput,
  latency percentiles, a real order-book snapshot, a real trade tape,
  the optimization result, the honest validation-gate ceiling), every
  figure traced to a source file in `presentation/README.md`. Open
  directly in a browser, no build step.

## Correctness (Phases 1-2)

The matching engine has 50 unit tests covering every edge case named in
the project's spec (partial fills, cancel/modify time-priority rules,
IOC/FOK semantics, crossing multiple price levels, malformed input
rejection), passing clean under ASan, UBSan, and TSan. The full suite
(103 tests as of Phase 5, spanning the engine, the pool-allocator
differential test, and the backtester) runs in CI (`.github/workflows/
ci.yml`) across all four sanitizer/debug configurations on every push.

Beyond unit tests, the engine is validated against
[LOBSTER](https://lobsterdata.com) sample data — a reconstruction of
NASDAQ TotalView-ITCH, not the raw feed itself, so this validates the
book-reconstruction logic against a trusted reference, not raw-ITCH
parsing. **Honestly stated finding:** free LOBSTER sample data starts
mid-day with resting liquidity the message file gives no record of,
some of it not referenced until it trades, arbitrarily later in the
day. This makes a "whole day validates with zero mismatches" gate
provably unachievable from this data source, regardless of adapter
design — not a weakness in the matching engine itself.
`docs/DESIGN.md` documents the exact mechanism, with concrete evidence,
and the resulting (honest, small) validated-row-count baselines per
ticker — enforced as a CI regression guard
(`.github/workflows/lobster-validation.yml`, manually triggered since it
fetches real data at run time).

## Benchmarking and optimization (Phases 3-4)

Phase 3 built the measurement apparatus *before* any optimization work:
`bench/replay_harness.cpp` (real-data New/Cancel/Modify latency
percentiles, histogram, throughput, with disclosed and subtracted timer
overhead) and `bench/micro_benchmarks.cpp` (Google Benchmark, isolating
crossing cost via a controlled synthetic sweep, since real LOBSTER data
essentially never exercises that path directly).

The project's original target machine (an Acer Nitro 5 laptop) broke
mid-project and is no longer available; the target is now a desktop
(AMD Ryzen 5 7600X) accessed via WSL2 on Windows — a real, disclosed
change in what "the target machine" means for this project, detailed
in `bench/ENVIRONMENT.md`. Real baseline across all three of Phase 2's
validated tickers, `release` build, median across 10 independent runs:

| Ticker | p50 (all) | p99 (all) | max (all) | throughput |
|---|---|---|---|---|
| AAPL | 70ns | 261ns | 43.6µs | 3.68M msg/s |
| AMZN | 60ns | 171ns | 28.9µs | 4.33M msg/s |
| MSFT | 50ns | 161ns | 30.5µs | 4.58M msg/s |

Consistent order of magnitude across tickers despite MSFT having ~2.8x
AAPL's message count — a reasonable sanity check these are real, stable
numbers rather than a one-off fluke.

Phase 4 profiled that baseline (`perf record`, isolated to the engine
under a synthetic crossing scenario) and found real self-time in FIFO
queue heap allocation during multi-level crossing sweeps. The fix (a
pre-allocated pool allocator, `include/riptide/fixed_size_pool.hpp` +
`pool_allocator.hpp`, backing a new `PooledMatchingEngine` that shares
the *exact same* matching logic as the reference engine via the
`MatchingEngine<Book>` template) measured out exactly as hypothesized:
negligible effect on a plain resting insert (only ever one allocation
either way), and a real, monotonically growing improvement on crossing
as sweep depth increases — from −2.6% at depth 1 up to **−14.4% at
depth 100**. Phase 4 then stopped deliberately, for scope/time reasons
rather than a flattened profile — `docs/OPTIMIZATION_LOG.md` says so
explicitly, along with the untried candidates (intrusive FIFO lists,
flat price-level array, SoA layout, PGO, ...) left for anyone picking
this back up.

## Backtesting (Phase 5)

The central design question for any limit-order-book backtester: once
a strategy's own orders enter the book, does the replay still have to
match history? Riptide picks **real participation** over the more
common "approximate fills against the historical tape" approach — a
`Strategy`'s orders are submitted to the *exact same* live
`MatchingEngine` that `lobster::Adapter` is replaying real historical
order flow into, through an `OrderPort`. Fills are real match results,
not a guess about what would have happened. The honest tradeoff: once
the strategy trades, the book can diverge from LOBSTER's own historical
orderbook file — expected, and fine, since Phase 5 isn't re-running
Phase 2's validation gate, it's using LOBSTER as realistic
other-participant flow, not a target the strategy must reproduce. See
`docs/DESIGN.md`'s Phase 5 section for the full reasoning, including a
gap (`Strategy` initially had no way to see current book prices) that
only became visible once a real example strategy was written against
it — a useful example of why "write the demo before calling it done"
matters.

What's there: a `Portfolio` tracking cash and per-instrument position
with O(1) streaming max-drawdown and a mean/stddev "per-sample Sharpe"
— deliberately *not* called a real Sharpe ratio, since it's computed on
per-replayed-row deltas within one session rather than independent,
annualized periodic returns; calling it a real Sharpe ratio would be
exactly the kind of fabricated number this project's rules forbid.
Optional per-instrument `RiskLimits` (max position, max order size)
enforced by `OrderPort` before an order ever reaches the engine.
`Backtester<Engine>` for single-instrument runs and
`MultiInstrumentBacktester<Engine>` for several LOBSTER files replayed
in true chronological order at once (verified against two real files —
AAPL and AMZN — replayed together, each instrument's Phase 2 baseline
still holding, both provably interleaved rather than replayed
sequentially).

`tools/backtest` is the CLI: it runs a small, honestly-labeled demo
strategy (`RestAtBestBidStrategy` — rests one resting order at the
touch and holds it; a machinery demo, not a claim of a sound trading
strategy) against a real LOBSTER file. Example real output from an
actual run against AAPL (trimmed to the highlights — full output also
includes max |position|, max drawdown, and per-sample Sharpe):

```
$ ./build/debug/tools/backtest/riptide_backtest_cli \
    data/AAPL/AAPL_2012-06-21_34200000_57600000_message_10.csv \
    data/AAPL/AAPL_2012-06-21_34200000_57600000_orderbook_10.csv \
    --instrument AAPL --quantity 100

Rows replayed: 1904
Stopped early: new_order for order 19260106 unexpectedly crossed the
  book — our book state has already diverged from LOBSTER's
Fills: 3
Position: 100
Cash: -585330000
Mark-to-market P&L (mid=5853500): 20000
```

That "stopped early" line is the expected divergence described above —
not a bug, the direct consequence of the strategy having actually
traded.

## Build

Requires CMake 3.21+ and GCC 13+ or Clang 17+ (C++20).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `relwithdebinfo`, `release`, `asan`, `ubsan`, `tsan`.
CI (`.github/workflows/ci.yml`) runs `debug`, `asan`, `ubsan`, and
`tsan` on every push. LOBSTER validation
(`.github/workflows/lobster-validation.yml`) is triggered manually,
since it fetches real sample data at run time — see
`tools/fetch_data.sh` for why that's never automatic.

## Project layout

```
include/riptide/  Core engine types: Order, Event, MatchingEngine<Book>,
                   OrderBook, the pool allocator, Portfolio, Strategy,
                   OrderPort, RiskLimits.
src/               Implementations for the above.
tools/lobster/     LOBSTER CSV parsing + Adapter<Engine> (message-stream
                   -> engine-call translation).
tools/validate/    Phase 2's LOBSTER validation gate CLI.
tools/backtest/    Phase 5's backtest demo CLI.
backtest/          Backtester<Engine> and MultiInstrumentBacktester<Engine>.
bench/             Replay harness, micro-benchmarks, committed raw results.
tests/             GoogleTest suite (103 tests).
fuzz/              Differential-test/fuzz-target shared random-sequence
                   generator.
docs/              DESIGN.md (design reasoning) and OPTIMIZATION_LOG.md
                   (every optimization attempt, including negative results).
```

## License

[MIT](LICENSE).

## Roadmap

0. Scaffolding — done
1. Matching engine — correctness only, no optimization — done
2. LOBSTER validation gate — the correctness bar the project must clear — done
3. Honest benchmark methodology — harness built, baselines captured (AAPL/AMZN/MSFT + crossing micro-benchmarks) — done
4. Latency engineering — one validated optimization (order pool allocator, 2.6-14.4% faster on crossing sweeps), deliberately paused for scope/time reasons rather than a flattened profile — see `docs/OPTIMIZATION_LOG.md` — done for now
5. Event-driven backtester — real-participation model, risk limits, drawdown/Sharpe, multi-instrument support — done
6. Documentation and presentation *(current)*
