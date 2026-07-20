# tools/

- `fetch_data.sh` — downloads LOBSTER sample data into `data/` (gitignored,
  never committed — see the script for why) and verifies message/orderbook
  row counts match.
- `validate/` — streams a message/orderbook file pair in lockstep, replays
  the messages through a `MatchingEngine`, and compares the resulting book
  against LOBSTER's reference after every row. See `docs/DESIGN.md` for the
  adapter design and the validation gate's methodology.
