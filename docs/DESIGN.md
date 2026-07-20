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
