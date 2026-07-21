#include "riptide/fixed_size_pool.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <list>
#include <set>
#include <vector>

#include "riptide/pool_allocator.hpp"

namespace riptide {
namespace {

TEST(FixedSizePool, AllocateReturnsNonNull) {
  FixedSizePool pool(sizeof(std::uint64_t));
  void* block = pool.Allocate();
  EXPECT_NE(block, nullptr);
}

TEST(FixedSizePool, AllocateReturnsProperlyAlignedBlocks) {
  FixedSizePool pool(sizeof(std::uint64_t));
  for (int i = 0; i < 10; ++i) {
    void* block = pool.Allocate();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(block) % alignof(std::max_align_t), 0u);
  }
}

TEST(FixedSizePool, AllocatedBlocksAreDistinctAndWritable) {
  FixedSizePool pool(sizeof(std::uint64_t));
  std::set<void*> seen;
  std::vector<std::uint64_t*> blocks;

  for (int i = 0; i < 100; ++i) {
    auto* block = static_cast<std::uint64_t*>(pool.Allocate());
    ASSERT_TRUE(seen.insert(block).second) << "pool handed out the same block twice";
    *block = static_cast<std::uint64_t>(i);
    blocks.push_back(block);
  }

  // Writing through one block must never have clobbered another --
  // proof the pool isn't accidentally handing out overlapping memory.
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(*blocks[static_cast<std::size_t>(i)], static_cast<std::uint64_t>(i));
  }
}

TEST(FixedSizePool, DeallocateThenAllocateReusesTheSameBlock) {
  FixedSizePool pool(sizeof(std::uint64_t));
  void* first = pool.Allocate();
  pool.Deallocate(first);
  void* second = pool.Allocate();

  // LIFO free list: the most recently freed block is the next one handed
  // out. This is what proves recycling is actually happening, not just
  // the pool silently growing forever.
  EXPECT_EQ(first, second);
}

TEST(FixedSizePool, GrowsBeyondInitialCapacityWithoutCorruption) {
  constexpr std::size_t kInitialCapacity = 4;
  FixedSizePool pool(sizeof(std::uint64_t), kInitialCapacity);

  std::set<void*> seen;
  for (int i = 0; i < 50; ++i) {  // well beyond kInitialCapacity, forces growth
    void* block = pool.Allocate();
    EXPECT_TRUE(seen.insert(block).second) << "pool handed out a duplicate block after growing";
  }
}

// PoolAllocator<T> is what actually gets used (backing std::list, etc.)
// -- exercised here via a simple container to confirm the allocator
// mechanics (allocate/deallocate/rebind) work correctly end to end, not
// just FixedSizePool in isolation.
TEST(PoolAllocator, WorksAsAStdListAllocator) {
  std::list<int, PoolAllocator<int>> values;
  for (int i = 0; i < 200; ++i) {
    values.push_back(i);
  }

  int expected = 0;
  for (int v : values) {
    EXPECT_EQ(v, expected++);
  }
  EXPECT_EQ(expected, 200);

  // Erase from the middle and both ends -- exercises deallocate paths,
  // not just allocate.
  values.pop_front();
  values.pop_back();
  auto middle = values.begin();
  std::advance(middle, values.size() / 2);
  values.erase(middle);

  EXPECT_EQ(values.size(), 197u);
}

TEST(PoolAllocator, AllInstancesForTheSameTypeAreEqual) {
  PoolAllocator<int> a;
  PoolAllocator<int> b;
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
}

}  // namespace
}  // namespace riptide
