# Optimization Log

Every optimization attempt gets an entry here, in the order it was
attempted — including attempts that didn't help or made things worse.
Negative results are kept deliberately: they're evidence of real
measurement discipline, not just a changelog of wins, and "we tried X and
it didn't help, here's why" is often more credible than another
speedup number.

## The loop (no exceptions)

Every entry below followed this, in order:

1. **Profile first.** `perf stat`/`perf record` with cache-miss and
   branch-miss counters against the real baseline (`bench/replay_harness`
   and/or `bench/micro_benchmarks`) — never a guess about what's slow.
2. **State the hypothesis** before writing any code: what's slow, why,
   and what the fix is expected to buy, in concrete terms (not just
   "should be faster").
3. **Implement** — as a new/modified component alongside `MatchingEngine`
   (`ReferenceEngine`), never by editing the reference implementation
   itself. See `include/riptide/matching_engine.hpp`'s `ReferenceEngine`
   comment for why.
4. **Re-run full validation**: `tests/differential_test.cpp` (optimized
   vs. reference on randomized sequences) and Phase 2's LOBSTER gate
   (`tools/validate`, `--min-rows` regression guard against the
   documented baselines in `docs/DESIGN.md`). **If either fails, the
   change is reverted before anything else happens** — a faster engine
   that doesn't match the reference isn't an optimization, it's a
   different (and unvalidated) engine.
5. **Re-benchmark**: `bench/run_replay_benchmark.sh` (median + IQR
   across >=10 runs, same discipline as the Phase 3 baseline) and/or
   `bench/micro_benchmarks.cpp` for the specific operation targeted.
   Record before/after here regardless of outcome.

## Entry template

```markdown
### N. <short name> — YYYY-MM-DD

**Profile evidence**: what perf/cache-miss/branch-miss data motivated
this, with the actual numbers/output, not a paraphrase.

**Hypothesis**: what's slow, why, and the expected effect, stated before
implementation.

**Change**: what was actually built (component name, files touched, one
paragraph of design reasoning — link to code comments for detail rather
than duplicating them here).

**Validation**: differential test result, LOBSTER gate result (still
matches documented baseline row counts?). PASS/FAIL, and if FAIL, that
the change was reverted.

**Before/after**: the actual measured numbers, same format as
`bench/ENVIRONMENT.md`'s results tables — median [IQR] across >=10 runs,
same machine, same methodology. Link to the `bench/results/` files.

**Verdict**: helped / didn't help / made it worse, and whether it's kept.
```

## Entries

### 1. Order pool allocator — 2026-07-20

**Profile evidence**: `perf stat` against `bench/replay_harness` on the
target machine (AMD Ryzen 5 7600X, WSL2 Ubuntu 26.04) showed
444196 cache-misses, 6623268 branch-misses, 1492573768 instructions,
642105197 cycles — but a large share of that self-time turned out to be
the harness's own CSV parsing and `bench/stats.cpp`'s percentile/
histogram sorting (`std::__introsort_loop`, ~19% self-time), not engine
code, since a real LOBSTER type-1 row only ever shows an order's resting
remainder and essentially never exercises the crossing path (see
`docs/DESIGN.md`). Re-profiling `riptide_bench_micro
--benchmark_filter=BM_EngineNewOrderCrossing` with
`perf record -g --call-graph dwarf` instead (7556 samples, isolated to
just the engine under a synthetic crossing scenario) gave a much cleaner
signal: roughly 46% of self-time in `MatchingEngine`/`OrderBook` code
proper, ~8.6% in `std::list`/`std::map`/`std::unordered_map` bookkeeping,
~6.1% in raw heap allocation (`malloc`/`cfree`), and ~9.24% specifically
in `OrderBook::best_price`.

**Hypothesis**: the FIFO order queue (`std::list<Order>`) allocates and
frees a heap node on every insert/remove. Crossing a multi-level sweep
removes several fully-filled orders in one `new_order` call, hitting
`malloc`/`free` repeatedly on a call stack where nothing else needs the
general-purpose allocator's flexibility (fixed node size, LIFO-friendly
reuse pattern). Replacing it with a pre-allocated, fixed-block-size pool
with an intrusive free list should cut that ~6.1% heap-allocation cost
and reduce the cache-miss overhead of scattered heap nodes, with the
effect scaling with how many orders a single call has to remove (deeper
sweeps = more allocator calls avoided) and being negligible on a simple
resting insert (only ever one allocation either way, dominated by the
surrounding map/hash-map bookkeeping instead).

