// created by leipeng 2022-07-21 09:48, all rights reserved
#include "vm_util.hpp"
#include <terark/stdtypes.hpp>
#include <stdio.h>
#if defined(__linux__)
  #include <linux/version.h>
  #include <linux/mman.h>
  #include <sys/utsname.h>
#endif

namespace terark {

int get_linux_kernel_version() {
    utsname u;
    uname(&u);
    int major, minor, patch;
    if (sscanf(u.release, "%d.%d.%d", &major, &minor, &patch) == 3) {
        return KERNEL_VERSION(major, minor, patch);
    }
    return -1;
}
static const int g_linux_kernel_version = get_linux_kernel_version();
static const bool g_has_madv_populate = []{
    if (g_linux_kernel_version >= KERNEL_VERSION(5,14,0)) {
        return true;
    }
    fprintf(stderr,
        "WARN: MADV_POPULATE_READ requires kernel 5.14+, fallback to mlock\n");
    return false;
}();
const size_t g_min_prefault_pages = g_has_madv_populate ? 1 : 2;

#if defined(__linux__) && !defined(MADV_POPULATE_READ)
  #warning "MADV_POPULATE_READ requires kernel version 5.14+, we define MADV_POPULATE_READ = 22 ourselves"
  #define MADV_POPULATE_READ  22
  #define MADV_POPULATE_WRITE 23
#endif

void AutoPrefaultMem::maybe_prefault(const void* p, size_t n, size_t min_pages) {
    size_t lo = pow2_align_down(size_t(p), VM_PAGE_SIZE);
    size_t hi = pow2_align_up(size_t(p) + n, VM_PAGE_SIZE);
    len = hi - lo;
    if (len >= VM_PAGE_SIZE * min_pages) {
        if (g_has_madv_populate)
            madvise((void*)lo, len, MADV_POPULATE_READ);
        else
            mlock(ptr = (void*)lo, len);
    }
}

} // namespace terark
