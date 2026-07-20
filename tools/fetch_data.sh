#!/usr/bin/env bash
set -euo pipefail

# Downloads LOBSTER free sample data (message + orderbook file pairs) into
# data/, which is gitignored — this data must NEVER be committed. The site's
# terms of use (php.lobsterdata.com/imprint.php) prohibit "systematic
# downloading of materials from this website for collections, archives,
# directories or databases," which a git repo would be. So this script
# re-fetches fresh every time instead of the repo ever holding a copy.
#
# Source: the legacy static subdomain php.lobsterdata.com. The primary
# lobsterdata.com site has been rebuilt as a gated commercial portal; this
# subdomain still serves the free samples directly and unauthenticated as
# of 2026-07, but it's a legacy artifact that could be locked down without
# notice — don't assume this URL is stable long-term.
#
# Usage:
#   tools/fetch_data.sh [TICKER...]
#   LOBSTER_LEVEL=5 tools/fetch_data.sh AAPL GOOG
#
# Defaults to AAPL, AMZN, MSFT at level 10 (>=3 tickers, matching Phase 2's
# validation gate requirement) if no tickers are given.

BASE_URL="https://php.lobsterdata.com/info/sample"
DATE="2012-06-21"  # the only date free samples are published for
LEVEL="${LOBSTER_LEVEL:-10}"
DATA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/data"

if [[ $# -gt 0 ]]; then
  TICKERS=("$@")
else
  TICKERS=(AAPL AMZN MSFT)
fi

mkdir -p "$DATA_DIR"

for ticker in "${TICKERS[@]}"; do
  zip_name="LOBSTER_SampleFile_${ticker}_${DATE}_${LEVEL}.zip"
  url="${BASE_URL}/${zip_name}"
  dest_dir="${DATA_DIR}/${ticker}"
  mkdir -p "$dest_dir"

  echo "==> Fetching ${ticker} (level ${LEVEL}, ${DATE})"
  curl -fSL "$url" -o "${dest_dir}/${zip_name}"

  echo "==> Unzipping"
  unzip -oq "${dest_dir}/${zip_name}" -d "$dest_dir"
  rm "${dest_dir}/${zip_name}"

  message_file=$(find "$dest_dir" -maxdepth 1 -name "*_message_*.csv" | head -n1)
  orderbook_file=$(find "$dest_dir" -maxdepth 1 -name "*_orderbook_*.csv" | head -n1)

  if [[ -z "$message_file" || -z "$orderbook_file" ]]; then
    echo "ERROR: expected a message/orderbook CSV pair for ${ticker}, found none" >&2
    exit 1
  fi

  # LOBSTER guarantees row k of orderbook == book state after row k of
  # message (see ReadMe.txt), so a line-count mismatch means a corrupted
  # or partial download, not a legitimate data variation.
  message_lines=$(wc -l < "$message_file")
  orderbook_lines=$(wc -l < "$orderbook_file")

  if [[ "$message_lines" -ne "$orderbook_lines" ]]; then
    echo "ERROR: ${ticker} message file has ${message_lines} lines but" \
         "orderbook file has ${orderbook_lines} — expected an exact match," \
         "this looks like a corrupted download" >&2
    exit 1
  fi

  echo "OK: ${ticker} — ${message_lines} rows, message/orderbook line counts match"
done
