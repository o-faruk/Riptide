# Benchmark Environment

Every number under `bench/` is meaningless without this document next to
it. Read this before trusting (or citing) any latency/throughput figure
in this repository.

## This session's coding machine is not the measurement machine

This project's code was developed in a session running on **Apple M3 Pro
/ macOS** (`arm64`). The project's actual target machine — the one every
"official" benchmark number in this repo must come from — is the laptop
described in the project brief: **Acer Nitro 5, Ubuntu Server 24.04 LTS,
8 GB RAM**, a consumer x86_64 laptop CPU with frequency scaling and no
isolated cores.

These are architecturally different machines (ARM vs. x86_64, a fanless-
tuned Apple SoC vs. a budget gaming laptop CPU under a thin-and-light
chassis). Any timing captured on the macOS dev machine has **no bearing**
on the project's real performance claims and must never be reported as
if it were a project benchmark result — that would be exactly the kind
of fabricated-looking number the project's own rules prohibit. Numbers
produced on the dev machine during development are labeled "smoke test"
in commit messages and this document, never "baseline" or "result."

## Reproducing on the real target machine

```sh
git pull                                    # or clone fresh
cmake --preset release
cmake --build --preset release
./bench/collect_environment.sh > bench/results/environment_$(date +%Y%m%d).txt
./bench/run_replay_benchmark.sh ./build/release/bench/riptide_bench_replay \
    data/AAPL/AAPL_2012-06-21_34200000_57600000_message_10.csv 10 \
    | tee bench/results/replay_baseline_$(date +%Y%m%d).txt
```

`data/` needs the LOBSTER sample fetched first: `tools/fetch_data.sh AAPL`
(see that script and `docs/DESIGN.md` for why it isn't committed).

Optional, for the most controlled run (needs root, Linux only):

```sh
sudo bench/pin_and_run.sh 0 -- ./build/release/bench/riptide_bench_replay data/AAPL/...
```

## Methodology

**Timer: `std::chrono::steady_clock`, not `rdtscp`.** Two reasons: (1)
this project is developed on ARM64 macOS and benchmarked on x86_64 Linux
— `rdtscp` is an x86 intrinsic and wouldn't even compile on the dev
machine, so it can't be smoke-tested before a real run; (2) on modern
Linux, `clock_gettime(CLOCK_MONOTONIC)` (what `steady_clock` calls) is
itself typically served by a TSC-backed vDSO fast path, so it doesn't
give up much precision for the portability. The harness measures and
discloses its own overhead rather than assuming it away — see
`CalibrateTimerOverheadNs()` in `bench/replay_harness.cpp`: it calls
`now()` back-to-back 200,000 times and reports the median delta, which is
then subtracted from every reported latency. If `rdtscp` is ever adopted
for Phase 4's tighter hot-path work, it needs the same disclosed-overhead
treatment, plus a calibrated cycles→ns conversion and a check that the
target CPU's TSC is invariant across power states.

**Warm-up and cold start are reported separately, not conflated.**
`bench/replay_harness.cpp` times the first 200 applied messages
individually right after engine construction ("cold start" — cold
caches, untrained branch predictor) as its own distribution, then applies
(but does not time) the next 50,000 messages as warm-up before steady-
state measurement begins. 50,000 was chosen to be comfortably larger than
any plausible cache/branch-predictor warm-up horizon while staying small
relative to a full day's ~200K–600K messages, so it doesn't meaningfully
shrink the measurement window. Both constants are named and commented at
the top of `replay_harness.cpp`, not buried magic numbers.

**Percentiles are nearest-rank, not interpolated**: p50/p90/p99/p99.9/
p99.99/max, computed by `bench/stats.cpp` (unit-tested in
`tests/stats_test.cpp`). See that file's header comment for the exact
definition.

**Statistical discipline**: `bench/run_replay_benchmark.sh` runs the
replay harness 10 independent times (separate process launches, so OS
scheduling/heap layout/thermal state vary between them the way they
would for any real invocation) and reports the **median with IQR (Q1–Q3)**
per statistic across those runs — never a single run's numbers, and never
just the best run.

**Two separate tools measure two separate things, deliberately not one
conflated measurement:**
- `bench/replay_harness.cpp` replays a real LOBSTER message file directly
  through the engine (no adapter/bucket translation overhead — see that
  file's header) to get realistic New/Cancel/Modify latency and full-day
  throughput.
- `bench/micro_benchmarks.cpp` (Google Benchmark, built only with
  `-DRIPTIDE_BUILD_BENCH=ON`) measures crossing cost under a controlled,
  synthetic sweep — necessary because a real LOBSTER type-1 row only ever
  shows an order's *resting* remainder (see `docs/DESIGN.md`), so replaying
  real data essentially never exercises the crossing path at all.

**Build flags**: the `release` CMake preset (`-O3 -DNDEBUG` via
`CMAKE_BUILD_TYPE=Release`, no exotic flags). `-march=native` and LTO are
deliberately *not* baked into the checked-in presets — `-march=native`
ties the binary to the exact CPU it was compiled on, which would silently
break reproducibility from a clean clone on a different machine. If a
specific run uses `-march=native` (e.g.
`cmake --preset release -DCMAKE_CXX_FLAGS="-march=native"`), that must be
disclosed next to the numbers it produced. LTO isn't enabled yet; if
Phase 4 finds it matters, that belongs in `docs/OPTIMIZATION_LOG.md` as a
measured before/after, not silently folded into the Phase 3 baseline.

**Always benchmark `release` (or `relwithdebinfo`), never `debug`.**
Google Benchmark itself warns about this (`***WARNING*** Library was
built as DEBUG`); the difference was ~2x on the dev machine during smoke
testing, which is exactly the kind of thing that would misrepresent the
engine if reported.

## Known limitations of the target machine (disclose, don't hide)

- **No isolated cores.** `bench/pin_and_run.sh` pins to a single core via
  `taskset`, but the OS scheduler still shares every other core with
  normal system load; this isn't a tuned trading box with `isolcpus`.
- **Frequency scaling.** `pin_and_run.sh` sets the `performance` governor
  for the pinned core when it can (needs root), but consumer laptop
  firmware still makes independent thermal/power decisions the governor
  setting doesn't fully override.
- **Thermal throttling under sustained load.** A laptop chassis will
  throttle under repeated benchmark runs in a way a desktop or server
  wouldn't. This is exactly what the 10-run median/IQR discipline is for
  — report the spread, don't pretend it isn't there.
- **8 GB RAM.** Not usually a benchmark-accuracy concern for a single
  day's LOBSTER replay, but relevant context for why this project
  streams data rather than loading full days into memory anywhere.

## Results

No baseline has been captured on the target machine yet. Once
`bench/run_replay_benchmark.sh` and `bench/collect_environment.sh` have
been run on the actual Ubuntu laptop, their output belongs here (or in
`bench/results/`, referenced from this section) — committed as the
literal, unedited tool output, not retyped or summarized, so the number
in the README always traces back to a command that was actually run.
