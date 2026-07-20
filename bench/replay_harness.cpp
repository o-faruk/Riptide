// Custom end-to-end replay benchmark: streams a real LOBSTER message file
// directly through MatchingEngine (no LOBSTER-correctness machinery --
// see the note above ApplyForBenchmark) and reports per-message latency
// distributions and throughput.
//
// Deliberately NOT using tools/lobster's Adapter here: Adapter's job is
// reproducing LOBSTER's exact reconstructed semantics (bucket redirection
// for pre-existing liquidity, synthesized aggressors for type-4 rows —
// see docs/DESIGN.md), which is real, necessary work for Phase 2's
// correctness gate but is a property of replaying *this particular
// academic data source*, not of the matching engine itself. Measuring
// through it would conflate adapter translation cost with engine cost,
// which matters a lot once Phase 4 starts optimizing the engine and needs
// a clean before/after comparison.
//
// One consequence: LOBSTER type-1 rows only ever show an order's
// *resting* remainder (see docs/DESIGN.md's Phase 2 section) — a real
// exchange's Add Order message never crosses the book. So replaying raw
// type-1 rows through new_order() essentially never exercises the
// crossing path. Crossing cost is deliberately NOT measured here; see
// bench/micro_benchmarks.cpp for a purpose-built synthetic scenario that
// isolates it properly instead of relying on incidental crossings.

#include <algorithm>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "lobster/lobster_message.hpp"
#include "riptide/matching_engine.hpp"
#include "stats.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// Number of back-to-back timer calls used to measure the timer's own
// overhead. Each recorded latency below is `t1 - t0` around one engine
// call; this measures what `t1 - t0` would read with *no* work between
// the two calls, i.e. the fixed cost baked into every measurement, so it
// can be disclosed and subtracted.
constexpr int kTimerCalibrationSamples = 200'000;

// First N successfully-applied messages, timed individually before any
// warm-up — this is what "cold start" means here: cold instruction/data
// caches, an untrained branch predictor, and a freshly-constructed engine
// right after process start. Small enough to stay a distinct "just
// started" snapshot rather than blending into steady state.
constexpr int kColdStartSamples = 200;

// Messages applied (advancing book state) but not timed, after the cold-
// start window and before steady-state measurement begins. Chosen to be
// comfortably larger than any plausible cache/branch-predictor warm-up
// horizon, while staying small relative to a full day's ~200K-600K
// messages so it doesn't meaningfully shrink the measurement window.
constexpr std::size_t kWarmupMessages = 50'000;

enum class Category { kNew, kCancel, kModify };

const char* CategoryName(Category category) {
  switch (category) {
    case Category::kNew:
      return "New";
    case Category::kCancel:
      return "Cancel";
    case Category::kModify:
      return "Modify";
  }
  return "?";
}

// Translates one LOBSTER message into a direct MatchingEngine call and
// applies it. Returns the category to time it under, or nullopt if this
// message shouldn't be applied/timed at all:
//   - types 4/5/6/7: not translated here (see file header) -- skipped.
//   - type 2 (partial cancellation) referencing an order this run never
//     saw a type-1 for: LOBSTER's free sample data references pre-
//     existing orders with no type-1 row at all (see docs/DESIGN.md);
//     reproducing that correctly needs Adapter's bucket machinery, which
//     is deliberately not used here, so these are skipped rather than
//     silently applying a wrong delta.
std::optional<Category> ApplyForBenchmark(riptide::MatchingEngine& engine,
                                           const riptide::lobster::Message& message) {
  using riptide::ModifyRequest;
  using riptide::NewOrderRequest;
  using riptide::OrderType;
  using riptide::Side;
  using riptide::TimeInForce;

  switch (message.event_type) {
    case 1:
      engine.new_order(NewOrderRequest{.id = message.order_id,
                                        .side = message.direction == 1 ? Side::Buy : Side::Sell,
                                        .type = OrderType::Limit,
                                        .tif = TimeInForce::GTC,
                                        .price = message.price,
                                        .quantity = message.size});
      return Category::kNew;

    case 2: {
      const riptide::Order* existing = engine.book().find(message.order_id);
      if (existing == nullptr || message.size >= existing->remaining) return std::nullopt;
      engine.modify(ModifyRequest{.id = message.order_id,
                                   .price = std::nullopt,
                                   .quantity = existing->remaining - message.size});
      return Category::kModify;
    }

    case 3:
      engine.cancel(message.order_id);
      return Category::kCancel;

    default:
      return std::nullopt;
  }
}

