#include "vcalloc/vcalloc.h"
#include "vcalloc/block.h"
#include "vcalloc/common.h"

#include <cassert>
#include <cstdio>
#include <pthread.h>
#include <sstream>
#include <sys/mman.h>
#include <sys/shm.h>
#include <thread>

vcalloc &Global::GetAllocator() {
  static vcalloc allocator;
  return allocator;
}

void GetKeyAndSize(key_t &key, size_t &size) {
  const char *mem_name = std::getenv("VCALLOC_MEM_NAME");
  const char *mem_size = std::getenv("VCALLOC_MEM_SIZE");
  if (mem_name) {
    std::stringstream s_mem_name(mem_name);
    s_mem_name >> key;
  } else {
    key = 12345;
  }
  if (mem_size) {
    std::stringstream s_mem_size(mem_size);
    s_mem_size >> size;
  } else {
    size = 1024 * 1024 * 512;
  }
}

vcalloc::vcalloc() {
  key_t key;
  size_t size;
  GetKeyAndSize(key, size);

  int shmid = shmget(key, size, IPC_CREAT | 0666);
  if (shmid < 0) {
    exit(1);
  }
  struct shmid_ds shminfo;
  if (shmctl(shmid, IPC_STAT, &shminfo) == -1) {
    exit(1);
  }
  void *mem = (void *)shmat(shmid, 0, 0);
  if (mem == NULL) {
    exit(1);
  }
  CheckMem(mem);

  std::ptrdiff_t control_mem = std::ptrdiff_t(mem);
  control_ = reinterpret_cast<ControlHeader *>(control_mem);
  if (shminfo.shm_nattch == 0) {
    control_->Init();
    control_->InitPool((void *)(control_mem + sizeof(ControlHeader)),
                       size - sizeof(ControlHeader));
  }
}

void *vcalloc::Malloc(size_t size) {
  const size_t adjust = AdjustRequestSize(size);
  BlockHeader *block = nullptr;
  void *ptr = nullptr;
  while (true) {
    pthread_mutex_lock(&control_->lock_);
    block = control_->LocateFreeBlock(adjust);
    if (block == nullptr) {
      // full, wait
      pthread_mutex_unlock(&control_->lock_);
      pthread_cond_wait(&control_->cond_, &control_->mtx_);
      continue;
    }
    ptr = control_->BlockPrepareUsed(block, adjust);
    pthread_mutex_unlock(&control_->lock_);
    break;
  }
  return ptr;
}

void vcalloc::Free(void *ptr) {
  if (!ptr) {
    return;
  }

  pthread_mutex_lock(&control_->lock_);

  BlockHeader *block = BlockHeader::FromPtr(ptr);
  assert(!block->IsFree() && "block already marked as free");

  block->MarkAsFree();
  block = control_->MergePrevBlock(block);
  block = control_->MergeNextBlock(block);
  control_->InsertBlock(block);

  pthread_mutex_unlock(&control_->lock_);
  pthread_cond_broadcast(&control_->cond_);
}

float vcalloc::GetUsageRate() {
#if defined(VCALLOC_STATISTIC)
  pthread_mutex_lock(&control_->lock_);
  float usage = float(control_->used_size_) / float(control_->max_size_);
  pthread_mutex_unlock(&control_->lock_);
  return usage;
#endif
  return 0;
}

size_t vcalloc::ToOffset(void *ptr) {
  BlockHeader *block = BlockHeader::FromPtr(ptr);
  return control_->GetBlockOffset(block);
}

void *vcalloc::FromOffset(size_t offset) {
  BlockHeader *block = control_->ApplyBlockOffset(offset);
  return block->ToPtr();
}

#if defined(VCALLOC)
void *operator new(size_t size) { return Global::GetAllocator().Malloc(size); }

void operator delete(void *ptr) noexcept { Global::GetAllocator().Free(ptr); }

void *operator new[](size_t size) {
  return Global::GetAllocator().Malloc(size);
}

void operator delete[](void *ptr) noexcept { Global::GetAllocator().Free(ptr); }
#endif