#pragma once

#include "vcalloc/common.h"
#include "vcalloc/const.h"

#include <assert.h>
#include <thread>

constexpr size_t block_header_free_bit = 1 << 0;
constexpr size_t block_header_prev_free_bit = 1 << 1;

typedef struct BlockHeader {
  // Points to the previous physical block
  size_t prev_phys_block_;

  // The size of this block, excluding the block header
  size_t size_;

  // Next and previous free blocks
  size_t next_free_;
  size_t prev_free_;

  static size_t Overhead() {
    return sizeof(size_t);
  }

  static size_t StartOffset() {
    return offsetof(BlockHeader, size_) + sizeof(size_t);
  };

  static size_t MinSize() {
    return sizeof(BlockHeader) - sizeof(struct BlockHeader *);
  }

  static size_t MaxSize() { return size_t(1) << kFLIndexMax; }

  size_t Size() const {
    return size_ & ~(block_header_free_bit | block_header_prev_free_bit);
  }

  void SetSize(size_t new_size) {
    size_ = new_size |
            (size_ & (block_header_free_bit | block_header_prev_free_bit));
  }

  bool IsFree() const { return size_ & block_header_free_bit; }

  void SetFree() { size_ |= block_header_free_bit; }

  void SetUsed() { size_ &= ~block_header_free_bit; }

  bool IsPrevFree() const { return size_ & block_header_prev_free_bit; }

  void SetPrevFree() { size_ |= block_header_prev_free_bit; }

  void SetPrevUsed() { size_ &= ~block_header_prev_free_bit; }

  bool IsLast() const { return Size() == 0; }

  void *ToPtr() const { return (void *)(std::ptrdiff_t(this) + StartOffset()); }

  static BlockHeader *FromPtr(const void *ptr) {
    return reinterpret_cast<BlockHeader *>(std::ptrdiff_t(ptr) - StartOffset());
  }

  void MarkAsFree() {
    BlockHeader *next = LinkNext();
    next->SetPrevFree();
    SetFree();
  }

  void MarkAsUsed() {
    Next()->SetPrevUsed();
    SetUsed();
  }

  BlockHeader *Prev() const {
    assert(IsPrevFree() && "previous block must be free");
    return (BlockHeader*)(std::ptrdiff_t(this) - prev_phys_block_);
  }

  BlockHeader *Next() const {
    BlockHeader *next = reinterpret_cast<BlockHeader *>(
        std::ptrdiff_t(ToPtr()) + Size() - sizeof(BlockHeader *));
    assert(!IsLast());
    return next;
  }

  BlockHeader *LinkNext() {
    BlockHeader *next = Next();
    next->prev_phys_block_ = std::ptrdiff_t(next) - std::ptrdiff_t(this);
    return next;
  }

#if defined (VCALLOC_MULTI_THREAD)
  bool CanSplit(size_t size) { return Size() >= MinSize() + Overhead() + size; }
#else
  bool CanSplit(size_t size) { return Size() >= sizeof(BlockHeader) + size; }
#endif

  // Split a block into two, the second of which is free
  BlockHeader *Split(size_t size) {
    // Calculate the amount of space left in the remaining block
    BlockHeader *remaining = reinterpret_cast<BlockHeader *>(
        std::ptrdiff_t(ToPtr()) + size - sizeof(BlockHeader *));
    const size_t remain_size = Size() - (size + Overhead());

    assert(remaining->ToPtr() == AlignPtr(remaining->ToPtr()) &&
           "remaining block not aligned properly");
    assert(Size() == remain_size + size + Overhead());
    remaining->SetSize(remain_size);
    assert(remaining->Size() >= MinSize() && "block split with invalid size");
    SetSize(size);
    remaining->MarkAsFree();
    return remaining;
  }

} BlockHeader;

static size_t AdjustRequestSize(size_t size) {
  if (VCCALLOC_unlikely(!size)) {
    return 0;
  }
  size_t aligned = AlignUp(size);
  if (aligned < BlockHeader::MaxSize()) {
    return Max(aligned, BlockHeader::MinSize());
  }
  return 0;
}
