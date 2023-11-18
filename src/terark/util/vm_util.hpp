// created by leipeng 2022-07-21 09:48, all rights reserved
#pragma once
#if defined(_MSC_VER)
  // nothing
#else
  #include <sys/mman.h>
#endif
#include <terark/config.hpp>

namespace terark {

constexpr size_t VM_PAGE_SIZE = 4096;

TERARK_DLL_EXPORT extern const int g_linux_kernel_version;
TERARK_DLL_EXPORT extern const bool g_has_madv_populate;
TERARK_DLL_EXPORT extern const size_t g_min_prefault_pages;

TERARK_DLL_EXPORT void vm_prefetch(const void* addr, size_t len, size_t min_pages);

} // namespace terark
