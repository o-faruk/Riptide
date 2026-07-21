# Benchmark Environment

Every number under `bench/` is meaningless without this document next to
it. Read this before trusting (or citing) any latency/throughput figure
in this repository.

## This session's coding machine is not the measurement machine

This project's code is developed in a session running on **Apple M3 Pro /
macOS** (`arm64`). The project's original target machine was an Acer
Nitro 5 laptop (Ubuntu Server 24.04, 8 GB RAM) — that laptop has since
broken and is no longer available. **The current target machine is a
desktop**: AMD Ryzen 5 7600X (6 cores / 12 threads, 32 MB L3), accessed
via WSL2 (Ubuntu 26.04) on Windows, ~15 GB RAM visible inside WSL2. Full
detail in `bench/results/environment_20260720.txt`.

This is a real change in what "the target machine" means for the
project, not a cosmetic one: a desktop CPU has thermal headroom a laptop
chassis doesn't, but WSL2 sits between the benchmark and the bare metal
(see Known Limitations below) in a way plain Ubuntu-on-a-laptop wouldn't
have. Both are disclosed, not glossed over.

Regardless of which machine is "the" target at any point, the macOS dev
machine's numbers are never valid substitutes for it — different
architecture entirely (ARM vs. x86_64). Numbers produced on the dev
machine during development are labeled "smoke test" in commit messages
and this document, never "baseline" or "result."

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

Optional, for the most controlled run (needs root, Linux only — and see
Known Limitations below for why this does less under WSL2 than on bare
metal):

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

- **WSL2 virtualization sits between the benchmark and the bare metal.**
  This is a real hypervisor layer (Hyper-V), not a lightweight
  namespace/chroot — confirmed by `collect_environment.sh` itself:
  `Hypervisor vendor: Microsoft`, `Virtualization type: full`. Whatever
  Windows' own scheduler is doing with the host's 12 logical CPUs at any
  given moment can affect what WSL2's guest kernel sees, in a way bare-
  metal Linux wouldn't have.
- **No CPU frequency governor exposed at all.** Confirmed directly:
  `collect_environment.sh` reports "No cpufreq scaling_governor files
  found." `bench/pin_and_run.sh` degrades gracefully (warns and proceeds
  without setting a governor) rather than failing, but this means there
  is currently no way to force a `performance` governor for a run on this
  machine — a strictly weaker guarantee than the bare-metal-Linux case
  the script was originally written for.
- **`taskset` pinning still works** (WSL2's kernel is real Linux), but
  pinning to a WSL2-visible core number doesn't guarantee the same
  physical host core stays assigned to it for the run's duration — that's
  ultimately Hyper-V's call, not ours.
- **Thermal throttling is less of a concern than the original laptop
  framing** — this is a desktop CPU, not a battery-powered chassis — but
  hasn't been independently confirmed (`lm-sensors` isn't installed; see
  `collect_environment.sh` output). The 10-run median/IQR discipline
  covers this regardless of cause: report the spread, don't assume it
  away.
- **~15 GB RAM visible to WSL2.** Comfortably more than the original 8 GB
  figure this project was scoped around. Doesn't change the streaming
  design (still correct practice for realistic multi-symbol/multi-day
  data volumes), just means memory pressure isn't a live concern on this
  particular machine for a single day's LOBSTER replay.

## Results

`bench/results/` holds the literal, unedited output of every command in
"Reproducing on the real target machine" above, run on the machine
described at the top of this document. All three of Phase 2's validated
tickers (AAPL, AMZN, MSFT), 2012-06-21, level-10 sample, `release`
build, 10 independent runs each, median [Q1, Q3]:

**AAPL** (`replay_baseline_20260720_AAPL.txt`, n=315,088):

| Category | p50 | p90 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|
| all | 70ns | 130ns | 261ns | 561ns | 9157ns | 43593ns |
| new | 81ns | 150ns | 321ns | 662ns | 9709ns | 39350ns |
| cancel | 60ns | 91ns | 151ns | 261ns | 1323ns | 38614ns |
| modify | 41ns | 90ns | 170ns | 251ns | 391ns | 391ns |

Throughput: **3,675,918 msg/s** [3,610,728, 3,703,745]

**AMZN** (`replay_baseline_20260721_AMZN.txt`, n=207,892):

| Category | p50 | p90 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|
| all | 60ns | 100ns | 171ns | 401ns | 5481ns | 28945ns |
| new | 71ns | 111ns | 211ns | 522ns | 7575ns | 28444ns |
| cancel | 50ns | 71ns | 110ns | 180ns | 932ns | 27031ns |
| modify | 40ns | 60ns | 130ns | 180ns | 241ns | 241ns |

Throughput: **4,325,952 msg/s** [4,236,604, 4,353,588]

**MSFT** (`replay_baseline_20260721_MSFT.txt`, n=584,635):

| Category | p50 | p90 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|
| all | 50ns | 81ns | 161ns | 521ns | 1943ns | 30548ns |
| new | 60ns | 91ns | 231ns | 651ns | 3637ns | 23925ns |
| cancel | 40ns | 60ns | 91ns | 160ns | 381ns | 19597ns |
| modify | 40ns | 60ns | 130ns | 191ns | 1373ns | 1373ns |

Throughput: **4,577,080 msg/s** [4,565,813, 4,595,235]

All three are New/Cancel/Modify latencies measured directly through
`MatchingEngine`, deliberately not through the LOBSTER-correctness
adapter (see `bench/replay_harness.cpp`'s header). Consistent shape
across tickers (cancel cheapest, new most expensive, modify in between
but on very few samples — modify is rare in this data, n ≈ 2,700–4,800
vs. hundreds of thousands for new/cancel) and consistent order-of-
magnitude p50s (40–80ns) despite MSFT having ~2.8x AAPL's message count,
which is a reasonable sanity check that these numbers reflect real,
stable engine behavior rather than a one-off fluke.

**Crossing cost** (`micro_benchmarks_20260721.txt`, Google Benchmark,
`-DRIPTIDE_BUILD_BENCH=ON`, single run — see that file for why it isn't
put through the 10-run wrapper):

| Sweep depth | Time/call | Per-order (items/s) |
|---|---|---|
| 1 | 468ns | 2.16M/s |
| 10 | 786ns | 12.7M/s |
| 50 | 2663ns | 18.8M/s |
| 100 | 5097ns | 19.6M/s |

Per-order cost *drops* as sweep depth grows (fixed per-call overhead —
validation, sequence assignment, event vector setup — amortizing over
more fills), which is exactly the kind of thing Phase 4's profiling
should confirm or correct with real cache-miss/branch-miss data before
touching anything. `BM_OrderBookInsert` (459ns) and
`BM_EngineNewOrderResting` (471ns) are close, as expected — resting is
dominated by the same map/list insert `OrderBookInsert` measures
directly.
