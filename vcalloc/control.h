#pragma once

#include "vcalloc/block.h"
#include "vcalloc/common.h"

#include <assert.h>
#include <cstdio>
#include <limits>
#include <mutex>
#include <pthread.h>

static void MappingInsert(size_t size, int *fli, int *sli) {
  int fl, sl;
  if (size < kSmallBlockSize) {
    // Store small blocks in first list
    fl = 0;
    sl = (int)size / (kSmallBlockSize / kSLIndexCount);
  } else {
    fl = vcalloc_fls_sizet(size);
    sl = (int)((size >> (fl - kSLIndexCountLog2)) ^ (1 << kSLIndexCountLog2));
    fl -= (kFLIndexShift - 1);
  }
  *fli = fl;
  *sli = sl;
}

static void MappingSearch(size_t size, int *fli, int *sli) {
  if (size >= kSmallBlockSize) {
    const size_t round =
        (1 << (vcalloc_fls_sizet(size) - kSLIndexCountLog2)) - 1;
    size += round;
  }
  MappingInsert(size, fli, sli);
}

const size_t NULL_OFFSET = std::numeric_limits<size_t>::max();

typedef struct ControlHeader {
  pthread_mutex_t mtx_;
  pthread_cond_t cond_;

  pthread_mutex_t lock_;

  // Statistic
#if defined(VCALLOC_STATISTIC)
  size_t used_size_;
  size_t max_size_;
#endif

  // Bitmaps for free lists
  unsigned int fl_bitmap_;
  unsigned int sl_bitmap_[kFLIndexCount];

  // Head of free lists
  size_t blocks_offset_[kFLIndexCount][kSLIndexCount];

  void Init() {
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&mtx_, &mutex_attr);

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&cond_, &cond_attr);

    pthread_mutexattr_t lock_attr;
    pthread_mutexattr_init(&lock_attr);
    pthread_mutexattr_setpshared(&lock_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&lock_, &lock_attr);

    fl_bitmap_ = 0;
    for (int i = 0; i < kFLIndexCount; i++) {
      sl_bitmap_[i] = 0;
      for (int j = 0; j < kSLIndexCount; j++) {
        blocks_offset_[i][j] = NULL_OFFSET;
      }
    }
  }

  void InitPool(void *mem, size_t size) {
    CheckMem(mem);

    BlockHeader *block;
    BlockHeader *next;

    const size_t pool_overhead = 2 * BlockHeader::Overhead();
    const size_t pool_size = AlignDown(size - pool_overhead);

    if (pool_size < BlockHeader::MinSize() ||
        pool_size > BlockHeader::MaxSize()) {
#if defined(VCALLOC_64BIT)
      printf("InitPool: Memory size must be between 0x%x and 0x%x00 bytes.\n",
             (unsigned int)(pool_overhead + BlockHeader::MinSize()),
             (unsigned int)((pool_overhead + BlockHeader::MaxSize()) / 256));
#else
      printf("InitPool: Memory size must be between %u and %u bytes.\n",
             (unsigned int)(pool_overhead + BlockHeader::MinSize()),
             (unsigned int)(pool_overhead + BlockHeader::MaxSize()));
#endif
      return;
    }

    /*
    ** Create the main free block. Offset the start of the block slightly
    ** so that the prev_phys_block field falls outside of the pool -
    ** it will never be used.
    */
    block = reinterpret_cast<BlockHeader *>(std::ptrdiff_t(mem) -
                                            BlockHeader::Overhead());
    block->SetSize(pool_size);
    block->SetFree();
    block->SetPrevUsed();
    InsertBlock(block);

    // Split the block to create a zero-size sentinel block
    next = block->LinkNext();
    next->SetSize(0);
    next->SetUsed();
    next->SetPrevFree();

#if defined(VCALLOC_STATISTIC)
    used_size_ = BlockHeader::Overhead();
    max_size_ = pool_size;
#endif
  }

  BlockHeader* ApplyBlockOffset(size_t offset) {
    if (offset == NULL_OFFSET) {
      return nullptr;
    }
    return reinterpret_cast<BlockHeader*>(std::ptrdiff_t(this) + sizeof(ControlHeader) + offset);
  }

  size_t GetBlockOffset(BlockHeader *block) {
    if (!block) {
      return NULL_OFFSET;
    }
    return (size_t)(std::ptrdiff_t(block) - std::ptrdiff_t(this) - sizeof(ControlHeader));
  }

  // Insert a given block into the free list
  void InsertBlock(BlockHeader *block) {
    int fl, sl;
    MappingInsert(block->Size(), &fl, &sl);
    BlockHeader *current = ApplyBlockOffset(blocks_offset_[fl][sl]);
    assert(block && "cannot insert a null entry into the free list");
    block->next_free_ = GetBlockOffset(current);
    block->prev_free_ = NULL_OFFSET;
    if (current) {
      current->prev_free_ = GetBlockOffset(block);
    }

    assert(block->ToPtr() == AlignPtr(block->ToPtr()) &&
           "block not aligned properly");

#if defined(VCALLOC_STATISTIC)
    used_size_ -= (block->Size() + BlockHeader::Overhead());
#endif

    /*
    ** Insert the new block at the head of the list, and mark the first-
    ** and second-level bitmaps appropriately.
    */
    blocks_offset_[fl][sl] = GetBlockOffset(block);
    fl_bitmap_ |= (1U << fl);
    sl_bitmap_[fl] |= (1U << sl);
  }

  BlockHeader *SearchSuitableBlock(int *fli, int *sli) {
    int fl = *fli;
    int sl = *sli;
    /*
    ** First, search for a block in the list associated with the given
    ** fl/sl index.
    */
    unsigned int sl_map = sl_bitmap_[fl] & (~0U << sl);
    if (!sl_map) {
      // No block exists. Search in the next largest first-level list
      const unsigned int fl_map = fl_bitmap_ & (~0U << (fl + 1));
      if (!fl_map) {
        // No free blocks available, memory has been exhausted
        return 0;
      }

      fl = vcalloc_ffs(fl_map);
      *fli = fl;
      sl_map = sl_bitmap_[fl];
    }
    assert(sl_map && "internal error - second level bitmap is null");
    sl = vcalloc_ffs(sl_map);
    *sli = sl;

    // Return the first block in the free list
    return ApplyBlockOffset(blocks_offset_[fl][sl]);
  }

  BlockHeader *LocateFreeBlock(size_t size) {
    int fl = 0, sl = 0;
    BlockHeader *block = 0;
    if (size) {
      MappingSearch(size, &fl, &sl);
      if (fl < kFLIndexCount) {
        block = SearchSuitableBlock(&fl, &sl);
      }
    }
    if (block) {
      assert(block->Size() >= size);
      RemoveFreeBlock(block, fl, sl);
    }
    return block;
  }

  // Remove a free block from the free list
  void RemoveFreeBlock(BlockHeader *block, int fl, int sl) {
    BlockHeader *prev = ApplyBlockOffset(block->prev_free_);
    BlockHeader *next = ApplyBlockOffset(block->next_free_);
    if (next) {
      next->prev_free_ = GetBlockOffset(prev);
    }
    if (prev) {
      prev->next_free_ = GetBlockOffset(next);
    }

    // If this block is the head of the free list, set new head
    if (ApplyBlockOffset(blocks_offset_[fl][sl]) == block) {
      blocks_offset_[fl][sl] = GetBlockOffset(next);

      // If the new head is null, clear the bitmap
      if (GetBlockOffset(next) == NULL_OFFSET) {
        sl_bitmap_[fl] &= ~(1U << sl);

        // If the second bitmap is now empty, clear the fl bitmap
        if (!sl_bitmap_[fl]) {
          fl_bitmap_ &= ~(1U << fl);
        }
      }
    }

#if defined(VCALLOC_STATISTIC)
    used_size_ += (block->Size() + BlockHeader::Overhead());
#endif
  }

  void *BlockPrepareUsed(BlockHeader *block, size_t size) {
    if (!block) {
      return 0;
    }
    assert(size && "size must be non-zero");
    BlockTrimFree(block, size);
    block->MarkAsUsed();
    return block->ToPtr();
  }

  // Trim any trailing block space off the end of a block, return to pool
  void BlockTrimFree(BlockHeader *block, size_t size) {
    assert(block->IsFree() && "block must be free");
    if (!block->CanSplit(size)) {
      return;
    }
    BlockHeader *remaining_block = block->Split(size);
    block->LinkNext();
    remaining_block->SetPrevFree();
    InsertBlock(remaining_block);
  }

  // Merge a just-freed block with an adjacent previous free block
  BlockHeader *MergePrevBlock(BlockHeader *block) {
    if (!block->IsPrevFree()) {
      return block;
    }
    BlockHeader *prev = block->Prev();
    assert(prev && "prev physical block can't be null");
    assert(prev->IsFree() && "prev block is not free though marked as such");
    RemoveBlock(prev);
    block = AbsorbBlock(prev, block);
    return block;
  }

  // Merge a just-freed block with an adjacent free block
  BlockHeader *MergeNextBlock(BlockHeader *block) {
    BlockHeader *next = block->Next();
    assert(next && "next physical block can't be null");
    if (!next->IsFree()) {
      return block;
    }
    assert(!block->IsLast() && "previous block can't be last");
    RemoveBlock(next);
    AbsorbBlock(block, next);
    return block;
  }

  void RemoveBlock(BlockHeader *block) {
    int fl, sl;
    MappingInsert(block->Size(), &fl, &sl);
    RemoveFreeBlock(block, fl, sl);
  }

  // Absorb a free block's storage into an adjacent previous free block
  BlockHeader *AbsorbBlock(BlockHeader *prev, BlockHeader *block) {
    assert(!prev->IsLast() && "previous block can't be last");
    // Note: Leaves flags untouched
    prev->size_ += block->Size() + BlockHeader::Overhead();
    prev->LinkNext();
    return prev;
  }

} ControlHeader;