#pragma once

#include <cstddef>
#include <type_traits>

#include "riptide/fixed_size_pool.hpp"

namespace riptide {

// A minimal std::allocator-compliant type backed by a FixedSizePool —
// intended for use as e.g. std::list<Order, PoolAllocator<Order>>'s
// allocator, so every node insert/erase pulls from a pre-allocated slab
// (via free-list reuse) instead of calling malloc/free.
//
// The pool is a function-local static, one per distinct instantiation of
// PoolAllocator<T> — every PoolAllocator<T> anywhere in the process
// shares it. This matters because standard containers *rebind* the
// allocator you give them to their own internal node type: passing
// PoolAllocator<Order> to std::list doesn't allocate Order objects
// directly, it gets converted (via the templated constructor below) to
// PoolAllocator<SomeInternalListNodeType>, and that's the type whose
// static pool actually ends up serving every allocation. A per-instance
// pool (e.g. one owned by each OrderBook) would have to solve "how does
// a rebound allocator find the original instance's pool," which a
// process-wide pool per node type sidesteps entirely — at the cost of
// pool memory not being freed until process exit, which is fine for a
// matching engine that's expected to run for a long time anyway.
template <typename T>
class PoolAllocator {
 public:
  using value_type = T;

  PoolAllocator() noexcept = default;

  // The converting constructor std::list/map/unordered_map actually use
  // to rebind this to their internal node type — intentionally ignores
  // `other`, since a source PoolAllocator<U>'s pool is sized for U, not
  // T, and can't be reused across the type change.
  template <typename U>
  PoolAllocator(const PoolAllocator<U>&) noexcept {}  // NOLINT(google-explicit-constructor)

  T* allocate(std::size_t n) {
    if (n != 1) {
      // This allocator only ever serves single-node allocations — list/
      // tree/hashtable nodes are always allocated one at a time. Falling
      // back to the global allocator for anything else keeps this
      // correct rather than silently wrong if it's ever used somewhere
      // that batch-allocates.
      return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    return static_cast<T*>(Pool().Allocate());
  }

  void deallocate(T* ptr, std::size_t n) noexcept {
    if (n != 1) {
      ::operator delete(ptr);
      return;
    }
    Pool().Deallocate(ptr);
  }

  // All instances of PoolAllocator<T> share the same static pool, so any
  // two are always interchangeable — memory allocated by one can always
  // be deallocated by another, satisfying the Allocator contract's
  // equality requirement trivially.
  template <typename U>
  bool operator==(const PoolAllocator<U>&) const noexcept {
    return std::is_same_v<T, U>;
  }
  template <typename U>
  bool operator!=(const PoolAllocator<U>& other) const noexcept {
    return !(*this == other);
  }

 private:
  static FixedSizePool& Pool() {
    static FixedSizePool pool(sizeof(T));
    return pool;
  }
};

}  // namespace riptide
