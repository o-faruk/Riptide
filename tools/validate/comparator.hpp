#pragma once

#include <ostream>

#include "lobster/lobster_book.hpp"
#include "riptide/order_book.hpp"

namespace riptide::lobster {

// Compares the top `levels` price levels of `book` (both sides) against
// `reference`. Zero tolerance, per the project's validation gate: true iff
// every level's price and aggregate size match exactly.
bool TopLevelsMatch(const OrderBook& book, const BookRow& reference, int levels);

// Prints a side-by-side table of `book`'s top levels vs. `reference` to
// `out` — for diagnosing a mismatch TopLevelsMatch has already found.
void PrintSideBySideDiff(std::ostream& out, const OrderBook& book, const BookRow& reference,
                          int levels);

}  // namespace riptide::lobster
