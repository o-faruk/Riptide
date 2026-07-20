#include "lobster_book.hpp"

#include <charconv>
#include <stdexcept>
#include <string_view>

namespace riptide::lobster {

namespace {

std::vector<std::string_view> SplitCsv(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t comma = line.find(',', start);
    if (comma == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, comma - start));
    start = comma + 1;
  }
  return fields;
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

OrderBookFileReader::OrderBookFileReader(const std::string& path) : file_(path) {
  if (!file_) {
    throw std::runtime_error("failed to open LOBSTER orderbook file: " + path);
  }
}

bool OrderBookFileReader::Next(BookRow& out) {
  std::string line;
  if (!std::getline(file_, line)) return false;
  ++line_number_;

  const std::vector<std::string_view> fields = SplitCsv(line);
  if (fields.empty() || fields.size() % 4 != 0) {
    throw std::runtime_error("malformed orderbook row at line " + std::to_string(line_number_) +
                              " (field count not a multiple of 4): " + line);
  }

  const int row_levels = static_cast<int>(fields.size() / 4);
  if (levels_ == 0) {
    levels_ = row_levels;
  } else if (row_levels != levels_) {
    throw std::runtime_error("orderbook row at line " + std::to_string(line_number_) +
                              " has " + std::to_string(row_levels) +
                              " levels, expected " + std::to_string(levels_));
  }

  out.asks.clear();
  out.bids.clear();
  out.asks.reserve(static_cast<std::size_t>(levels_));
  out.bids.reserve(static_cast<std::size_t>(levels_));

  for (int level = 0; level < levels_; ++level) {
    const std::size_t base = static_cast<std::size_t>(level) * 4;
    out.asks.push_back(
        BookLevel{ParseInt<riptide::Price>(fields[base]), ParseInt<riptide::Quantity>(fields[base + 1])});
    out.bids.push_back(BookLevel{ParseInt<riptide::Price>(fields[base + 2]),
                                  ParseInt<riptide::Quantity>(fields[base + 3])});
  }
  return true;
}

}  // namespace riptide::lobster
