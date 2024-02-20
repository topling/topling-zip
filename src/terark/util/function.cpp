#include "function.hpp"

namespace terark {

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
