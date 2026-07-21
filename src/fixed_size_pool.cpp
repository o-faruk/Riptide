#include "riptide/fixed_size_pool.hpp"

#include <algorithm>
#include <cassert>
#include <new>

namespace riptide {

namespace {

// Round up to a multiple of `alignof(std::max_align_t)` so every block in
// a slab starts at a properly-aligned address for any type that might be
// placed there (pointers, Order structs, etc.) — new[] itself only
// guarantees this alignment for the slab's own start, not for each
// block_size_-offset within it unless block_size_ is itself a multiple.
std::size_t RoundUpToAlignment(std::size_t size) {
  constexpr std::size_t kAlignment = alignof(std::max_align_t);
  return ((size + kAlignment - 1) / kAlignment) * kAlignment;
}

}  // namespace

FixedSizePool::FixedSizePool(std::size_t block_size, std::size_t initial_capacity)
    : block_size_(RoundUpToAlignment(std::max(block_size, sizeof(FreeNode)))),
      next_slab_capacity_(initial_capacity) {
  assert(initial_capacity > 0);
  AddSlab(next_slab_capacity_);
}

void FixedSizePool::AddSlab(std::size_t capacity) {
  auto slab = std::make_unique<std::byte[]>(block_size_ * capacity);
  std::byte* base = slab.get();
  slabs_.push_back(std::move(slab));

  // Thread every block in the new slab onto the free list. Placement-new
  // is what makes this well-defined under the object-lifetime rules
  // (this raw storage becomes "a FreeNode" here, and later becomes
  // whatever the caller actually needs once handed out by Allocate) --
  // FreeNode is trivially destructible, so nothing needs to undo this.
  for (std::size_t i = 0; i < capacity; ++i) {
    void* block = base + i * block_size_;
    auto* node = ::new (block) FreeNode;
    node->next = free_list_;
    free_list_ = node;
  }
}

void* FixedSizePool::Allocate() {
  if (free_list_ == nullptr) {
    next_slab_capacity_ *= 2;
    AddSlab(next_slab_capacity_);
  }
  FreeNode* node = free_list_;
  free_list_ = free_list_->next;
  return node;
}

void FixedSizePool::Deallocate(void* ptr) {
  auto* node = ::new (ptr) FreeNode;  // this memory is a FreeNode again
  node->next = free_list_;
  free_list_ = node;
}

}  // namespace riptide
