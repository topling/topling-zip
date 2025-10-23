#include "function.hpp"

namespace terark {

#if TOPLING_USE_BOUND_PMF == 2 // msvc
TERARK_DLL_EXPORT
const void* AbiExtractFuncPtr(const void* obj, const void* mf) {
    auto ip = (const byte_t*)(mf);
    while (ip[0]==0xE9) { // jmp XXXXXXXX(int32)
        ip += unaligned_load<int>(ip + 1) + 5;
    }
    if (ip[0]==0x48 && ip[1]==0x8B && ip[2]==0x01) { // mov rax,qword ptr [rcx]
        intptr_t disp;
        if (ip[3]==0xFF && ip[4]==0x60) { // jmp qword ptr [rax+96]; disp 8 bits
            disp = (byte_t)ip[5]; // it is signed but never be negtive
        } else if (ip[3]==0xFF && ip[4]==0xA0) { // disp 32 bits
            disp = unaligned_load<unsigned>(ip+5); // it is signed but never be negtive
        } else {
            return ip;
        }
        auto vtab = aligned_load<const byte_t*>(obj);
        ip = aligned_load<const byte_t*>(vtab + disp);
        while (ip[0] == 0xE9) { // jmp XXXXXXXX(int32)
            ip += unaligned_load<int>(ip + 1) + 5;
        }
        return ip;
    }
    return ip;
}
#endif // TOPLING_USE_BOUND_PMF

UserMemPool::UserMemPool() {}
UserMemPool::~UserMemPool() {}

void* UserMemPool::alloc(size_t len) {
    return ::malloc(len);
}

void* UserMemPool::realloc(void* ptr, size_t len) {
    return ::realloc(ptr, len);
}

void  UserMemPool::sfree(void* ptr, size_t) {
    return ::free(ptr);
}

UserMemPool* UserMemPool::SysMemPool() {
    static UserMemPool sysMem;
    return &sysMem;
}

#ifdef FORCE_CACHE_LINE_SIZE
    #define CACHE_LINE_SIZE FORCE_CACHE_LINE_SIZE
#else
    #if defined(__s390__)
        #if defined(__GNUC__) && __GNUC__ < 7
            #define CACHE_LINE_SIZE 64U
        #else
            #define CACHE_LINE_SIZE 256U
        #endif
    #elif defined(__powerpc__) || defined(__aarch64__)
        #define CACHE_LINE_SIZE 128U
    #else
        #define CACHE_LINE_SIZE 64U
    #endif
#endif

static_assert((CACHE_LINE_SIZE & (CACHE_LINE_SIZE - 1)) == 0,
              "Cache line size must be a power of 2 number of bytes");

void* CacheAlignedNewDelete::operator new(size_t size) {
#if defined(_MSC_VER)
  return _aligned_malloc(size, CACHE_LINE_SIZE);
#elif TOPLING_USE_JEMALLOC
  // a bit faster than posix_memalign
  return mallocx(size, MALLOCX_ALIGN(CACHE_LINE_SIZE));
#else
  void* p = nullptr;
  if (posix_memalign(&p, CACHE_LINE_SIZE, size)) {
    TERARK_DIE("posix_memalign(%d, %zd) = %m", CACHE_LINE_SIZE, size);
  }
  return p;
#endif
}

void CacheAlignedNewDelete::operator delete(void* p, size_t) {
#if defined(_MSC_VER)
  _aligned_free(p);
#else
  free(p);
#endif
}

} // namespace terark
