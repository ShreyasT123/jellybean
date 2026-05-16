#pragma once

#if defined(_MSC_VER)
#  define JELLYBEAN_FORCE_INLINE __forceinline
#  define JELLYBEAN_NO_INLINE __declspec(noinline)
#  define JELLYBEAN_LIKELY(x) (x)
#  define JELLYBEAN_UNLIKELY(x) (x)
#else
#  define JELLYBEAN_FORCE_INLINE inline __attribute__((always_inline))
#  define JELLYBEAN_NO_INLINE __attribute__((noinline))
#  define JELLYBEAN_LIKELY(x) __builtin_expect(!!(x), 1)
#  define JELLYBEAN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif
