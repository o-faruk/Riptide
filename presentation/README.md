# Presentation assets

`console.html` is a static, self-contained HTML page (open it directly in
a browser — no build step, no server) styled as a trading-terminal
console. Every number on it is real and traceable to a specific file in
this repository — nothing is randomly generated or simulated, unlike a
typical "live dashboard" mockup. Provenance for each figure:

| On the page | Source |
|---|---|
| Throughput (3,675,918 msg/s), latency percentiles (p50/p90/p99/p99.9/p99.99/max) | `bench/ENVIRONMENT.md`'s AAPL results table, itself sourced from `bench/results/replay_baseline_20260720_AAPL.txt` |
| Order book depth (row 5000) | `data/AAPL/AAPL_2012-06-21_34200000_57600000_orderbook_10.csv`, line 5000 (fetch the data first via `tools/fetch_data.sh AAPL` to reproduce) |
| Trade tape | `data/AAPL/AAPL_2012-06-21_34200000_57600000_message_10.csv`, the first several type-4 (visible execution) rows, decoded per `tools/lobster/lobster_message.hpp`'s field semantics |
| LOBSTER validation gate (11 / 400,391 rows) | `docs/DESIGN.md`'s Phase 2 section — the documented, honest ceiling on full-file validation, not an engine defect |
| Phase 4 optimization result (−14.4% at sweep depth 100) | `docs/OPTIMIZATION_LOG.md`, entry 1 |
| Phase 5 demo strategy result (3 fills, +$2.00 mark-to-market) | An actual `tools/backtest/riptide_backtest_cli` run against real AAPL data (see `docs/DESIGN.md`'s Phase 5 section for the full output) |

If any of these source numbers change (a re-run on updated data, a new
optimization entry, a new benchmark), `console.html` needs a manual
update to match — it is not wired to regenerate itself, by design: a
static snapshot with an explicit provenance table is easier to audit
than a live page silently pulling from files that could drift.
