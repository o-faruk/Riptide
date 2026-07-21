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

None yet — Phase 4 has just started. Profiling against the Phase 3
baseline (`bench/results/`) on the real target machine (see
`bench/ENVIRONMENT.md`) is the next step before any candidate
optimization gets a hypothesis, let alone code.

Candidate list, roughly in expected-value order (from the project's
original scope — actual order will follow whatever profiling data
actually shows, not this list):

- Order pool allocator (pre-allocated slab, free-list reuse, zero
  allocation on the hot path)
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
