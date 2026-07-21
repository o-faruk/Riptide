#pragma once

#include <iomanip>
#include <ostream>
#include <sstream>
#include <vector>

#include "lobster/lobster_book.hpp"
#include "riptide/order_book.hpp"

namespace riptide::lobster {

namespace detail {

// LOBSTER's dummy sentinels for an unoccupied price level (see ReadMe.txt).
inline constexpr Price kAskDummyPrice = 9999999999LL;
inline constexpr Price kBidDummyPrice = -9999999999LL;

template <typename LevelMap>
std::vector<BookLevel> TopLevels(const LevelMap& levels_map, int levels) {
  std::vector<BookLevel> result;
  result.reserve(static_cast<std::size_t>(levels));
  for (const auto& [price, level] : levels_map) {
    if (static_cast<int>(result.size()) >= levels) break;
    result.push_back(BookLevel{price, level.total_quantity});
  }
  return result;
}

// True iff level `index` agrees between our book and LOBSTER's reference:
// either both have a real occupied level there with the same price/size,
// or both agree there's nothing there (LOBSTER's dummy vs. us simply
// having fewer occupied levels than `index`).
inline bool LevelMatches(const std::vector<BookLevel>& actual, const std::vector<BookLevel>& reference,
                          int index, Price dummy_price) {
  const bool reference_occupied = reference[static_cast<std::size_t>(index)].price != dummy_price;
  const bool actual_occupied = index < static_cast<int>(actual.size());
  if (reference_occupied != actual_occupied) return false;
  if (!reference_occupied) return true;
  return actual[static_cast<std::size_t>(index)].price ==
             reference[static_cast<std::size_t>(index)].price &&
         actual[static_cast<std::size_t>(index)].size ==
             reference[static_cast<std::size_t>(index)].size;
}

inline void PrintCell(std::ostream& out, const std::vector<BookLevel>& levels, int index) {
  if (index < static_cast<int>(levels.size())) {
    out << levels[static_cast<std::size_t>(index)].price << ","
        << levels[static_cast<std::size_t>(index)].size;
  } else {
    out << "(none)";
  }
}

inline void PrintReferenceCell(std::ostream& out, const BookLevel& level, Price dummy_price) {
  if (level.price == dummy_price) {
    out << "(none)";
  } else {
    out << level.price << "," << level.size;
  }
}

}  // namespace detail

// Compares the top `levels` price levels of `book` (both sides) against
// `reference`. Zero tolerance, per the project's validation gate: true iff
// every level's price and aggregate size match exactly.
//
// Templated on Book (rather than fixed to OrderBook) so tools/validate can
// run Phase 2's validation gate against any MatchingEngine<Book>'s
// underlying book type -- same reasoning as Adapter<Engine>.
template <typename Book>
bool TopLevelsMatch(const Book& book, const BookRow& reference, int levels) {
  const std::vector<BookLevel> actual_asks = detail::TopLevels(book.ask_levels(), levels);
  const std::vector<BookLevel> actual_bids = detail::TopLevels(book.bid_levels(), levels);

  for (int i = 0; i < levels; ++i) {
    if (!detail::LevelMatches(actual_asks, reference.asks, i, detail::kAskDummyPrice)) return false;
    if (!detail::LevelMatches(actual_bids, reference.bids, i, detail::kBidDummyPrice)) return false;
  }
  return true;
}

// Prints a side-by-side table of `book`'s top levels vs. `reference` to
// `out` — for diagnosing a mismatch TopLevelsMatch has already found.
template <typename Book>
void PrintSideBySideDiff(std::ostream& out, const Book& book, const BookRow& reference, int levels) {
  const std::vector<BookLevel> actual_asks = detail::TopLevels(book.ask_levels(), levels);
  const std::vector<BookLevel> actual_bids = detail::TopLevels(book.bid_levels(), levels);

  out << std::left;
  out << "  " << std::setw(4) << "lvl" << std::setw(28) << "mine: bid" << std::setw(28)
      << "lobster: bid" << std::setw(28) << "mine: ask" << std::setw(28) << "lobster: ask" << "\n";

  for (int i = 0; i < levels; ++i) {
    out << "  " << std::setw(4) << (i + 1);

    std::ostringstream mine_bid, ref_bid, mine_ask, ref_ask;
    detail::PrintCell(mine_bid, actual_bids, i);
    detail::PrintReferenceCell(ref_bid, reference.bids[static_cast<std::size_t>(i)], detail::kBidDummyPrice);
    detail::PrintCell(mine_ask, actual_asks, i);
    detail::PrintReferenceCell(ref_ask, reference.asks[static_cast<std::size_t>(i)], detail::kAskDummyPrice);

    const bool bid_ok = detail::LevelMatches(actual_bids, reference.bids, i, detail::kBidDummyPrice);
    const bool ask_ok = detail::LevelMatches(actual_asks, reference.asks, i, detail::kAskDummyPrice);

    out << std::setw(28) << (mine_bid.str() + (bid_ok ? "" : " <-- MISMATCH")) << std::setw(28)
        << ref_bid.str() << std::setw(28) << (mine_ask.str() + (ask_ok ? "" : " <-- MISMATCH"))
        << std::setw(28) << ref_ask.str() << "\n";
  }
}

}  // namespace riptide::lobster
