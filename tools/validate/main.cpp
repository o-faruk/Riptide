#include <algorithm>
#include <charconv>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "comparator.hpp"
#include "lobster/adapter.hpp"
#include "lobster/lobster_book.hpp"
#include "lobster/lobster_message.hpp"
#include "riptide/matching_engine.hpp"
#include "riptide/pooled_order_book.hpp"

namespace {

void PrintUsage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " <message.csv> <orderbook.csv> [--engine reference|pooled] [--report-all] [--top N]"
         " [--after N] [--min-rows N]\n"
      << "\n"
      << "--engine: which MatchingEngine<Book> instantiation to validate against.\n"
      << "reference (default) is Phase 1's unoptimized engine; pooled is the pool-allocator\n"
      << "optimization from Phase 4 (docs/OPTIMIZATION_LOG.md). Re-running this gate against\n"
      << "an optimized engine is exactly how Phase 4's loop re-validates a change before\n"
      << "trusting any benchmark result from it.\n"
      << "\n"
      << "LOBSTER's free sample files start at regular market open (09:30) with\n"
      << "a book that already has resting liquidity from before that window --\n"
      << "liquidity the message file gives no type-1 rows for, some of it never\n"
      << "referenced at all until it trades (see docs/DESIGN.md). This tool\n"
      << "seeds that initial state from orderbook row 1, then validates from\n"
      << "row 2 onward at full depth, stopping at the first divergence with a\n"
      << "full side-by-side diagnostic. Because of the above, that first\n"
      << "divergence is expected well before end of day for every ticker --\n"
      << "the gate is that this happens no earlier than the documented\n"
      << "baseline (see --min-rows), not that the whole file validates.\n"
      << "\n"
      << "--report-all: diagnostic mode. Instead of stopping at the first\n"
      << "mismatch, logs a one-line summary per mismatch and keeps going,\n"
      << "printing a final tally. Useful for telling an isolated data\n"
      << "artifact apart from a systematic adapter bug.\n"
      << "\n"
      << "--top N: compare only the top N levels instead of the file's full\n"
      << "depth.\n"
      << "\n"
      << "--after N: suppress diagnostics until message line N, then behave as\n"
      << "if just starting from there. For inspecting one later mismatch\n"
      << "without wading through everything before it.\n"
      << "\n"
      << "--min-rows N: regression-guard mode for CI. Exit 0 as long as at\n"
      << "least N rows validated before stopping (whatever the reason) --\n"
      << "i.e. \"no worse than the documented baseline\" -- instead of\n"
      << "requiring the whole file to validate, which free sample data can't\n"
      << "achieve. Exit 1 if divergence happens earlier than N: that's a real\n"
      << "regression, not the known, documented data limitation.\n";
}

struct Options {
  bool report_all = false;
  std::optional<int> compare_levels;
  std::size_t after_line = 0;
  std::optional<std::size_t> min_rows;
};

