#include "lobster/lobster_message.hpp"

#include <charconv>
#include <stdexcept>
#include <string_view>

namespace riptide::lobster {

namespace {

// LOBSTER's message/orderbook files are pure numeric CSV with no quoting,
// so a plain comma split is sufficient — no need for a general CSV parser.
std::size_t SplitCsv(std::string_view line, std::string_view* fields, std::size_t max_fields) {
  std::size_t count = 0;
  std::size_t start = 0;
  while (count < max_fields) {
    const std::size_t comma = line.find(',', start);
    if (comma == std::string_view::npos) {
      fields[count++] = line.substr(start);
      break;
    }
    fields[count++] = line.substr(start, comma - start);
    start = comma + 1;
  }
  return count;
}

template <typename T>
T ParseInt(std::string_view field) {
  T value{};
  const auto result = std::from_chars(field.data(), field.data() + field.size(), value);
  if (result.ec != std::errc{}) {
    throw std::runtime_error("failed to parse integer field: '" + std::string(field) + "'");
  }
  return value;
}

}  // namespace

MessageReader::MessageReader(const std::string& path) : file_(path) {
  if (!file_) {
    throw std::runtime_error("failed to open LOBSTER message file: " + path);
  }
}

bool MessageReader::Next(Message& out) {
  std::string line;
  if (!std::getline(file_, line)) return false;
  ++line_number_;

  std::string_view fields[6];
  if (SplitCsv(line, fields, 6) != 6) {
    throw std::runtime_error("malformed message row at line " + std::to_string(line_number_) +
                              " (expected 6 fields): " + line);
  }

  // Time is the one non-integer field; std::from_chars<double> support is
  // inconsistent across standard library implementations, so std::stod
  // (which every implementation supports) is used here instead.
  out.time = std::stod(std::string(fields[0]));
  out.event_type = ParseInt<int>(fields[1]);
  out.order_id = ParseInt<std::uint64_t>(fields[2]);
  out.size = ParseInt<std::uint32_t>(fields[3]);
  out.price = ParseInt<std::int64_t>(fields[4]);
  out.direction = ParseInt<int>(fields[5]);
  return true;
}

}  // namespace riptide::lobster
