#pragma once

#include <assert.h>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>

#include "vcalloc/common.h"
#include "vcalloc/const.h"
#include "vcalloc/control.h"

struct ControlHeader;
struct BlockHeader;

#define VCALLOC
#define VCALLOC_DEBUG

#define VCALLOC_MEM_SIZE 100 * 1024 * 1024
#if defined(VCALLOC_MULTI_THREAD)
#define VCALLOC_POOL_COUNT 4
#else
#define VCALLOC_POOL_COUNT 1
#endif

class vcalloc {
private:
  ControlHeader *controls_[VCALLOC_POOL_COUNT];

public:
  vcalloc(size_t size);

  void *Malloc(size_t size);
  void Free(void *ptr);

  // for debug
  int Check();
  void Walk();
};
