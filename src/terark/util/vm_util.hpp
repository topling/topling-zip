// created by leipeng 2022-07-21 09:48, all rights reserved
#pragma once
#include <sys/mman.h>

namespace terark {

constexpr size_t VM_PAGE_SIZE = 4096;

extern const size_t g_min_prefault_pages;

struct AutoPrefaultMem {
    void maybe_prefault(const void* p, size_t n, size_t min_pages);
    inline ~AutoPrefaultMem() {
        if (ptr)
            munlock(ptr, len);
    }
    void*  ptr = nullptr;
    size_t len = 0;
};

} // namespace terark
