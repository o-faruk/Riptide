# Design Notes

Non-obvious design decisions and their reasoning, collected as the project
progresses. Line-level rationale lives in code comments; this document is
for decisions that span multiple files or need more room to explain than a
comment allows.

## Phase 1 — matching engine

- **Prices are scaled `int64` ticks** (`riptide::Price`), matching LOBSTER's
  own scale (dollar price × 10000) so Phase 2 needs no unit conversion.
- **`TimeInForce` is orthogonal to `OrderType`**, not folded into a flat
  four-way enum — see `include/riptide/types.hpp`.
- **`OrderBook` is a pure data structure**; `MatchingEngine` is the only
  thing that decides when two orders should trade. See
  `include/riptide/order_book.hpp` and `matching_engine.hpp`.
- **Trades always print at the resting (maker) order's price.**
- **Modify's time-priority rule** (quantity decrease keeps priority and
  queue position; price change or quantity increase loses it) is judged
  against an order's *current remaining* quantity, not its original
  submitted quantity — see the `Modify*` tests in
  `tests/matching_engine_test.cpp`.

Phase 1's 50 unit tests, covering every edge case the project spec calls
out by name, are the correctness foundation the rest of this document
builds on. Everything below assumes that foundation is solid and asks a
different question: does the engine reconstruct a *real* exchange's book
correctly when driven by *real* market data?

## Phase 2 — LOBSTER validation

### What LOBSTER actually is

LOBSTER is a reconstruction of NASDAQ TotalView-ITCH, not the raw ITCH
feed itself. The `message` file is the event stream; the `orderbook` file
is *LOBSTER's own* reconstructed book state after each message. Validating
against it checks that this project's book-building logic matches a
trusted reference implementation's — it is not the same claim as parsing
raw ITCH correctly. This project does not claim the latter.

### Message → engine translation (`tools/validate/adapter.cpp`)

The interesting design problem in Phase 2 wasn't the matching logic
(Phase 1 already covers that) — it was translating LOBSTER's *already
reconstructed* event semantics into calls against a *from-scratch*
engine. Two things about LOBSTER's semantics are not obvious from its
documentation and had to be worked out empirically against the sample
data:

1. **Type-2 (partial cancellation) `Size` is the amount removed, not the
   resulting size.** Not stated in LOBSTER's docs; established by
   tracing full order lifecycles in sample data (e.g. an order submitted
   with size 3, partially cancelled by 1, later fully deleted with size
   2 — 3 − 1 = 2, confirming `Size` on the type-2 row was a delta).

2. **An order that executes fully and immediately on arrival never gets
   its own type-1 "submission" row.** Only the resting orders it hits
   do, each as a separate type-4 row naming *that* resting order's ID
   and side — never the aggressor's. This mirrors real ITCH behavior
   (an "Add Order" message is only ever sent for the portion that
   actually rests) and means type-4 rows can't be treated as passive
   narration of fills the engine already knows about. Each one has to
   drive a synthesized aggressor order sized exactly to reproduce that
   fill. Because LOBSTER reports executions in genuine price-time-priority
   order and every row is applied strictly in file order, an
   exactly-sized synthetic order can only ever match the named resting
   order — never anything else. See `Adapter::ApplyVisibleExecution`.

### The bootstrap problem, and where it stops being fixable

LOBSTER's free sample files cover regular trading hours only
(09:30–16:00). The book is not empty at message 1 — it already holds
resting liquidity from before the file's time window (pre-market
activity, the opening auction), and the message file gives **no type-1
rows for any of it**. Some of that liquidity isn't referenced at all
until it trades, arbitrarily later in the day, sometimes at prices
better than anything visible in the orderbook file's first row (verified
concretely: order `13419503` in the AAPL sample executes at message line
448 with zero prior mention anywhere in the file's 400,391 rows).

The adapter handles this with a bootstrap-and-bucket approach
(`Adapter::SeedFromInitialBookState`, `Adapter::Resolve`):

- Orderbook row 1 is used to seed one synthetic "bucket" order per
  occupied price level — the best approximation of pre-existing state
  available, since the file only ever reveals aggregate size per level,
  never individual orders.
- A later message referencing an order ID that's neither live nor
  bucket-covered lazily materializes a fresh bucket sized to exactly
  satisfy that operation, which is then immediately consumed by the
  operation itself. Net contribution to permanent book state from a
  first-sighting is always zero — this is what keeps the aggregate
  correct without ever having to know an unknown order's true original
  size.
- A price level can hold multiple distinct never-seen orders; when a
  bucket is exhausted, a fresh one is created rather than treating the
  price as fully explained.

