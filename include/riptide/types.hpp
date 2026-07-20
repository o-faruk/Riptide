#pragma once

#include <cstdint>

namespace riptide {

// Prices are scaled integers ("ticks"), never floating point — floating
// point comparison/rounding has no place in a matching engine's crossing
// logic. The scale matches LOBSTER's own message/orderbook files (integer
// price * 10000, i.e. this unit is $0.0001) so Phase 2 validation needs no
// unit conversion between the engine's book and the reference data.
using Price = std::int64_t;

using OrderId = std::uint64_t;
using Quantity = std::uint32_t;

// Assigned by the engine when an order is accepted, never by the caller.
// Price-time priority is priority by arrival at the engine, and this is
// the only thing that breaks ties within a price level — see order.hpp.
using Sequence = std::uint64_t;

enum class Side { Buy, Sell };

enum class OrderType { Limit, Market };

// Orthogonal to OrderType rather than folded into it: real venues (FIX tags
// 39/59) model these independently, and it's not just more "correct" — it's
// less code, because a flat {Limit, Market, IOC, FOK} enum still has to
// special-case "IOC has no defined limit price" and can't express a FOK
// limit order at all, which real exchanges support.
enum class TimeInForce { GTC, IOC, FOK };

}  // namespace riptide
