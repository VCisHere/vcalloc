#pragma once

#include <new>

#include "vcalloc/common.h"
#include "vcalloc/const.h"
#include "vcalloc/control.h"

struct ControlHeader;
struct BlockHeader;

class vcalloc {
private:
  ControlHeader *control_;

public:
  vcalloc();

  void *Malloc(size_t size);
  void Free(void *ptr);

  float GetUsageRate();
  size_t ToOffset(void *ptr);
  void *FromOffset(size_t offset);
};

class Global {
public:
  static vcalloc &GetAllocator();
};
