#pragma once

constexpr int kSLIndexCountLog2 = 5;

#if defined (VCALLOC_64BIT)
constexpr int	kAlignSizeLog2 = 3;
constexpr int kFLIndexMax = 32;
#else
constexpr int	kAlignSizeLog2 = 2;
constexpr int kFLIndexMax = 30;
#endif
constexpr int kAlignSize = (1 << kAlignSizeLog2);
constexpr int kSLIndexCount = (1 << kSLIndexCountLog2);
constexpr int kFLIndexShift = (kSLIndexCountLog2 + kAlignSizeLog2);
constexpr int kFLIndexCount = (kFLIndexMax - kFLIndexShift + 1);
constexpr int kSmallBlockSize = (1 << kFLIndexShift);

static_assert(0 == (kAlignSize & (kAlignSize - 1)) && "must align to a power of two");

inline static size_t AlignUp(size_t x) { return (x + (kAlignSize - 1)) & ~(kAlignSize - 1); }

inline static size_t AlignDown(size_t x) { return x - (x & (kAlignSize - 1)); }

inline static void* AlignPtr(const void* ptr) {
	return (void*)((std::ptrdiff_t(ptr) + (kAlignSize - 1)) & ~(kAlignSize - 1));
}
