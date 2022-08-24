// created by leipeng 2022-07-21 09:48, all rights reserved
#pragma once
#include <sys/mman.h>
#include <terark/config.hpp>

namespace terark {

constexpr size_t VM_PAGE_SIZE = 4096;

TERARK_DLL_EXPORT extern const int g_linux_kernel_version;
TERARK_DLL_EXPORT extern const bool g_has_madv_populate;
TERARK_DLL_EXPORT extern const size_t g_min_prefault_pages;

struct TERARK_DLL_EXPORT AutoPrefaultMem {
    void maybe_prefault(const void* p, size_t n, size_t min_pages);
    inline ~AutoPrefaultMem() {
        if (ptr)
            munlock(ptr, len);
    }
    void*  ptr = nullptr;
    size_t len = 0;
};

} // namespace terark
