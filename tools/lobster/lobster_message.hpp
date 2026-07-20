#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace riptide::lobster {

// One row of a LOBSTER message file. Fields are kept raw (not translated
// into riptide::Side/Price/etc.) because their meaning is event-type
// dependent — e.g. for a type-7 trading halt, `price` and `direction` are
// repurposed as halt-state sentinels, not a real price/side. See
// adapter.cpp for how each event type interprets these fields.
struct Message {
  double time;
  int event_type;
  std::uint64_t order_id;
  std::uint32_t size;
  std::int64_t price;
  int direction;  // -1 or 1
};

// Streams a LOBSTER message CSV file line by line. Files are hundreds of
// MB for a single trading day, and real market-data volumes (more
// symbols, more days) scale well past whatever RAM happens to be
// available on the machine running this — so this never loads more than
// one line at a time regardless of the current dev/bench machine's specs.
class MessageReader {
 public:
  explicit MessageReader(const std::string& path);

  // Reads the next row into `out`. Returns false at EOF.
  bool Next(Message& out);

  // 1-based row number of the most recently read message — matches the
  // corresponding row of the orderbook file 1:1 (LOBSTER guarantees this).
  std::size_t line_number() const { return line_number_; }

 private:
  std::ifstream file_;
  std::size_t line_number_ = 0;
};

}  // namespace riptide::lobster
