# Riptide

A low-latency limit-order-book matching engine and event-driven backtester,
built in modern C++.

**Status: Phase 0 (scaffolding).** The matching engine itself doesn't exist
yet — this repo currently just builds and runs one trivial sanity test. This
README will be rewritten with real content (architecture, correctness
methodology, benchmark results) once the project reaches Phase 6.

## Build

Requires CMake 3.21+ and GCC 13+ or Clang 17+ (C++20).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `relwithdebinfo`, `release`, `asan`, `ubsan`, `tsan`.

## Roadmap

0. Scaffolding *(current)*
1. Matching engine — correctness only, no optimization
2. LOBSTER validation gate — the correctness bar the project must clear
3. Honest benchmark methodology
4. Latency engineering — profiled, incremental, logged
5. Event-driven backtester
6. Documentation and presentation