**Change**: `include/riptide/fixed_size_pool.hpp` + `src/fixed_size_pool.cpp`
(`FixedSizePool`: pre-allocated slabs, intrusive free list, O(1)
allocate/deallocate, doubles capacity on exhaustion) and
`include/riptide/pool_allocator.hpp` (`PoolAllocator<T>`: minimal
`std::allocator`-compliant wrapper backed by a function-local static pool
per node type). `include/riptide/pooled_order_book.hpp` +
`src/pooled_order_book.cpp` (`PooledOrderBook`): byte-for-byte duplicate
of `OrderBook` with `std::list<Order, PoolAllocator<Order>>` as the only
difference. `MatchingEngine` was made a class template
(`MatchingEngine<Book>`, see its `OrderBookLike` concept) specifically so
this and future optimized engines reuse the exact same matching logic
rather than forking it — `PooledMatchingEngine = MatchingEngine<PooledOrderBook>`.

**Validation**: `tests/differential_test.cpp` — `ReferenceEngine` vs.
`PooledMatchingEngine` on 8 seeds x 500 random operations each, byte-for-
byte identical event streams: PASS. Full suite (78 tests, including a
5000-cycle high-churn pool stress test) passes, including under ASan:
PASS. Phase 2's LOBSTER gate (`riptide_validate --engine pooled`)
produces row-for-row identical results to `--engine reference` against
the documented baselines (AAPL 11, AMZN 35, MSFT 6): PASS.

**Before/after**: `riptide_bench_micro`, Release build, target machine
(AMD Ryzen 5 7600X, WSL2 Ubuntu 26.04), 10 repetitions,
aggregates-only:

| Benchmark | Reference (mean) | Pooled (mean) | Delta |
|---|---|---|---|
| Resting insert, no crossing | 471 ns (σ 15.8, cv 3.35%) | 464 ns (σ 10.3, cv 2.21%) | −7 ns (−1.5%, within noise) |
| Crossing, sweep depth 1 | 457 ns | 445 ns | −12 ns (−2.6%) |
| Crossing, sweep depth 10 | 734 ns | 681 ns | −53 ns (−7.2%) |
| Crossing, sweep depth 50 | 2530 ns | 2247 ns | −283 ns (−11.2%) |
| Crossing, sweep depth 100 | 4938 ns | 4225 ns | −713 ns (−14.4%) |

**Verdict**: Helped, kept. Matches the hypothesis exactly: negligible
(noise-level) effect on a single resting insert, and a real,
monotonically-growing improvement on crossing as sweep depth increases
— up to 14.4% at depth 100. Real trading data is dominated by resting
inserts (see the profile-evidence note above on why replay data rarely
exercises crossing), so this optimization's practical value is
concentrated in genuinely marketable order flow rather than the common
case — worth keeping regardless, since it's a pure win with zero
measured downside, but the next candidate should target something that
also helps the resting-insert path (e.g. the price-level lookup or
order-ID map candidates below) if overall replay throughput is the goal.

Candidate list, roughly in expected-value order (from the project's
original scope — actual order will follow whatever profiling data
actually shows, not this list):

- Intrusive doubly-linked lists for FIFO queues, O(1) cancel via an
  order-ID -> node handle map
- Price-level lookup: flat array indexed by tick offset near the
  touch, fallback structure for far-from-touch prices
- Order ID map: open-addressing hash map replacing `std::unordered_map`
- Cache-friendly layout: structure-of-arrays for hot fields
- Branch behavior: profile-guided hints, removing unpredictable
  branches on the hot path
- Lock-free SPSC ring buffer for feed -> engine handoff, if/when
  threading is introduced
- Huge pages for the order pool, if measurable

Stop optimizing when the profile flattens or effort stops paying off —
and say so here, explicitly, when it happens. Knowing when to stop is
itself a signal worth recording.

## Status: Phase 4 paused here — 2026-07-21

Stopping after one entry (the order pool allocator). This is **not**
"the profile flattened" — entry #1's own `perf record` breakdown shows
real remaining self-time in `std::list`/`std::map`/`std::unordered_map`
bookkeeping (~8.6%) and `OrderBook::best_price` (~9.24%), so the
price-level-lookup and order-ID-map candidates below would very likely
show a real, measurable win if pursued. This is an explicit scope/time
decision instead: Riptide is a portfolio project targeting Summer 2027
internship applications, and at this point the marginal value of
another single-digit-to-low-teens percent latency win is lower than
finishing Phase 5 (event-driven backtester) and Phase 6 (documentation)
with the time remaining. The distinction matters because the loop's own
rule is to record *why* optimization stopped, honestly, rather than
imply the well ran dry when it didn't.

Deferred, unexplored, in case Phase 4 is resumed later: intrusive FIFO
lists, flat price-level array, order-ID hash map replacement,
cache-friendly SoA layout, branch/PGO work, lock-free SPSC ring buffer,
huge pages — the full list above, none of which have been profiled
against the real target machine yet.
