#pragma once

#include <cstdint>
#include <optional>

#include "riptide/types.hpp"

namespace riptide {

// Pre-trade risk controls OrderPort enforces on a Strategy's own order
// submissions -- a backtester-level risk desk sitting on top of, not a
// replacement for, MatchingEngine's own request validation (which every
// participant is subject to regardless). nullopt means "no limit" for
// that check. Per-instrument: a multi-instrument backtest applies the
// same RiskLimits independently to each instrument's position, not to
// a combined cross-instrument exposure.
struct RiskLimits {
  // Checked against the position OrderPort projects the order would
  // leave, assuming it fills completely -- not whether it actually
  // does. A resting order that never fills can't itself breach this.
  std::optional<std::int64_t> max_abs_position;

  // Checked against the order's own size, independent of position.
  std::optional<Quantity> max_order_quantity;
};

}  // namespace riptide
