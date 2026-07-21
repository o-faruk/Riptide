#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace riptide {

// A fixed-block-size memory pool: pre-allocates slabs of `block_size`
// bytes and hands out individual blocks via an intrusive free list (a
// free block's own storage doubles as the list node — no separate
// bookkeeping structure, and O(1) allocate/deallocate). Backs
// PoolAllocator (pool_allocator.hpp), which is what actually wires this
// into std::list/std::map/std::unordered_map as a drop-in allocator.
//
// Grows by allocating a new, larger slab when the free list runs dry
// (doubling, same amortized-growth rationale as std::vector), and never
// returns memory to the system allocator until the pool itself is
// destroyed — blocks are recycled, not freed, which is the entire point
// for a hot path where orders are constantly added and removed and
// repeated malloc/free of same-sized blocks is exactly the cost being
// eliminated.
//
// Not thread-safe: the whole project is single-threaded through Phase 4
// (see docs/OPTIMIZATION_LOG.md's candidate list — a lock-free SPSC ring
// buffer is a *later*, separate candidate, gated on threading actually
// being introduced). Adding synchronization now would be exactly the
// "handle a scenario that can't currently happen" the project's own
// rules discourage; revisit if/when that changes.
class FixedSizePool {
 public:
  explicit FixedSizePool(std::size_t block_size, std::size_t initial_capacity = 256);

  void* Allocate();
  void Deallocate(void* ptr);

 private:
  struct FreeNode {
    FreeNode* next;
  };

  void AddSlab(std::size_t capacity);

  std::size_t block_size_;
  std::size_t next_slab_capacity_;
  std::vector<std::unique_ptr<std::byte[]>> slabs_;
  FreeNode* free_list_ = nullptr;
};

}  // namespace riptide
