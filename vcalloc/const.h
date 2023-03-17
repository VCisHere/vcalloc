#pragma once

#include <cstddef>

/*
** Detect whether or not we are building for a 32- or 64-bit (LP/LLP)
** architecture. There is no reliable portable method at compile-time.
*/
#if defined(__alpha__) || defined(__ia64__) || defined(__x86_64__) ||          \
    defined(_WIN64) || defined(__LP64__) || defined(__LLP64__)
#define VCALLOC_64BIT
#endif

constexpr int kSLIndexCountLog2 = 5;

#if defined(VCALLOC_64BIT)
constexpr int kAlignSizeLog2 = 3;
constexpr int kFLIndexMax = 32;
#else
constexpr int kAlignSizeLog2 = 2;
constexpr int kFLIndexMax = 30;
#endif
constexpr int kAlignSize = (1 << kAlignSizeLog2);
constexpr int kSLIndexCount = (1 << kSLIndexCountLog2);
constexpr int kFLIndexShift = (kSLIndexCountLog2 + kAlignSizeLog2);
constexpr int kFLIndexCount = (kFLIndexMax - kFLIndexShift + 1);
constexpr int kSmallBlockSize = (1 << kFLIndexShift);

static_assert(0 == (kAlignSize & (kAlignSize - 1)),
              "must align to a power of two");

inline static size_t AlignUp(size_t x) {
  return (x + (kAlignSize - 1)) & ~(kAlignSize - 1);
}

inline static size_t AlignDown(size_t x) { return x - (x & (kAlignSize - 1)); }

inline static void *AlignPtr(const void *ptr) {
  return (void *)((std::ptrdiff_t(ptr) + (kAlignSize - 1)) & ~(kAlignSize - 1));
}

#define CheckMem(mem)                                                          \
  (assert(std::ptrdiff_t(mem) % kAlignSize == 0 && "Memory must be aligned"))
