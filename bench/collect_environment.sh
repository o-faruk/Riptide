#!/usr/bin/env bash
set -euo pipefail

# Collects the environment details Phase 3 requires disclosing alongside
# any benchmark numbers: CPU model, core count, cache sizes, SMT status,
# frequency-scaling governor, kernel/OS, compiler version, build flags.
#
# Run this on the SAME machine and SAME build directory you run the
# benchmarks in, and keep its output alongside the results -- a latency
# number is close to meaningless without this context next to it.

echo "=== Riptide benchmark environment ==="
echo "Collected: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
echo

if [[ "$(uname -s)" == "Linux" ]]; then
  echo "--- OS ---"
  uname -a
  [[ -f /etc/os-release ]] && grep -E "^(NAME|VERSION)=" /etc/os-release
  echo

  echo "--- CPU ---"
  if command -v lscpu >/dev/null 2>&1; then
    lscpu
  else
    grep -m1 "model name" /proc/cpuinfo
    echo "logical CPUs: $(nproc)"
  fi
  echo

  echo "--- SMT / Hyperthreading ---"
  if [[ -f /sys/devices/system/cpu/smt/active ]]; then
    echo "SMT active: $(cat /sys/devices/system/cpu/smt/active) (1 = on, 0 = off)"
  else
    echo "SMT status file not found (older kernel, or not applicable to this CPU)"
  fi
  echo

  echo "--- Frequency scaling governor (per core) ---"
  found_governor=0
  for gov_file in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [[ -f "$gov_file" ]] || continue
    found_governor=1
    echo "${gov_file}: $(cat "$gov_file")"
  done
  [[ "$found_governor" -eq 0 ]] && echo "No cpufreq scaling_governor files found"
  echo

  echo "--- Cache sizes (cpu0) ---"
  for cache_dir in /sys/devices/system/cpu/cpu0/cache/index*/; do
    [[ -d "$cache_dir" ]] || continue
    level=$(cat "${cache_dir}level" 2>/dev/null || echo "?")
    type=$(cat "${cache_dir}type" 2>/dev/null || echo "?")
    size=$(cat "${cache_dir}size" 2>/dev/null || echo "?")
    echo "L${level} ${type}: ${size}"
  done
  echo

  echo "--- Thermal (best effort) ---"
  if command -v sensors >/dev/null 2>&1; then
    sensors
  else
    echo "lm-sensors not installed (apt install lm-sensors); skipping temperature readout"
  fi
else
  echo "!!! This script targets Linux -- the project's actual measurement"
  echo "!!! environment (an Ubuntu laptop), not whatever you're running"
  echo "!!! this on ($(uname -s)). Only limited, best-effort info follows;"
  echo "!!! this is NOT a substitute for collecting on the real machine."
  echo
  echo "--- CPU (best effort) ---"
  sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "unknown"
  echo "physical CPUs: $(sysctl -n hw.physicalcpu 2>/dev/null || echo '?')"
  echo "logical CPUs: $(sysctl -n hw.logicalcpu 2>/dev/null || echo '?')"
  echo
  echo "--- OS ---"
  sw_vers 2>/dev/null || uname -a
fi

echo
echo "--- Compiler ---"
"${CXX:-c++}" --version | head -1

echo
echo "--- Build flags (from CMakeCache.txt, if present) ---"
for build_dir in build/release build/relwithdebinfo build/debug build/asan build/ubsan build/tsan; do
  [[ -f "${build_dir}/CMakeCache.txt" ]] || continue
  echo "[${build_dir}]"
  grep -E "^CMAKE_(BUILD_TYPE|CXX_FLAGS):" "${build_dir}/CMakeCache.txt" || true
done
