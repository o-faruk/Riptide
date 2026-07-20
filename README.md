# Riptide

A low-latency limit-order-book matching engine and event-driven backtester,
built in modern C++.

**Status: Phase 2 (LOBSTER validation) in progress.** This README is a stub
and will be rewritten properly in Phase 6 — see `docs/DESIGN.md` for the
real detail in the meantime.

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
2. LOBSTER validation gate — the correctness bar the project must clear *(current)*
3. Honest benchmark methodology
4. Latency engineering — profiled, incremental, logged
5. Event-driven backtester
6. Documentation and presentation
