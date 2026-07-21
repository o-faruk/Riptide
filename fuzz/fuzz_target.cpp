// libFuzzer entry point for MatchingEngine: builds a sequence of
// new/cancel/modify operations directly from the fuzzer-provided byte
// stream (see byte_stream.hpp) and drives ReferenceEngine through them.
// There's no correctness assertion here — unlike the differential test
// (tests/differential_test.cpp), a fuzz harness's job is to make
// ASan/UBSan or the engine's own invariants (asserts) catch a crash or
// undefined behavior, not to check output.
//
// Byte-level (not seed-level) construction is deliberate: it's what lets
// libFuzzer's coverage-guided mutation work at all — a small mutation to
// the input bytes should produce a small change in the resulting
// operation sequence, so the fuzzer can incrementally discover inputs
// that reach new code paths. Deriving operations from a hashed seed
// (like tests/differential_test.cpp does, where reproducibility matters
// more than mutation-friendliness) would make small input changes
// produce unrelated sequences, defeating that.
//
// Real fuzzing (coverage-guided mutation under Clang's -fsanitize=fuzzer)
// needs mainline Clang — see docs/DESIGN.md. This file also compiles as
// a standalone "replay a file's bytes once" smoke-test binary via main()
// below when built without RIPTIDE_FUZZ_TARGET_LIBFUZZER_MAIN defined
// (the default), which is what lets it be verified on this project's
// macOS dev machine, where AppleClang lacks the libFuzzer runtime.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

#include "byte_stream.hpp"
#include "riptide/matching_engine.hpp"

namespace {

// Caps how many operations one input can generate — without this, a
// large or adversarial input could drive an unbounded-length run,
// which would look like a hang to libFuzzer rather than a real bug.
constexpr int kMaxOperations = 200;

riptide::NewOrderRequest ConsumeNewOrderRequest(riptide::fuzzing::ByteStream& stream,
                                                 riptide::OrderId id) {
  using riptide::NewOrderRequest;
  using riptide::OrderType;
  using riptide::Price;
  using riptide::Quantity;
  using riptide::Side;
  using riptide::TimeInForce;

  const Side side = stream.ConsumeBool() ? Side::Buy : Side::Sell;
  const int combo = static_cast<int>(stream.ConsumeRange(0, 3));
  // Range deliberately includes out-of-spec values (zero/negative price,
  // huge quantity) — the engine's validation path is exactly as much a
  // fuzz target as the happy path.
  const Price price = stream.ConsumeRange(-10, 2000);
  const auto quantity = static_cast<Quantity>(stream.ConsumeRange(0, 100000));

  switch (combo) {
    case 0:
      return NewOrderRequest{.id = id, .side = side, .type = OrderType::Limit,
                              .tif = TimeInForce::GTC, .price = price, .quantity = quantity};
    case 1:
      return NewOrderRequest{.id = id, .side = side, .type = OrderType::Limit,
                              .tif = TimeInForce::IOC, .price = price, .quantity = quantity};
    case 2:
      return NewOrderRequest{.id = id, .side = side, .type = OrderType::Limit,
                              .tif = TimeInForce::FOK, .price = price, .quantity = quantity};
    default:
      return NewOrderRequest{.id = id, .side = side, .type = OrderType::Market,
                              .tif = TimeInForce::IOC, .price = std::nullopt, .quantity = quantity};
  }
}

void RunOnce(const std::uint8_t* data, std::size_t size) {
  riptide::fuzzing::ByteStream stream(data, size);
  riptide::ReferenceEngine engine;
  riptide::OrderId next_id = 1;

  for (int i = 0; i < kMaxOperations && !stream.Empty(); ++i) {
    const int op_kind = static_cast<int>(stream.ConsumeRange(0, 9));

    if (op_kind < 6) {
      engine.new_order(ConsumeNewOrderRequest(stream, next_id++));
    } else if (op_kind < 8) {
      // Small ID range deliberately overlaps real submitted IDs often,
      // and misses just as often — both the accept and reject paths of
      // cancel are fuzz targets.
      const auto id = static_cast<riptide::OrderId>(stream.ConsumeRange(0, 1000));
      engine.cancel(id);
    } else {
      const auto id = static_cast<riptide::OrderId>(stream.ConsumeRange(0, 1000));
      const std::optional<riptide::Price> price =
          stream.ConsumeBool() ? std::optional<riptide::Price>{stream.ConsumeRange(-10, 2000)}
                                : std::nullopt;
      const std::optional<riptide::Quantity> quantity =
          stream.ConsumeBool()
              ? std::optional<riptide::Quantity>{static_cast<riptide::Quantity>(
                    stream.ConsumeRange(0, 100000))}
              : std::nullopt;
      engine.modify(riptide::ModifyRequest{.id = id, .price = price, .quantity = quantity});
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  RunOnce(data, size);
  return 0;
}

#ifndef RIPTIDE_FUZZ_TARGET_LIBFUZZER_MAIN
// Standalone smoke-test driver, used when NOT built with
// -fsanitize=fuzzer (libFuzzer supplies its own main() in that case —
// defining one here too would be a duplicate-symbol link error).
int main(int argc, char** argv) {
  if (argc < 2) {
    RunOnce(nullptr, 0);
    std::puts("OK: smoke test with empty input completed without crashing");
    return 0;
  }
  for (int i = 1; i < argc; ++i) {
    std::ifstream file(argv[i], std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
    RunOnce(bytes.data(), bytes.size());
    std::printf("OK: %s completed without crashing\n", argv[i]);
  }
  return 0;
}
#endif
