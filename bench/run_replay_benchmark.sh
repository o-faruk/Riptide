#!/usr/bin/env bash
set -euo pipefail

# Runs riptide_bench_replay N independent times against the same message
# file and reports the median and IQR (Q1-Q3) of each statistic across
# runs. This is the project's statistical discipline requirement in
# practice: a single run's numbers are never reported on their own,
# because a single run can be skewed by transient system noise -- this
# matters especially on a laptop with no isolated cores and frequency
# scaling under thermal load (see bench/ENVIRONMENT.md).
#
# Usage: bench/run_replay_benchmark.sh <riptide_bench_replay path> <message.csv> [num_runs=10]

BINARY="${1:?Usage: $0 <riptide_bench_replay path> <message.csv> [num_runs]}"
MESSAGE_FILE="${2:?Usage: $0 <riptide_bench_replay path> <message.csv> [num_runs]}"
NUM_RUNS="${3:-10}"

if [[ ! -x "$BINARY" ]]; then
  echo "ERROR: $BINARY is not an executable file" >&2
  exit 1
fi
if [[ ! -f "$MESSAGE_FILE" ]]; then
  echo "ERROR: $MESSAGE_FILE not found" >&2
  exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "Running ${NUM_RUNS} independent runs of:"
echo "  ${BINARY} ${MESSAGE_FILE}"
echo

for i in $(seq 1 "$NUM_RUNS"); do
  echo "  run ${i}/${NUM_RUNS}..."
  "$BINARY" "$MESSAGE_FILE" > "${TMP_DIR}/run_${i}.log" 2>/dev/null
done

echo
echo "=== Percentiles: median [Q1, Q3] across ${NUM_RUNS} runs ==="
echo

# Pull just the SUMMARY_CSV block's 4 data rows (fixed 8 columns:
# category,count,p50,p90,p99,p999,p9999,max; header and the trailing
# THROUGHPUT_MSG_PER_SEC line excluded) out of every run's log,
# concatenated, then let awk group by category and compute median/IQR
# per column. All N files share this fixed shape, which is what keeps
# this script simple.
for i in $(seq 1 "$NUM_RUNS"); do
  awk '
    /^SUMMARY_CSV$/ { flag = 1; header_skipped = 0; next }
    /^THROUGHPUT_MSG_PER_SEC/ { flag = 0 }
    flag && !header_skipped { header_skipped = 1; next }
    flag { print }
  ' "${TMP_DIR}/run_${i}.log"
done > "${TMP_DIR}/all_summaries.csv"

for category in all new cancel modify; do
  awk -F',' -v cat="$category" '$1 == cat {print}' "${TMP_DIR}/all_summaries.csv" > "${TMP_DIR}/${category}.csv"

  echo "-- ${category} --"
  for col_idx in 2 3 4 5 6 7 8; do
    case $col_idx in
      2) col_name="count" ;;
      3) col_name="p50" ;;
      4) col_name="p90" ;;
      5) col_name="p99" ;;
      6) col_name="p999" ;;
      7) col_name="p9999" ;;
      8) col_name="max" ;;
    esac

    cut -d',' -f"$col_idx" "${TMP_DIR}/${category}.csv" | sort -n > "${TMP_DIR}/sorted_col.txt"
    n=$(wc -l < "${TMP_DIR}/sorted_col.txt")

    awk -v col="$col_name" -v n="$n" '
      { values[NR] = $1 }
      END {
        median_idx = int((n + 1) / 2)
        q1_idx = int((n + 3) / 4)
        q3_idx = int((3 * n + 1) / 4)
        if (median_idx < 1) median_idx = 1
        if (q1_idx < 1) q1_idx = 1
        if (q3_idx < 1) q3_idx = 1
        printf "  %-8s median=%-10s [%s, %s]\n", col, values[median_idx], values[q1_idx], values[q3_idx]
      }
    ' "${TMP_DIR}/sorted_col.txt"
  done
  echo
done

echo "=== Throughput: median [Q1, Q3] across ${NUM_RUNS} runs (msg/s) ==="
for i in $(seq 1 "$NUM_RUNS"); do
  awk '/^THROUGHPUT_MSG_PER_SEC/{print $2}' "${TMP_DIR}/run_${i}.log"
done | sort -n > "${TMP_DIR}/throughput.txt"

n=$(wc -l < "${TMP_DIR}/throughput.txt")
awk -v n="$n" '
  { values[NR] = $1 }
  END {
    median_idx = int((n + 1) / 2)
    q1_idx = int((n + 3) / 4)
    q3_idx = int((3 * n + 1) / 4)
    if (median_idx < 1) median_idx = 1
    if (q1_idx < 1) q1_idx = 1
    if (q3_idx < 1) q3_idx = 1
    printf "  median=%s [%s, %s]\n", values[median_idx], values[q1_idx], values[q3_idx]
  }
' "${TMP_DIR}/throughput.txt"
