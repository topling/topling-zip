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

TERARK_DLL_EXPORT int get_linux_kernel_version() {
    utsname u;
    uname(&u);
    int major, minor, patch;
    if (sscanf(u.release, "%d.%d.%d", &major, &minor, &patch) == 3) {
        return KERNEL_VERSION(major, minor, patch);
    }
    return -1;
}

TERARK_DLL_EXPORT
const int g_linux_kernel_version = get_linux_kernel_version();

TERARK_DLL_EXPORT
const bool g_has_madv_populate = []{
    if (g_linux_kernel_version >= KERNEL_VERSION(5,14,0)) {
        return true;
    }
    return false;
}();
const size_t g_min_prefault_pages = g_has_madv_populate ? 1 : 2;

#if defined(__linux__) && !defined(MADV_POPULATE_READ)
  #warning "MADV_POPULATE_READ requires kernel version 5.14+, we define MADV_POPULATE_READ = 22 ourselves"
  #define MADV_POPULATE_READ  22
  #define MADV_POPULATE_WRITE 23
#endif

TERARK_DLL_EXPORT
void vm_prefetch(const void* addr, size_t len, size_t min_pages) {
    size_t lo = pow2_align_down(size_t(addr), VM_PAGE_SIZE);
    size_t hi = pow2_align_up(size_t(addr) + len, VM_PAGE_SIZE);
    size_t aligned_len = hi - lo;
    if (aligned_len < VM_PAGE_SIZE * min_pages) {
      return;
    }
  #if BOOST_OS_WINDOWS
		WIN32_MEMORY_RANGE_ENTRY vm;
		vm.VirtualAddress = (void*)lo;
		vm.NumberOfBytes  = aligned_len;
		PrefetchVirtualMemory(GetCurrentProcess(), 1, &vm, 0);
  #elif !defined(__CYGWIN__)
    // check g_has_madv_populate first, only MADV_POPULATE_READ
    if (g_has_madv_populate) {
        madvise((void*)lo, aligned_len, MADV_POPULATE_READ);
    }
  #endif
}

} // namespace terark
