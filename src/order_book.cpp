#include "riptide/order_book.hpp"

#include <cassert>
#include <utility>

namespace riptide {

bool OrderBook::insert(Order order) {
  assert(order.price.has_value());

  if (index_.contains(order.id)) return false;

  const Side side = order.side;
  const Price price = *order.price;
  const OrderId id = order.id;
  const Quantity remaining = order.remaining;

  PriceLevel& level = (side == Side::Buy) ? bids_[price] : asks_[price];
  level.orders.push_back(std::move(order));
  level.total_quantity += remaining;

  index_[id] = OrderLocation{side, price, std::prev(level.orders.end())};
  return true;
}

bool OrderBook::remove(OrderId id) {
  auto found = index_.find(id);
  if (found == index_.end()) return false;

  const OrderLocation loc = found->second;
  PriceLevel* level = find_level(loc.side, loc.price);
  assert(level != nullptr);

  level->total_quantity -= loc.it->remaining;
  level->orders.erase(loc.it);
  index_.erase(found);
  erase_level_if_empty(loc.side, loc.price);
  return true;
}

Order* OrderBook::find(OrderId id) {
  auto found = index_.find(id);
  return found == index_.end() ? nullptr : &*found->second.it;
}

std::optional<Price> OrderBook::best_price(Side side) const {
  if (side == Side::Buy) {
    return bids_.empty() ? std::nullopt : std::optional{bids_.begin()->first};
  }
  return asks_.empty() ? std::nullopt : std::optional{asks_.begin()->first};
}

Order* OrderBook::front(Side side, Price price) {
  PriceLevel* level = find_level(side, price);
  if (level == nullptr || level->orders.empty()) return nullptr;
  return &level->orders.front();
}

std::optional<Quantity> OrderBook::level_quantity(Side side, Price price) const {
  const PriceLevel* level = find_level(side, price);
  return level == nullptr ? std::nullopt : std::optional{level->total_quantity};
}

void OrderBook::fill_front(Side side, Price price, Quantity fill_qty) {
  PriceLevel* level = find_level(side, price);
  assert(level != nullptr && !level->orders.empty());

  Order& resting = level->orders.front();
  assert(fill_qty <= resting.remaining);

  resting.remaining -= fill_qty;
  level->total_quantity -= fill_qty;

  if (resting.remaining == 0) {
    index_.erase(resting.id);
    level->orders.pop_front();
    erase_level_if_empty(side, price);
  }
}

bool OrderBook::set_remaining_in_place(OrderId id, Quantity new_remaining) {
  auto found = index_.find(id);
  if (found == index_.end()) return false;

  const OrderLocation& loc = found->second;
  PriceLevel* level = find_level(loc.side, loc.price);
  assert(level != nullptr);

  Order& order = *loc.it;
  level->total_quantity = level->total_quantity - order.remaining + new_remaining;
  order.remaining = new_remaining;
  return true;
}

PriceLevel* OrderBook::find_level(Side side, Price price) {
  if (side == Side::Buy) {
    auto it = bids_.find(price);
    return it == bids_.end() ? nullptr : &it->second;
  }
  auto it = asks_.find(price);
  return it == asks_.end() ? nullptr : &it->second;
}

const PriceLevel* OrderBook::find_level(Side side, Price price) const {
  if (side == Side::Buy) {
    auto it = bids_.find(price);
    return it == bids_.end() ? nullptr : &it->second;
  }
  auto it = asks_.find(price);
  return it == asks_.end() ? nullptr : &it->second;
}

void OrderBook::erase_level_if_empty(Side side, Price price) {
  if (side == Side::Buy) {
    auto it = bids_.find(price);
    if (it != bids_.end() && it->second.orders.empty()) bids_.erase(it);
  } else {
    auto it = asks_.find(price);
    if (it != asks_.end() && it->second.orders.empty()) asks_.erase(it);
  }
}

}  // namespace riptide