std::uint64_t CalibrateTimerOverheadNs() {
  std::vector<std::uint64_t> deltas;
  deltas.reserve(kTimerCalibrationSamples);
  for (int i = 0; i < kTimerCalibrationSamples; ++i) {
    const auto t0 = Clock::now();
    const auto t1 = Clock::now();
    deltas.push_back(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
  }
  return riptide::bench::ComputePercentiles(std::move(deltas)).p50;
}

struct CategorySamples {
  std::vector<std::uint64_t> latencies_ns;
};

void PrintPercentileRow(const char* label, const riptide::bench::Percentiles& p) {
  std::cout << label << ": n=" << p.count << " p50=" << p.p50 << "ns p90=" << p.p90
            << "ns p99=" << p.p99 << "ns p99.9=" << p.p999 << "ns p99.99=" << p.p9999
            << "ns max=" << p.max << "ns\n";
}

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " <message.csv>\n"
            << "\n"
            << "Replays a LOBSTER message file directly through MatchingEngine and\n"
            << "reports per-message-type latency percentiles, a histogram, and\n"
            << "throughput. See the file header comment for methodology.\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    PrintUsage(argv[0]);
    return 2;
  }

  std::cout << "Calibrating timer overhead (" << kTimerCalibrationSamples << " back-to-back calls)...\n";
  const std::uint64_t timer_overhead_ns = CalibrateTimerOverheadNs();
  std::cout << "Timer overhead (median): " << timer_overhead_ns << "ns"
            << " -- already subtracted from every latency reported below\n\n";

  riptide::MatchingEngine engine;
  riptide::lobster::MessageReader messages(argv[1]);
  riptide::lobster::Message message;

  std::vector<std::uint64_t> cold_start_latencies_ns;
  cold_start_latencies_ns.reserve(kColdStartSamples);

  CategorySamples new_samples, cancel_samples, modify_samples;
  std::size_t applied = 0;
  std::size_t skipped = 0;
  std::size_t total_messages = 0;

  const auto subtract_overhead = [timer_overhead_ns](std::uint64_t raw_ns) -> std::uint64_t {
    return raw_ns > timer_overhead_ns ? raw_ns - timer_overhead_ns : 0;
  };

  // Phase 1: cold start -- first kColdStartSamples *applied* messages,
  // timed individually right after engine construction.
  while (static_cast<int>(cold_start_latencies_ns.size()) < kColdStartSamples && messages.Next(message)) {
    ++total_messages;
    const auto t0 = Clock::now();
    const std::optional<Category> category = ApplyForBenchmark(engine, message);
    const auto t1 = Clock::now();
    if (!category.has_value()) {
      ++skipped;
      continue;
    }
    ++applied;
    cold_start_latencies_ns.push_back(subtract_overhead(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())));
  }

  // Phase 2: warm-up -- applied but not timed.
  std::size_t warmed_up = 0;
  while (warmed_up < kWarmupMessages && messages.Next(message)) {
    ++total_messages;
    if (ApplyForBenchmark(engine, message).has_value()) {
      ++applied;
      ++warmed_up;
    } else {
      ++skipped;
    }
  }

  // Phase 3: steady state -- applied and timed, bucketed by category.
  const auto steady_state_start = Clock::now();
  while (messages.Next(message)) {
    ++total_messages;
    const auto t0 = Clock::now();
    const std::optional<Category> category = ApplyForBenchmark(engine, message);
    const auto t1 = Clock::now();
    if (!category.has_value()) {
      ++skipped;
      continue;
    }
    ++applied;
    const std::uint64_t latency_ns = subtract_overhead(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    switch (*category) {
      case Category::kNew:
        new_samples.latencies_ns.push_back(latency_ns);
        break;
      case Category::kCancel:
        cancel_samples.latencies_ns.push_back(latency_ns);
        break;
      case Category::kModify:
        modify_samples.latencies_ns.push_back(latency_ns);
        break;
    }
  }
  const auto steady_state_end = Clock::now();

  std::cout << "Messages: " << total_messages << " total, " << applied << " applied ("
            << CategoryName(Category::kNew) << "/" << CategoryName(Category::kCancel) << "/"
            << CategoryName(Category::kModify) << " only -- see file header), " << skipped
            << " skipped (type 4/5/6/7, or a modify referencing an order this run never saw a"
               " type-1 for)\n\n";

  std::cout << "--- Cold start (first " << cold_start_latencies_ns.size() << " applied messages) ---\n";
  PrintPercentileRow("cold-start", riptide::bench::ComputePercentiles(cold_start_latencies_ns));

  std::cout << "\n--- Steady state (after " << kWarmupMessages << "-message warm-up, discarded) ---\n";

  std::vector<std::uint64_t> combined;
  combined.reserve(new_samples.latencies_ns.size() + cancel_samples.latencies_ns.size() +
                    modify_samples.latencies_ns.size());
  combined.insert(combined.end(), new_samples.latencies_ns.begin(), new_samples.latencies_ns.end());
  combined.insert(combined.end(), cancel_samples.latencies_ns.begin(), cancel_samples.latencies_ns.end());
  combined.insert(combined.end(), modify_samples.latencies_ns.begin(), modify_samples.latencies_ns.end());

  PrintPercentileRow("all", riptide::bench::ComputePercentiles(combined));
  PrintPercentileRow(CategoryName(Category::kNew), riptide::bench::ComputePercentiles(new_samples.latencies_ns));
  PrintPercentileRow(CategoryName(Category::kCancel),
                     riptide::bench::ComputePercentiles(cancel_samples.latencies_ns));
  PrintPercentileRow(CategoryName(Category::kModify),
                     riptide::bench::ComputePercentiles(modify_samples.latencies_ns));

  std::cout << "\n--- Histogram (all, steady state) ---\n";
  riptide::bench::PrintHistogram(std::cout, combined);

  const double steady_state_seconds =
      std::chrono::duration<double>(steady_state_end - steady_state_start).count();
  const std::size_t steady_state_applied = combined.size();
  std::cout << "\n--- Throughput ---\n";
  std::cout << "steady-state: " << steady_state_applied << " applied messages in " << steady_state_seconds
            << "s = " << static_cast<std::uint64_t>(static_cast<double>(steady_state_applied) /
                                                      steady_state_seconds)
            << " msg/s\n";

  // Machine-parseable summary for bench/run_replay_benchmark.sh's
  // multi-run median/IQR aggregation across independent process runs.
  // Two separate, fixed-shape blocks rather than one mixed table, so the
  // aggregation script doesn't need to special-case a row with a
  // different column count.
  std::cout << "\nSUMMARY_CSV\n";
  std::cout << "category,count,p50,p90,p99,p999,p9999,max\n";
  const auto print_csv = [](const char* label, const riptide::bench::Percentiles& p) {
    std::cout << label << "," << p.count << "," << p.p50 << "," << p.p90 << "," << p.p99 << "," << p.p999
               << "," << p.p9999 << "," << p.max << "\n";
  };
  print_csv("all", riptide::bench::ComputePercentiles(combined));
  print_csv("new", riptide::bench::ComputePercentiles(new_samples.latencies_ns));
  print_csv("cancel", riptide::bench::ComputePercentiles(cancel_samples.latencies_ns));
  print_csv("modify", riptide::bench::ComputePercentiles(modify_samples.latencies_ns));

  std::cout << "THROUGHPUT_MSG_PER_SEC "
            << static_cast<std::uint64_t>(static_cast<double>(steady_state_applied) / steady_state_seconds)
            << "\n";

  return 0;
}
