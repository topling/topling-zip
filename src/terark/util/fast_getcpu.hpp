#include <terark/config.hpp>

#if defined(__linux__) && (defined(__amd64__) || defined(__amd64) || \
      defined(__x86_64__) || defined(__x86_64) || \
      defined(__ia64__) || defined(_IA64) || defined(__IA64__) ) || \
    defined(__INTEL_COMPILER) && ( \
      defined(__ia64) || defined(__itanium__) || \
      defined(__x86_64) || defined(__x86_64__) )

namespace terark {
terark_forceinline unsigned int fast_getcpu(void) {
    /* Abused to load per CPU data from limit */
    const unsigned GDT_ENTRY_PER_CPU = 15;
    const unsigned __PER_CPU_SEG = (GDT_ENTRY_PER_CPU * 8 + 3);
    static const unsigned VGETCPU_CPU_MASK = 0xfff;
    unsigned int p;
    /*
     * Load per CPU data from GDT.  LSL is faster than RDTSCP and
     * works on all CPUs.  This is volatile so that it orders
     * correctly wrt barrier() and to keep gcc from cleverly
     * hoisting it out of the calling function.
     */
    asm volatile ("lsl %1,%0" : "=r" (p) : "r" (__PER_CPU_SEG));
    // unsigned node = p >> 12;
    return p & VGETCPU_CPU_MASK;
}
} // namespace terark

#elif !defined(_MSC_VER)

#include <sched.h>
namespace terark {
terark_forceinline unsigned int fast_getcpu(void) {
    return sched_getcpu();
}
} // namespace terark

#endif
