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

class vcalloc {
private:
  ControlHeader *control_;

public:
  vcalloc(void *mem, size_t size);

  void *Malloc(size_t size);
  void Free(void *ptr);

  // for debug
  int Check();
  void Walk();
};