// The actual validation run, templated on Engine so it can be
// instantiated against ReferenceEngine or any Phase 4 optimized engine
// (e.g. PooledMatchingEngine) without duplicating this logic — same
// reasoning as MatchingEngine<Book> and Adapter<Engine> themselves.
template <typename Engine>
int Run(const char* message_path, const char* orderbook_path, const Options& options) {
  try {
    Engine engine;
    riptide::lobster::Adapter<Engine> adapter(engine);
    riptide::lobster::MessageReader messages(message_path);
    riptide::lobster::OrderBookFileReader orderbook(orderbook_path);

    riptide::lobster::Message message;
    riptide::lobster::BookRow row;

    // Row 1 / message 1 together represent pre-existing book state (see
    // Adapter's docs) — consumed to bootstrap, not validated directly.
    if (!orderbook.Next(row) || !messages.Next(message)) {
      std::cerr << "ERROR: file(s) empty or too short to bootstrap\n";
      return 1;
    }
    adapter.SeedFromInitialBookState(row);
    std::cerr << "Seeded initial book from row 1 (" << (row.bids.size() + row.asks.size())
              << " level slots); validating from row 2 onward.\n";

    std::size_t rows_validated = 0;
    std::size_t mismatches = 0;
    bool stopped_early = false;

    while (true) {
      const bool has_message = messages.Next(message);
      const bool has_row = orderbook.Next(row);

      if (has_message != has_row) {
        std::cerr << "ERROR: message file and orderbook file have different row counts\n";
        return 1;
      }
      if (!has_message) break;  // both exhausted together: full file validated

      if (const auto error = adapter.ApplyMessage(message); error.has_value()) {
        std::cerr << "ERROR at message line " << messages.line_number() << ": " << *error << "\n";
        stopped_early = true;
        break;
      }

      const int levels =
          std::min(options.compare_levels.value_or(orderbook.levels()), orderbook.levels());
      if (!riptide::lobster::TopLevelsMatch(engine.book(), row, levels) &&
          messages.line_number() > options.after_line) {
        if (!options.report_all) {
          std::cerr << "MISMATCH at line " << messages.line_number() << "\n"
                     << "  message: time=" << message.time << " type=" << message.event_type
                     << " order_id=" << message.order_id << " size=" << message.size
                     << " price=" << message.price << " direction=" << message.direction << "\n";
          riptide::lobster::PrintSideBySideDiff(std::cerr, engine.book(), row, levels);
          stopped_early = true;
          break;
        }
        ++mismatches;
        std::cerr << "MISMATCH at line " << messages.line_number() << " (type=" << message.event_type
                   << " order_id=" << message.order_id << " price=" << message.price << ")\n";
      }

      ++rows_validated;
    }

    if (options.min_rows.has_value()) {
      const bool ok = rows_validated >= *options.min_rows;
      std::cout << (ok ? "OK" : "REGRESSION") << ": " << rows_validated
                << " rows validated before stopping (baseline: " << *options.min_rows << ")\n";
      return ok ? 0 : 1;
    }

    if (options.report_all) {
      std::cout << "DONE: " << rows_validated << " rows checked, " << mismatches
                << " mismatches (row 1 used to bootstrap pre-existing book state)\n";
      return mismatches == 0 ? 0 : 1;
    }

    if (stopped_early) return 1;

    std::cout << "OK: " << rows_validated << " of " << (rows_validated + 1)
              << " rows validated (row 1 used to bootstrap pre-existing book state), zero"
                 " mismatches\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FATAL: " << e.what() << "\n";
    return 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    PrintUsage(argv[0]);
    return 2;
  }

  Options options;
  std::string engine_name = "reference";

  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--report-all") {
      options.report_all = true;
    } else if (arg == "--engine" && i + 1 < argc) {
      engine_name = argv[++i];
    } else if ((arg == "--top" || arg == "--after" || arg == "--min-rows") && i + 1 < argc) {
      long value = 0;
      const std::string_view digits = argv[++i];
      if (std::from_chars(digits.data(), digits.data() + digits.size(), value).ec != std::errc{} ||
          value <= 0) {
        PrintUsage(argv[0]);
        return 2;
      }
      if (arg == "--top") {
        options.compare_levels = static_cast<int>(value);
      } else if (arg == "--after") {
        options.after_line = static_cast<std::size_t>(value);
      } else {
        options.min_rows = static_cast<std::size_t>(value);
      }
    } else {
      PrintUsage(argv[0]);
      return 2;
    }
  }

  if (engine_name == "reference") {
    return Run<riptide::ReferenceEngine>(argv[1], argv[2], options);
  }
  if (engine_name == "pooled") {
    return Run<riptide::PooledMatchingEngine>(argv[1], argv[2], options);
  }
  std::cerr << "ERROR: unrecognized --engine value \"" << engine_name << "\" (expected reference or pooled)\n";
  return 2;
}
