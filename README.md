# Riptide

A low-latency limit-order-book matching engine and event-driven backtester,
built in modern C++.

**Status: Phase 4 (latency engineering) in progress.** This README is a
stub and will be rewritten properly in Phase 6 — see `docs/DESIGN.md`,
`bench/ENVIRONMENT.md`, and `docs/OPTIMIZATION_LOG.md` for the real
detail in the meantime.

## Correctness, so far

The matching engine (Phase 1) has 50 unit tests — covering every edge case
named in the project's spec (partial fills, cancel/modify time-priority
rules, IOC/FOK semantics, crossing multiple price levels, malformed input
rejection) — passing clean under ASan, UBSan, and TSan.

Phase 2 validates the engine against
[LOBSTER](https://lobsterdata.com) sample data — a reconstruction of
NASDAQ TotalView-ITCH, not the raw feed itself; this validates the book
reconstruction logic against a trusted reference, not raw-ITCH parsing.
**Honestly stated finding:** free LOBSTER sample data starts mid-day with
resting liquidity the message file gives no record of, some of it not
referenced until it trades, arbitrarily later in the day. This makes a
"whole day validates with zero mismatches" gate provably unachievable
from this data source, regardless of adapter design — not a weakness in
the matching engine itself. `docs/DESIGN.md` documents the exact
mechanism, with concrete evidence, and the resulting (honest, small)
validated-row-count baselines per ticker.

## Benchmarking, so far

Phase 3 built the measurement apparatus ahead of any optimization work:
`bench/replay_harness.cpp` (real-data New/Cancel/Modify latency
percentiles, histogram, throughput, with disclosed and subtracted timer
overhead) and `bench/micro_benchmarks.cpp` (Google Benchmark, isolating
crossing cost via a controlled synthetic sweep, since real LOBSTER data
essentially never exercises that path directly).

The project's original target machine (an Acer Nitro 5 laptop) broke
mid-project and is no longer available; the target is now a desktop (AMD
Ryzen 5 7600X) accessed via WSL2 on Windows — a real, disclosed change in
what "the target machine" means for this project, detailed in
`bench/ENVIRONMENT.md`. Real baseline across all three of Phase 2's
validated tickers, `release` build, median across 10 independent runs:

| Ticker | p50 (all) | p99 (all) | max (all) | throughput |
|---|---|---|---|---|
| AAPL | 70ns | 261ns | 43.6µs | 3.68M msg/s |
| AMZN | 60ns | 171ns | 28.9µs | 4.33M msg/s |
| MSFT | 50ns | 161ns | 30.5µs | 4.58M msg/s |

Consistent order of magnitude across tickers despite MSFT having ~2.8x
AAPL's message count — a reasonable sanity check these are real, stable
numbers rather than a one-off fluke. New/Cancel/Modify breakdown, IQRs,
crossing-cost micro-benchmarks (crossing cost *drops* per-order as sweep
depth grows, from 468ns at depth 1 to ~51ns/order at depth 100 — fixed
per-call overhead amortizing over more fills, a real signal for Phase 4's
profiling to confirm), full methodology, and known limitations (WSL2
exposes no CPU frequency governor at all, unlike bare-metal Linux) all in
`bench/ENVIRONMENT.md`.

## Build

Requires CMake 3.21+ and GCC 13+ or Clang 17+ (C++20).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `relwithdebinfo`, `release`, `asan`, `ubsan`, `tsan`.

## Roadmap

0. Scaffolding — done
1. Matching engine — correctness only, no optimization — done
2. LOBSTER validation gate — the correctness bar the project must clear — done
3. Honest benchmark methodology — harness built, baselines captured (AAPL/AMZN/MSFT + crossing micro-benchmarks) — done
4. Latency engineering — profiled, incremental, logged *(current — reference engine frozen, differential test + fuzzer built, real profiling pending on target hardware)*
5. Event-driven backtester
6. Documentation and presentation
