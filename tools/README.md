# tools/

- `fetch_data.sh` — downloads LOBSTER sample data into `data/` (gitignored,
  never committed — see the script for why) and verifies message/orderbook
  row counts match.
- `lobster/` — LOBSTER message/orderbook CSV parsing and `Adapter<Engine>`,
  which translates LOBSTER's reconstructed-event-stream semantics into
  `MatchingEngine` calls. Shared by `validate/`, `backtest/`, and `bench/`.
  See `docs/DESIGN.md` for the adapter design.
- `validate/` — streams a message/orderbook file pair in lockstep, replays
  the messages through a `MatchingEngine`, and compares the resulting book
  against LOBSTER's reference after every row. See `docs/DESIGN.md` for the
  validation gate's methodology.
- `backtest/` — Phase 5's demo CLI: runs an example `Strategy` against a
  real LOBSTER file through `Backtester<Engine>` and prints fills/P&L/
  drawdown/Sharpe. See `docs/DESIGN.md`'s Phase 5 section and
  `backtest/backtester.hpp`.
