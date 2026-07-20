#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "riptide/types.hpp"

namespace riptide::lobster {

struct BookLevel {
  riptide::Price price;
  riptide::Quantity size;
};

// One row of a LOBSTER orderbook file: top-N levels for both sides, best
// first. Unoccupied levels are kept as-is (price ±9999999999, size 0, per
// LOBSTER's own dummy-value convention) rather than filtered out here —
// the comparator is what decides how to treat them.
struct BookRow {
  std::vector<BookLevel> asks;  // asks[0] = best (lowest) ask
  std::vector<BookLevel> bids;  // bids[0] = best (highest) bid
};

// Streams a LOBSTER orderbook CSV file line by line, same rationale as
// MessageReader: these files run to hundreds of MB and must never be
// loaded whole.
class OrderBookFileReader {
 public:
  explicit OrderBookFileReader(const std::string& path);

  bool Next(BookRow& out);

  std::size_t line_number() const { return line_number_; }

  // Number of levels the file was generated with (columns / 4), inferred
  // from the first row read. 0 before the first Next() call.
  int levels() const { return levels_; }

 private:
  std::ifstream file_;
  std::size_t line_number_ = 0;
  int levels_ = 0;
};

}  // namespace riptide::lobster
