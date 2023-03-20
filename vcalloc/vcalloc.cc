#include "vcalloc/vcalloc.h"
#include "vcalloc/block.h"
#include "vcalloc/const.h"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <thread>

inline size_t GetControlIDByTID(std::thread::id tid) {
#if defined(VCALLOC_MULTI_THREAD)
  size_t hash = std::hash<std::thread::id>{}(tid);
  return hash % VCALLOC_POOL_COUNT;
#else
  return 0;
#endif
}

inline size_t GetControlIDByBlock(BlockHeader *block) {
  assert(block != 0);
#if defined(VCALLOC_MULTI_THREAD)
  size_t hash = std::hash<std::thread::id>{}(block->tid_);
  return hash % VCALLOC_POOL_COUNT;
#else
  return 0;
#endif
}

vcalloc::vcalloc(size_t size) {
  void *mem = malloc(size);
  CheckMem(mem);

  size_t splitted_size = AlignDown(size / VCALLOC_POOL_COUNT);
  std::ptrdiff_t control_mem = std::ptrdiff_t(mem);
  for (size_t i = 0; i < VCALLOC_POOL_COUNT; i++) {
    controls_[i] = reinterpret_cast<ControlHeader *>(control_mem);
    controls_[i]->Init();
    controls_[i]->InitPool((void *)(control_mem + sizeof(ControlHeader)),
                           splitted_size - sizeof(ControlHeader));
    control_mem += splitted_size;
  }
}

void *vcalloc::Malloc(size_t size) {
  const size_t adjust = AdjustRequestSize(size);
#if defined(VCALLOC_MULTI_THREAD)
  std::thread::id tid = std::this_thread::get_id();
  size_t ctl_id = GetControlIDByTID(tid);
  controls_[ctl_id]->lock_.lock();
  BlockHeader *block = controls_[ctl_id]->LocateFreeBlock(adjust);
  void *ptr = controls_[ctl_id]->BlockPrepareUsed(block, tid, adjust);
  controls_[ctl_id]->lock_.unlock();
  return ptr;
#else
  BlockHeader *block = controls_[0]->LocateFreeBlock(adjust);
  return controls_[ctl_id]->BlockPrepareUsed(block, adjust);
#endif
}

void vcalloc::Free(void *ptr) {
  // Don't attempt to free a NULL pointer
  if (!ptr) {
    return;
  }
  BlockHeader *block = BlockHeader::FromPtr(ptr);
  assert(!block->IsFree() && "block already marked as free");

  block->MarkAsFree();
  size_t ctl_id = GetControlIDByBlock(block);
#if defined(VCALLOC_MULTI_THREAD)
  controls_[ctl_id]->lock_.lock();
#endif
  block = controls_[ctl_id]->MergePrevBlock(block);
  block = controls_[ctl_id]->MergeNextBlock(block);
  controls_[ctl_id]->InsertBlock(block);
#if defined(VCALLOC_MULTI_THREAD)
  controls_[ctl_id]->lock_.unlock();
#endif
}

#define vcalloc_insist(x)                                                      \
  {                                                                            \
    assert(x);                                                                 \
    if (!(x)) {                                                                \
      status--;                                                                \
    }                                                                          \
  }

int vcalloc::Check() {
  int status = 0;
  // Check that the free lists and bitmaps are accurate
  for (int k = 0; k < VCALLOC_POOL_COUNT; k++) {
    for (int i = 0; i < kFLIndexCount; ++i) {
      for (int j = 0; j < kSLIndexCount; ++j) {
        const int fl_map = controls_[i]->fl_bitmap_ & (1U << i);
        const int sl_list = controls_[i]->sl_bitmap_[i];
        const int sl_map = sl_list & (1U << j);
        const BlockHeader *block = controls_[i]->blocks_[i][j];

        // Check that first- and second-level lists agree
        if (!fl_map) {
          vcalloc_insist(!sl_map && "second-level map must be null");
        }

        if (!sl_map) {
          vcalloc_insist(block == &controls_[i]->block_null_ &&
                         "block list must be null");
          continue;
        }

        // Check that there is at least one free block
        vcalloc_insist(sl_list && "no free blocks in second-level map");
        vcalloc_insist(block != &controls_[i]->block_null_ &&
                       "block should not be null");

        while (block != &controls_[i]->block_null_) {
          int fli, sli;
          vcalloc_insist(block->IsFree() && "block should be free");
          vcalloc_insist(!block->IsPrevFree() &&
                         "blocks should have coalesced");
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
  }

  return status;
}

static void default_walker(void *ptr, size_t size, int used) {
  printf("\t%p %s size: %d (%p)\n", ptr, used ? "used" : "free",
         (unsigned int)size, BlockHeader::FromPtr(ptr));
}

void vcalloc::Walk() {
  for (int i = 0; i < VCALLOC_POOL_COUNT; i++) {
    std::ptrdiff_t pool = std::ptrdiff_t(controls_[i]) + sizeof(ControlHeader);
    BlockHeader *block =
        reinterpret_cast<BlockHeader *>(pool - BlockHeader::Overhead());

    printf("\n");
    printf("used: %zu, max: %zu\n", controls_[i]->used_size_,
           controls_[i]->max_size_);
    while (block && !block->IsLast()) {
      default_walker(block->ToPtr(), block->Size(), !block->IsFree());
      block = block->Next();
    }
    printf("\n");
  }
}

static vcalloc allocator(VCALLOC_MEM_SIZE);

void *operator new(size_t size) {
#if defined(VCALLOC)
  void *ptr = allocator.Malloc(size);
#else
  void *ptr = malloc(size);
#endif
#if defined(VCALLOC_DEBUG)
  // std::cout << "status: " << allocator.Check() << std::endl;
  std::cout << "after new:" << std::endl;
  allocator.Walk();
#endif
  return ptr;
}

void operator delete(void *ptr) noexcept {
#if defined(VCALLOC)
  allocator.Free(ptr);
#else
  free(ptr);
#endif
#if defined(VCALLOC_DEBUG)
  // std::cout << allocator.Check() << std::endl;
  std::cout << "after delete:" << std::endl;
  allocator.Walk();
#endif
}

void *operator new[](size_t size) {
  // std::cout << "User Defined :: Operator new []" << std::endl;
#if defined(VCALLOC)
  void *ptr = allocator.Malloc(size);
#else
  void *ptr = malloc(size);
#endif
#if defined(VCALLOC_DEBUG)
  // std::cout << allocator.Check() << std::endl;
  std::cout << "after new[]:" << std::endl;
  allocator.Walk();
#endif
  return ptr;
}

void operator delete[](void *ptr) noexcept {
  // std::cout << "User Defined :: Operator delete[]" << std::endl;
#if defined(VCALLOC)
  allocator.Free(ptr);
#else
  free(ptr);
#endif
#if defined(VCALLOC_DEBUG)
  // std::cout << allocator.Check() << std::endl;
  std::cout << "after delete[]:" << std::endl;
  allocator.Walk();
#endif
}
