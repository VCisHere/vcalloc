#include "vcalloc/vcalloc.h"
#include <cstdio>
#include <iostream>

#define VCMALLOC
#define VCMALLOC_DEBUG

// void vcalloc::InitPool(void *mem, size_t size) {
//   CheckMem(mem);

//   BlockHeader *block;
//   BlockHeader *next;

//   const size_t pool_overhead = 2 * BlockHeader::Overhead();
//   const size_t pool_size = AlignDown(size - pool_overhead);

//   if (pool_size < BlockHeader::MinSize() ||
//       pool_size > BlockHeader::MaxSize()) {
// #if defined(VCALLOC_64BIT)
//     printf("InitPool: Memory size must be between 0x%x and 0x%x00 bytes.\n",
//            (unsigned int)(pool_overhead + BlockHeader::MinSize()),
//            (unsigned int)((pool_overhead + BlockHeader::MaxSize()) / 256));
// #else
//     printf("InitPool: Memory size must be between %u and %u bytes.\n",
//            (unsigned int)(pool_overhead + BlockHeader::MinSize()),
//            (unsigned int)(pool_overhead + BlockHeader::MaxSize()));
// #endif
//     return;
//   }

//   /*
//   ** Create the main free block. Offset the start of the block slightly
//   ** so that the prev_phys_block field falls outside of the pool -
//   ** it will never be used.
//   */
//   block = reinterpret_cast<BlockHeader *>(std::ptrdiff_t(mem) -
//                                           BlockHeader::Overhead());
//   block->SetSize(pool_size);
//   block->SetFree();
//   block->SetPrevUsed();
//   control_->InsertBlock(block);

//   // Split the block to create a zero-size sentinel block
//   next = block->LinkNext();
//   next->SetSize(0);
//   next->SetUsed();
//   next->SetPrevFree();
// }

vcalloc::vcalloc(void *mem, size_t size) {
  CheckMem(mem);
  control_ = reinterpret_cast<ControlHeader *>(mem);
  control_->Init();
  control_->InitPool((void *)(std::ptrdiff_t(mem) + sizeof(ControlHeader)),
                     size - sizeof(ControlHeader));
}

void *vcalloc::Malloc(size_t size) {
  const size_t adjust = AdjustRequestSize(size);
  BlockHeader *block = control_->LocateFreeBlock(adjust);
  return control_->BlockPrepareUsed(block, adjust);
}

void vcalloc::Free(void *ptr) {
  // Don't attempt to free a NULL pointer
  if (!ptr) {
    return;
  }
  BlockHeader *block = BlockHeader::FromPtr(ptr);
  assert(!block->IsFree() && "block already marked as free");

  block->MarkAsFree();
  block = control_->MergePrevBlock(block);
  block = control_->MergeNextBlock(block);
  control_->InsertBlock(block);
}

#define vcalloc_insist(x)                                                      \
  {                                                                            \
    assert(x);                                                                 \
    if (!(x)) {                                                                \
      status--;                                                                \
    }                                                                          \
  }

int vcalloc::Check() {
  int i, j;
  int status = 0;
  // Check that the free lists and bitmaps are accurate
  for (i = 0; i < kFLIndexCount; ++i) {
    for (j = 0; j < kSLIndexCount; ++j) {
      const int fl_map = control_->fl_bitmap_ & (1U << i);
      const int sl_list = control_->sl_bitmap_[i];
      const int sl_map = sl_list & (1U << j);
      const BlockHeader *block = control_->blocks_[i][j];

      // Check that first- and second-level lists agree
      if (!fl_map) {
        vcalloc_insist(!sl_map && "second-level map must be null");
      }

      if (!sl_map) {
        vcalloc_insist(block == &control_->block_null_ &&
                       "block list must be null");
        continue;
      }

      // Check that there is at least one free block
      vcalloc_insist(sl_list && "no free blocks in second-level map");
      vcalloc_insist(block != &control_->block_null_ &&
                     "block should not be null");

      while (block != &control_->block_null_) {
        int fli, sli;
        vcalloc_insist(block->IsFree() && "block should be free");
        vcalloc_insist(!block->IsPrevFree() && "blocks should have coalesced");
        vcalloc_insist(!block->Next()->IsFree() &&
                       "blocks should have coalesced");
        vcalloc_insist(block->Next()->IsPrevFree() && "block should be free");
        vcalloc_insist(block->Size() >= BlockHeader::MinSize() &&
                       "block not minimum size");

        MappingInsert(block->Size(), &fli, &sli);
        vcalloc_insist(fli == i && sli == j &&
                       "block size indexed in wrong list");
        block = block->next_free_;
      }
    }
  }

  return status;
}

static void default_walker(void *ptr, size_t size, int used) {
  printf("\t%p %s size: %d (%p)\n", ptr, used ? "used" : "free",
         (unsigned int)size, BlockHeader::FromPtr(ptr));
}

void vcalloc::Walk() {
  std::ptrdiff_t pool = std::ptrdiff_t(control_) + sizeof(ControlHeader);
  BlockHeader *block =
      reinterpret_cast<BlockHeader *>(pool - BlockHeader::Overhead());

  printf("\n");
  printf("used: %zu, max: %zu\n", control_->used_size_, control_->max_size_);
  while (block && !block->IsLast()) {
    default_walker(block->ToPtr(), block->Size(), !block->IsFree());
    block = block->Next();
  }
  printf("\n");
}

// static size_t mem_size = 1024 * 1024 * 1024;
static size_t mem_size = 1024 * 1024;
static void *mem = malloc(mem_size);
static vcalloc allocator(mem, mem_size);

void *operator new(size_t size) {
#ifdef VCMALLOC
  void *ptr = allocator.Malloc(size);
#else
  void *ptr = malloc(size);
#endif
#ifdef VCMALLOC_DEBUG
  // std::cout << "status: " << allocator.Check() << std::endl;
  std::cout << "after new:" << std::endl;
  allocator.Walk();
#endif
  return ptr;
}

void operator delete(void *ptr) noexcept {
#ifdef VCMALLOC
  allocator.Free(ptr);
#else
  free(ptr);
#endif
#ifdef VCMALLOC_DEBUG
  // std::cout << allocator.Check() << std::endl;
  std::cout << "after delete:" << std::endl;
  allocator.Walk();
#endif
}

void *operator new[](size_t size) {
  // std::cout << "User Defined :: Operator new []" << std::endl;
#ifdef VCMALLOC
  void *ptr = allocator.Malloc(size);
#else
  void *ptr = malloc(size);
#endif
#ifdef VCMALLOC_DEBUG
  // std::cout << allocator.Check() << std::endl;
  std::cout << "after new[]:" << std::endl;
  allocator.Walk();
#endif
  return ptr;
}

void operator delete[](void *ptr) noexcept {
  // std::cout << "User Defined :: Operator delete[]" << std::endl;
#ifdef VCMALLOC
  allocator.Free(ptr);
#else
  free(ptr);
#endif
#ifdef VCMALLOC_DEBUG
  // std::cout << allocator.Check() << std::endl;
  std::cout << "after delete[]:" << std::endl;
  allocator.Walk();
#endif
}
