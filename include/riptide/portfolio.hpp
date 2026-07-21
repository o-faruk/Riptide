#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "riptide/event.hpp"
#include "riptide/types.hpp"

namespace riptide {

// Tracks a strategy's own cash and per-instrument position from Trade
// events -- whether they came from the strategy's own order submissions
// or from some other market participant's order trading against the
// strategy's resting order. See docs/DESIGN.md's Phase 5 section for
// why both are possible: the strategy is a real participant in a live
// MatchingEngine, not a passive observer approximating fills against a
// historical tape.
//
// Cash is a single account shared across every instrument (money is
// fungible); position, and the running max-abs-position seen, are
// tracked per instrument.
class Portfolio {
 public:
  // Every ID the strategy's own orders on `instrument` are submitted
  // under must be registered here first (OrderPort does this
  // automatically) -- otherwise ApplyEvent has no way to tell "this is
  // one of my fills" from "this is two other participants trading with
  // each other". IDs are only unique WITHIN an instrument (each
  // instrument has its own engine/adapter), hence keyed by instrument.
  void RegisterOrderId(const InstrumentId& instrument, OrderId id) {
    owned_ids_[instrument].insert(id);
  }

  // Call once per event this engine produces, from any source (market
  // row or strategy submission) -- events for order IDs this portfolio
  // doesn't own (on that instrument) are ignored.
  void ApplyEvent(const InstrumentId& instrument, const Event& event) {
    const auto* trade = std::get_if<Trade>(&event);
    if (trade == nullptr) return;

    const auto owned_it = owned_ids_.find(instrument);
    const bool has_any_owned = owned_it != owned_ids_.end();
    const bool own_resting = has_any_owned && owned_it->second.contains(trade->resting_id);
    const bool own_aggressor = has_any_owned && owned_it->second.contains(trade->aggressor_id);
    if (!own_resting && !own_aggressor) return;

    const auto qty = static_cast<std::int64_t>(trade->quantity);
    const auto notional = qty * trade->price;

    // Both being true would double-count a fill against ourselves — not
    // reachable here: new_order rejects a duplicate ID outright, and
    // every ID minted for the strategy (via OrderPort) is disjoint from
    // every other participant's, so a trade can own at most one side.
    if (own_aggressor) ApplySide(instrument, trade->aggressor_side, qty, notional);
    if (own_resting) {
      const Side resting_side = (trade->aggressor_side == Side::Buy) ? Side::Sell : Side::Buy;
      ApplySide(instrument, resting_side, qty, notional);
    }

    ++fill_count_;
  }

  std::int64_t position(const InstrumentId& instrument) const {
    const auto it = positions_.find(instrument);
    return it == positions_.end() ? 0 : it->second;
  }
  std::int64_t cash() const { return cash_; }
  std::uint64_t fill_count() const { return fill_count_; }
  std::uint64_t max_abs_position(const InstrumentId& instrument) const {
    const auto it = max_abs_position_.find(instrument);
    return it == max_abs_position_.end() ? 0 : it->second;
  }

  // Total mark-to-market value: cash plus every held instrument's
  // position priced at `marks`. An instrument with a nonzero position
  // but no entry in `marks` contributes nothing to the total -- an
  // omission the caller must supply a real price for, not something
  // silently guessed at (e.g. 0).
  std::int64_t mark_to_market(const std::unordered_map<InstrumentId, Price>& marks) const {
    std::int64_t total = cash_;
    for (const auto& [instrument, position] : positions_) {
      const auto mark_it = marks.find(instrument);
      if (mark_it != marks.end()) total += position * mark_it->second;
    }
    return total;
  }

  // Convenience for the common single-instrument case.
  std::int64_t mark_to_market(const InstrumentId& instrument, Price mark_price) const {
    return mark_to_market(std::unordered_map<InstrumentId, Price>{{instrument, mark_price}});
  }

  // Appends one sample to the running mark-to-market series (the
  // caller — Backtester/MultiInstrumentBacktester, which has book
  // access — computes `total_mtm` via mark_to_market() above and
  // records it here once per replayed row). Backs max_drawdown() and
  // sharpe_ratio_per_sample() below via O(1) streaming stats (Welford's
  // algorithm for the mean/variance, a running peak for drawdown) —
  // deliberately not storing the full history, since a real replay can
  // be hundreds of thousands of rows.
  void RecordMarkToMarket(std::int64_t total_mtm) {
    if (mtm_sample_count_ > 0) {
      const auto delta = static_cast<double>(total_mtm - last_mtm_);
      ++delta_count_;
      const double delta1 = delta - delta_mean_;
      delta_mean_ += delta1 / static_cast<double>(delta_count_);
      const double delta2 = delta - delta_mean_;
      delta_m2_ += delta1 * delta2;
    }
    last_mtm_ = total_mtm;
    ++mtm_sample_count_;

    peak_mtm_ = std::max(peak_mtm_, total_mtm);
    max_drawdown_ = std::max(max_drawdown_, peak_mtm_ - total_mtm);
  }

  // Worst peak-to-current decline seen across every recorded sample,
  // same tick-scaled units as riptide::Price throughout.
  std::int64_t max_drawdown() const { return max_drawdown_; }

  // Mean divided by standard deviation of PER-SAMPLE mark-to-market
  // deltas. Deliberately named "per sample," not "Sharpe ratio": a real
  // Sharpe ratio is computed on independent periodic (e.g. daily)
  // returns and annualized; the samples recorded here are deltas
  // between whatever rows the caller chose to record (typically every
  // replayed LOBSTER row within a single session), which are neither
  // independent nor a fixed period. This number is directionally
  // useful for comparing two runs against the same replay, and nothing
  // more -- reporting it as an annualized Sharpe ratio would be a
  // fabricated number this project's own rules don't allow.
  std::optional<double> sharpe_ratio_per_sample() const {
    if (delta_count_ < 2) return std::nullopt;
    const double variance = delta_m2_ / static_cast<double>(delta_count_ - 1);
    const double stddev = std::sqrt(variance);
    if (stddev == 0.0) return std::nullopt;
    return delta_mean_ / stddev;
  }

 private:
  void ApplySide(const InstrumentId& instrument, Side side, std::int64_t qty,
                 std::int64_t notional) {
    std::int64_t& position = positions_[instrument];
    if (side == Side::Buy) {
      position += qty;
      cash_ -= notional;
    } else {
      position -= qty;
      cash_ += notional;
    }
    std::uint64_t& max_abs = max_abs_position_[instrument];
    max_abs = std::max(max_abs, static_cast<std::uint64_t>(std::abs(position)));
  }

  std::unordered_map<InstrumentId, std::unordered_set<OrderId>> owned_ids_;
  std::unordered_map<InstrumentId, std::int64_t> positions_;
  std::unordered_map<InstrumentId, std::uint64_t> max_abs_position_;
  std::int64_t cash_ = 0;
  std::uint64_t fill_count_ = 0;

  std::int64_t last_mtm_ = 0;
  std::uint64_t mtm_sample_count_ = 0;
  std::uint64_t delta_count_ = 0;
  double delta_mean_ = 0.0;
  double delta_m2_ = 0.0;
  std::int64_t peak_mtm_ = 0;
  std::int64_t max_drawdown_ = 0;
};

}  // namespace riptide
