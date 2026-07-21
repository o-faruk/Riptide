#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace riptide::fuzzing {

// Minimal structured byte consumer for fuzz harnesses. Deliberately
// small and self-contained rather than depending on LLVM's
// FuzzedDataProvider.h, whose exact install location varies across
// clang packaging and isn't worth vendoring for what amounts to "read
// some bytes as an integer, in range." Running out of input bytes
// yields zeros rather than erroring — a fuzz harness should never fail
// on short input, only on genuinely bad engine behavior.
class ByteStream {
 public:
  ByteStream(const std::uint8_t* data, std::size_t size) : data_(data), remaining_(size) {}

  template <typename T>
  T ConsumeIntegral() {
    T value{};
    const std::size_t to_copy = std::min(sizeof(T), remaining_);
    std::memcpy(&value, data_, to_copy);
    data_ += to_copy;
    remaining_ -= to_copy;
    return value;
  }

  // A value in [lo, hi] inclusive, consuming one byte.
  std::int64_t ConsumeRange(std::int64_t lo, std::int64_t hi) {
    const std::uint8_t raw = ConsumeIntegral<std::uint8_t>();
    const auto span = static_cast<std::uint64_t>(hi - lo) + 1;
    return lo + static_cast<std::int64_t>(raw % span);
  }

  bool ConsumeBool() { return (ConsumeIntegral<std::uint8_t>() & 1) != 0; }

  bool Empty() const { return remaining_ == 0; }

 private:
  const std::uint8_t* data_;
  std::size_t remaining_;
};

}  // namespace riptide::fuzzing
