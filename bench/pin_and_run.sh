#!/usr/bin/env bash
set -euo pipefail

# Pins a command to a single core and sets that core's frequency-scaling
# governor to "performance" before running it, restoring the previous
# governor afterward. Linux only (taskset, /sys/devices/system/cpu) --
# this is the project's actual measurement environment (an Ubuntu
# laptop), not a dev machine.
#
# The governor change needs root (sudo). If you'd rather not grant that,
# comment out the governor section below and keep just the taskset
# pinning -- then disclose in bench/ENVIRONMENT.md that the governor
# wasn't pinned for that run. Never silently drop it and report the
# numbers as if it had been.
#
# Note even with pinning and the performance governor, a laptop under
# sustained load will still thermal-throttle -- this script controls what
# it can control, it doesn't eliminate that. Run repeated trials (see
# bench/run_replay_benchmark.sh) and report the run-to-run variance
# rather than pretending it isn't there.
#
# Usage: bench/pin_and_run.sh <core_id> -- <command> [args...]

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "ERROR: this script requires Linux (taskset, /sys cpufreq) -- run it" >&2
  echo "on the project's actual target machine, not a dev box." >&2
  exit 1
fi

if [[ $# -lt 3 || "$2" != "--" ]]; then
  echo "Usage: $0 <core_id> -- <command> [args...]" >&2
  exit 1
fi

CORE_ID="$1"
shift 2  # drop core_id and --

GOVERNOR_PATH="/sys/devices/system/cpu/cpu${CORE_ID}/cpufreq/scaling_governor"
ORIGINAL_GOVERNOR=""

if [[ -f "$GOVERNOR_PATH" ]]; then
  ORIGINAL_GOVERNOR="$(cat "$GOVERNOR_PATH")"
  echo "Setting cpu${CORE_ID} governor: ${ORIGINAL_GOVERNOR} -> performance"
  echo performance | sudo tee "$GOVERNOR_PATH" >/dev/null
else
  echo "WARNING: $GOVERNOR_PATH not found -- running with whatever governor" >&2
  echo "is currently active. Disclose this in bench/ENVIRONMENT.md alongside" >&2
  echo "the results." >&2
fi

restore_governor() {
  if [[ -n "$ORIGINAL_GOVERNOR" ]]; then
    echo "Restoring cpu${CORE_ID} governor: performance -> ${ORIGINAL_GOVERNOR}"
    echo "$ORIGINAL_GOVERNOR" | sudo tee "$GOVERNOR_PATH" >/dev/null
  fi
}
trap restore_governor EXIT

echo "Pinning to core ${CORE_ID}: taskset -c ${CORE_ID} $*"
taskset -c "$CORE_ID" "$@"