This recovers correctness in every case investigated **except** the
information LOBSTER's free sample genuinely never provides: what,
exactly, is resting beyond the top 10 levels at the moment the book is
seeded. When later trading activity reveals that depth (a shift near the
top of book exposes what was previously an invisible 11th+ level), no
adapter-side heuristic can conjure the correct price/size for it — that
information was never in the data. Empirically, and consistently across
AAPL, AMZN, and MSFT, this first manifests as a mismatch confined to the
deepest one or two levels (levels 1–9 matching exactly while level 10
does not) and can, over enough subsequent messages, compound: a later
type-1 row that should rest peacefully can instead unexpectedly cross
against the (slightly wrong) book, which `Adapter::ApplyNewOrder`
explicitly detects and reports (a genuine LOBSTER type-1 row should never
be marketable, per ITCH semantics — if one is, the book has already
diverged).

### The validation gate, honestly stated

Because of the above, "the full trading day validates with zero
mismatches" is not an achievable bar with free LOBSTER sample data,
regardless of adapter sophistication — the missing information (the true
composition of the pre-9:30 book beyond the visible top 10) cannot be
recovered from this data source. The gate this project uses instead:
validate at full requested depth (10 levels) until the data itself runs
out of information to give the engine, and treat "divergence happens no
earlier than a documented, understood baseline" as the regression guard
(`tools/validate`'s `--min-rows` flag; wired into
`.github/workflows/lobster-validation.yml`, manually triggered — see that
file for why it isn't run automatically on every push).

Current baselines (2012-06-21, level 10 sample data):

| Ticker | Rows validated before first divergence | Total rows in file |
|---|---|---|
| AAPL | 11 | 400,391 |
| AMZN | 35 | 269,748 |
| MSFT | 6 | 668,765 |

These numbers are small in isolation, and that's the honest result of a
free-sample-data ceiling, not a weak matching engine — Phase 1's 50 unit
tests independently establish the engine's crossing/FIFO/cancel/modify
correctness, and every divergence found here was traced to a specific,
named, reproducible cause in the data (documented above), not left
unexplained.

## Phase 3 — benchmark methodology

Full writeup lives in `bench/ENVIRONMENT.md` rather than here, since it's
inherently tied to a specific machine and needs updating whenever that
machine changes (which it did — see that file's account of the project's
original target laptop breaking mid-project). Headline design decisions:
`std::chrono::steady_clock` over `rdtscp` (portability across the ARM64
dev machine and x86_64 target, plus a modern Linux's `CLOCK_MONOTONIC` is
typically TSC-backed anyway), timer overhead measured and subtracted
rather than assumed away, and two deliberately separate measurement tools
— real-data replay for realistic New/Cancel/Modify latency, a synthetic
Google Benchmark sweep for crossing cost specifically, since real LOBSTER
`type-1` rows structurally never cross (see the Phase 2 section above) and
so real-data replay alone can't measure that path at all.

## Phase 4 — latency engineering

### Keeping a reference to diff against

`include/riptide/matching_engine.hpp` now exports
`using ReferenceEngine = MatchingEngine;` — not a rename, a designation.
From this point on, `MatchingEngine`/`ReferenceEngine` is frozen (bug
fixes only, never an optimization); every Phase 4 structural change
(order pool, intrusive lists, flat price-level array, …) belongs in a
new, separate engine type. The moment the reference implementation's own
behavior could change, "diff the optimized engine against the reference"
stops meaning anything.

### Two different kinds of randomized testing, one shared generator

`fuzz/random_sequence.cpp` generates deterministic (seeded), realistic
mixes of new/cancel/modify operations — biased toward a narrow price
range on purpose, so submitted orders collide and cross far more often
than a wide realistic range would, exercising FIFO ordering, multi-level
sweeps, and partial fills much harder than sparse random data would.
`tests/differential_test.cpp` uses it for **seed-driven, reproducible**
sequences: right now Reference-vs-itself (proving the harness is correct
on a case with a known answer before there's a second engine to diff),
becoming Optimized-vs-Reference the moment Phase 4 produces one.

The fuzz target (`fuzz/fuzz_target.cpp`) deliberately does *not* reuse
that seed-driven generator — it builds operations directly from the
fuzzer's raw byte stream (`fuzz/byte_stream.hpp`) instead, field by
field. That's what makes libFuzzer's coverage-guided mutation work at
all: a small mutation to the input bytes needs to produce a small change
in the resulting operation sequence, so the fuzzer can incrementally
discover inputs that reach new code paths. Deriving operations from a
hashed seed (fine for the differential test, where reproducibility
matters more than mutation-friendliness) would make adjacent inputs
produce unrelated sequences and defeat that.

The fuzz target also compiles as an ordinary, portable executable by
default (a `main()` that replays one or more files' bytes once each) —
real fuzzing needs `-fsanitize=fuzzer`, which needs mainline Clang, not
the AppleClang on this project's dev machine, so the default build mode
is what let this get smoke-tested (50 random inputs, clean under ASan
and UBSan) before ever reaching a machine that can actually fuzz it.
Build with `-DRIPTIDE_FUZZ_WITH_LIBFUZZER=ON` and a mainline Clang for
real coverage-guided fuzzing.

### The optimization loop

See `docs/OPTIMIZATION_LOG.md` for the actual loop and its entries.
Every candidate optimization gets profiled first against the real Phase
3 baseline, on the real target machine — not guessed at from this dev
machine, which (per `bench/ENVIRONMENT.md`) has no bearing on the
project's real performance numbers.

## Phase 5 — event-driven backtester

### The market-impact question, and why the answer reuses `Adapter<Engine>`

The central design question for any LOB backtester: once a strategy's
own orders enter the book, does the replay still have to match history?
Two answers exist. (a) *No market impact*: keep the strategy's orders
out of the real matching engine entirely and approximate its fills
against the historical trade tape (e.g. "the historical trade at this
price/quantity would have also filled my resting order, assuming I was
behind in queue"). This keeps the replay identical to LOBSTER's own
orderbook file forever, but the fill approximation is a guess, not a
match result. (b) *Real participation*: submit the strategy's orders to
the exact same `MatchingEngine` instance that the rest of the market's
order flow goes through. Fills are then real match results, not
approximations — but the moment the strategy trades, subsequent book
state can diverge from LOBSTER's historical orderbook file, since the
strategy is now a real participant history didn't include.

This project picks **(b)**, for a reason specific to what already
exists: `tools/lobster/adapter.hpp`'s `Adapter<Engine>` already
converts every LOBSTER message row into a real `new_order`/`cancel`/
`modify` call against a live `MatchingEngine<Book>` — that's exactly
"replay the market as real order flow," which Phase 2 already built and
validated. The backtester reuses `Adapter<Engine>` unmodified as the
*other-market-participants* feed, and interleaves the strategy's own
order submissions into the same engine, in timestamp order. Divergence
from LOBSTER's orderbook file after the strategy's first fill is
expected and fine: Phase 5 isn't re-running Phase 2's validation gate
(that only ever applies to the untouched replay), it's using LOBSTER as
a source of realistic *other-participant* flow to backtest against, not
a target the strategy must reproduce.

Order-ID collision is already a solved problem here too:
`Adapter::NextSyntheticId()` exists precisely to mint IDs that can't
collide with real LOBSTER order IDs (for type-4 rows' synthesized
aggressor). The strategy's order IDs reuse that same disjoint range.

### Strategy interface

A `Strategy` is an abstract base (virtual dispatch, not a template
parameter like `Book` or `Engine`) — unlike the `Book` axis, which is a
compile-time performance optimization, the strategy is user-supplied
*behavior* meant to be swapped freely, including at a point in the
roadmap (Phase 6) where more than one example strategy is expected to
exist side by side. It gets two callbacks:

- `OnMarketEvent(const std::vector<Event>&)` — fired after each LOBSTER
  row is applied, before the strategy reacts. Gives the strategy the
  resulting events and read access to the current book, so it can
  decide whether to act.
- `OnOwnEvent(const Event&)` — fired for every event resulting from the
  strategy's own order submissions (accepted, filled, cancelled, ...).

Order submission itself goes through a small `OrderPort` handle (Buy/
Sell/Cancel/Modify) passed into the strategy rather than exposing the
engine directly — the strategy should not be able to inspect or bypass
the same request validation every other participant is subject to.

**Caught while writing the first real example strategy:** the initial
cut of this interface gave a `Strategy` the resulting events and nothing
else — no way to ask "what's the market trading at right now." Any
price-aware decision (quote around the touch, only trade when the
spread is tight, ...) needs that, so `OrderPort` also exposes
`BestPrice(Side)`, backed by the same `engine_.book().best_price()`
every other read path already uses. Recorded here because it's a good
example of *why* "write the demo before calling the slice done" is part
of the loop — the gap wasn't visible until something tried to use the
interface for real.

### Portfolio and P&L

A `Portfolio` tracks the strategy's own cash (one shared account) and
position (per instrument — see multi-instrument below), updated off
`Trade` events where the strategy owns `resting_id` or `aggressor_id`.
Mark-to-market P&L (`cash + sum of position*mark_price` across held
instruments) can be computed at any point from the live book(s);
realized P&L is the same formula at the run's end.

**Max drawdown and per-sample "Sharpe"**: `Portfolio::RecordMarkToMarket`
takes one running total-portfolio mark-to-market value per replayed row
(computed by the backtester driver, which has book access) and folds it
into O(1) streaming stats — a running peak for drawdown, Welford's
algorithm for the mean/variance of period-to-period deltas — rather
than storing the full history, since a real replay can be hundreds of
thousands of rows. `max_drawdown()` is a real, honest number: worst
peak-to-current decline actually seen. `sharpe_ratio_per_sample()` is
deliberately NOT called a Sharpe ratio in its name or its doc comment:
a real Sharpe ratio needs independent, periodic (e.g. daily) returns
and annualization; the deltas here are between whatever rows the
backtester recorded (every replayed LOBSTER row within one session),
which are neither independent nor a fixed period. Reporting it as an
annualized Sharpe ratio would be exactly the kind of fabricated number
this project's rules don't allow — so it's named for what it actually
is (a directional mean/stddev ratio, useful for comparing two runs
against the same replay) and nothing more.

**Risk limits**: `RiskLimits` (`include/riptide/risk_limits.hpp`) is an
optional, per-instrument pre-trade control `OrderPort` enforces on the
strategy's own submissions — `max_order_quantity` (a single order's own
size) and `max_abs_position` (checked against the position the order
would leave assuming it fills completely, not whether it actually
does). A breach means the order never reaches the engine at all: no
Accepted, no Portfolio update, no `Strategy::OnOwnEvent` call — as if
it was never submitted. This sits on top of, not instead of,
`MatchingEngine`'s own request validation (which nothing can bypass
either way) — it represents a strategy's own risk desk, not an
exchange rule.

**Multi-instrument**: order IDs are only unique WITHIN one instrument
(each has its own engine/book), so `Portfolio`, `Strategy`, and
`OrderPort` are all keyed/parameterized by `InstrumentId` (a ticker
string) throughout — `Portfolio::position(instrument)`,
`OrderPort::Buy(instrument, ...)`, `Strategy::OnMarketEvent(instrument,
...)`. `MultiInstrumentBacktester<Engine>` (a new class alongside the
single-instrument `Backtester<Engine>`, sharing the same
Strategy/OrderPort/Portfolio types since they're already
instrument-aware) drives several LOBSTER file pairs at once, replaying
rows in TRUE chronological order across instruments — the earliest
pending timestamp across all open files goes next, not one file
replayed to completion before the next starts. Each instrument keeps
its own `Engine` and `lobster::Adapter<Engine>` (a `std::vector` of
`unique_ptr<InstrumentState<Engine>>`, not a hash map, specifically so
`Adapter`'s `Engine&` reference member is never at risk from a
container rehash/move — each instrument's state is constructed once,
in place, and never relocated). Verified against two real files at
once (AAPL + AMZN): both tickers' documented Phase 2 baselines still
hold when replayed together, and both tickers are provably interleaved
(each appears at least once before the other is exhausted), not
replayed sequentially.

### First slice vs. deferred

First slice (now including all of the above): one or several
instruments, one `Strategy` at a time, LOBSTER files as the
market-flow source via `Adapter<Engine>`, cash+per-instrument-position
P&L with drawdown/per-sample-Sharpe, optional risk limits, plain
end-of-run summary. Test plan mirrors Phase 1's philosophy: unit tests
for `Portfolio` arithmetic against hand-computed fills (including
cross-instrument attribution and the drawdown/Sharpe stats), plus a
regression check that a strategy which never trades reproduces exactly
the same book trajectory Phase 2 already validates (confirms the
backtester's reuse of `Adapter<Engine>` doesn't itself change engine
behavior).

`tools/backtest` is the CLI tool analogous to `tools/validate`: it runs
one example strategy (`RestAtBestBidStrategy` — rests a single GTC buy
at the current best bid the first time it observes the market, then
does nothing else; honestly documented as a machinery demo, not a real
trading strategy) against a real LOBSTER file and prints fills/P&L/
drawdown/Sharpe, with `--max-position`/`--max-order-quantity` flags for
RiskLimits. Run against real AAPL/AMZN/MSFT data, it reproduces exactly
what the design predicts: a handful of real fills with sane,
sanity-checkable P&L (e.g. buying 100 AAPL shares near the touch and
marking a few rows later at +$2), then the adapter reports the
*expected* divergence from LOBSTER's own orderbook file once the
strategy's resting order has actually changed the book's history — the
honest tradeoff of picking "real participation" over tape-approximated
fills (see above). `tools/backtest`'s CLI itself stays single-instrument
for now — `MultiInstrumentBacktester` is real, tested against real
data, and used directly in `tests/backtester_test.cpp`, but wiring a
multi-file CLI mode is left as a small, well-scoped follow-up rather
than done speculatively here.

Deferred (not built yet, in rough expected-value order):
queue-position-aware resting-order fill realism (a resting order's
place in the FIFO queue relative to pre-existing/synthetic bucket
orders isn't modeled beyond what `Adapter` already does for Phase 2),
config-driven strategy parameters/selection in the CLI, a
multi-instrument CLI mode.
