#pragma once

#if defined(__x86_64__) || defined(_M_X64)
#  define JELLYBEAN_ARCH_X64
#elif defined(__aarch64__) || defined(_M_ARM64)
#  define JELLYBEAN_ARCH_ARM64
#endif
