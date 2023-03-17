#pragma once

#include "vcalloc/const.h"

#include <cstddef>

#if defined(__cplusplus)
#define VCALLOC_DECL inline
#else
#define VCALLOC_DECL static
#endif

/*
** gcc 3.4 and above have builtin support, specialized for architecture.
** Some compilers masquerade as gcc; patchlevel test filters them out.
*/
#if defined(__GNUC__) &&                                                       \
    (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)) &&                \
    defined(__GNUC_PATCHLEVEL__)

#if defined(__SNC__)
/* SNC for Playstation 3. */

VCALLOC_DECL int vcalloc_ffs(unsigned int word) {
  const unsigned int reverse = word & (~word + 1);
  const int bit = 32 - __builtin_clz(reverse);
  return bit - 1;
}

#else

VCALLOC_DECL int vcalloc_ffs(unsigned int word) {
  return __builtin_ffs(word) - 1;
}

#endif

VCALLOC_DECL int vcalloc_fls(unsigned int word) {
  const int bit = word ? 32 - __builtin_clz(word) : 0;
  return bit - 1;
}

#elif defined(_MSC_VER) && (_MSC_VER >= 1400) &&                               \
    (defined(_M_IX86) || defined(_M_X64))
/* Microsoft Visual C++ support on x86/X64 architectures. */

#include <intrin.h>

#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward)

VCALLOC_DECL int vcalloc_fls(unsigned int word) {
  unsigned long index;
  return _BitScanReverse(&index, word) ? index : -1;
}

VCALLOC_DECL int vcalloc_ffs(unsigned int word) {
  unsigned long index;
  return _BitScanForward(&index, word) ? index : -1;
}

#elif defined(_MSC_VER) && defined(_M_PPC)
/* Microsoft Visual C++ support on PowerPC architectures. */

#include <ppcintrinsics.h>

VCALLOC_DECL int vcalloc_fls(unsigned int word) {
  const int bit = 32 - _CountLeadingZeros(word);
  return bit - 1;
}

VCALLOC_DECL int vcalloc_ffs(unsigned int word) {
  const unsigned int reverse = word & (~word + 1);
  const int bit = 32 - _CountLeadingZeros(reverse);
  return bit - 1;
}

#elif defined(__ARMCC_VERSION)
/* RealView Compilation Tools for ARM */

VCALLOC_DECL int vcalloc_ffs(unsigned int word) {
  const unsigned int reverse = word & (~word + 1);
  const int bit = 32 - __clz(reverse);
  return bit - 1;
}

VCALLOC_DECL int vcalloc_fls(unsigned int word) {
  const int bit = word ? 32 - __clz(word) : 0;
  return bit - 1;
}

#elif defined(__ghs__)
/* Green Hills support for PowerPC */

#include <ppc_ghs.h>

VCALLOC_DECL int vcalloc_ffs(unsigned int word) {
  const unsigned int reverse = word & (~word + 1);
  const int bit = 32 - __CLZ32(reverse);
  return bit - 1;
}

VCALLOC_DECL int vcalloc_fls(unsigned int word) {
  const int bit = word ? 32 - __CLZ32(word) : 0;
  return bit - 1;
}

#else
// Fall back to generic implementation

VCALLOC_DECL int vcalloc_fls_generic(unsigned int word) {
  int bit = 32;

  if (!word)
    bit -= 1;
  if (!(word & 0xffff0000)) {
    word <<= 16;
    bit -= 16;
  }
  if (!(word & 0xff000000)) {
    word <<= 8;
    bit -= 8;
  }
  if (!(word & 0xf0000000)) {
    word <<= 4;
    bit -= 4;
  }
  if (!(word & 0xc0000000)) {
    word <<= 2;
    bit -= 2;
  }
  if (!(word & 0x80000000)) {
    word <<= 1;
    bit -= 1;
  }

  return bit;
}

// Implement ffs in terms of fls
VCALLOC_DECL int vcalloc_ffs(unsigned int word) {
  return vcalloc_fls_generic(word & (~word + 1)) - 1;
}

VCALLOC_DECL int vcalloc_fls(unsigned int word) {
  return vcalloc_fls_generic(word) - 1;
}

#endif

// Possibly 64-bit version of vcalloc_fls
#if defined(VCALLOC_64BIT)
VCALLOC_DECL int vcalloc_fls_sizet(size_t size) {
  int high = (int)(size >> 32);
  int bits = 0;
  if (high) {
    bits = 32 + vcalloc_fls(high);
  } else {
    bits = vcalloc_fls((int)size & 0xffffffff);
  }
  return bits;
}
#else
#define vcalloc_fls_sizet vcalloc_fls
#endif

#undef VCALLOC_DECL

#ifndef VCCALLOC_likely
#define VCCALLOC_likely(x) (__builtin_expect(!!(x), 1))
#endif
#ifndef VCCALLOC_unlikely
#define VCCALLOC_unlikely(x) (__builtin_expect(!!(x), 0))
#endif

#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Max(a, b) ((a) > (b) ? (a) : (b))
