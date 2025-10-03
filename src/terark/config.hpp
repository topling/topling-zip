#pragma once

#if defined(_MSC_VER)

#ifndef _CRT_SECURE_NO_WARNINGS
# define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#endif

#ifndef _SCL_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS
#endif

#  if defined(TERARK_CREATE_DLL)
#    pragma warning(disable: 4251)
#    define TERARK_DLL_EXPORT __declspec(dllexport)      // creator of dll
#    if defined(_DEBUG) || !defined(NDEBUG)
#//	   pragma message("creating terark-d.lib")
#    else
#//	   pragma message("creating terark-r.lib")
#    endif
#  elif defined(TERARK_USE_DLL)
#    pragma warning(disable: 4251)
#    define TERARK_DLL_EXPORT __declspec(dllimport)      // user of dll
#    if defined(_DEBUG) || !defined(NDEBUG)
//#	   pragma comment(lib, "terark-d.lib")
#    else
//#	   pragma comment(lib, "terark-r.lib")
#    endif
#  else
#    define TERARK_DLL_EXPORT                            // static lib creator or user
#  endif

#else /* _MSC_VER */

#  define TERARK_DLL_EXPORT

#endif /* _MSC_VER */

#if defined(__GNUC__) && __GNUC__*1000 + __GNUC_MINOR__ >= 7001
#  pragma GCC diagnostic ignored "-Walloc-size-larger-than="
#  define no_break_fallthrough __attribute__ ((fallthrough))
#endif

#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)

#  define terark_likely(x)    __builtin_expect(x, 1)
#  define terark_unlikely(x)  __builtin_expect(x, 0)
#  define terark_no_return    __attribute__((noreturn))
#  define terark_warn_unused_result  __attribute__((warn_unused_result))
#  define terark_flatten __attribute__((flatten))
#  define terark_used_static_obj __attribute__((used))
#  define terark_no_inline     __attribute__((noinline))
#  define terark_forceinline     inline __attribute__((always_inline))
#  define terark_pure_func     __attribute__((pure))
#  define terark_nonnull  __attribute__((nonnull))
#  define terark_returns_nonnull  __attribute__((returns_nonnull))

#else

#if defined(_MSC_VER) && _MSC_VER >= 1310
#  define terark_no_return __declspec(noreturn)
#  define terark_no_inline __declspec(noinline)
#  define terark_forceinline __forceinline
#endif

#if defined(_MSC_VER) && _MSC_VER >= 1400
#  define terark_no_alias __declspec(noalias)
#endif

#if defined(_MSC_VER) && _MSC_VER >= 1910 // vs2017
# if _MSVC_LANG >= 201703L
#  define no_break_fallthrough [[fallthrough]]
# endif
#endif

#  define terark_likely(x)    x
#  define terark_unlikely(x)  x
#  define terark_warn_unused_result
#  define terark_flatten
#  define terark_pure_func

#endif

#if !defined(terark_no_return)
#  define terark_no_return
#endif

#if !defined(terark_no_inline)
#  define terark_no_inline
#endif

#if !defined(terark_forceinline)
#  define terark_forceinline inline
#endif

#if !defined(terark_no_alias)
#  define terark_no_alias
#endif

#if !defined(terark_used_static_obj)
#  define terark_used_static_obj volatile
#endif

#if !defined(terark_nonnull)
#  define terark_nonnull
#endif

#if !defined(terark_returns_nonnull)
#  define terark_returns_nonnull
#endif

/* The ISO C99 standard specifies that in C++ implementations these
 *    should only be defined if explicitly requested __STDC_CONSTANT_MACROS
 */
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#if defined(__GNUC__) && ( \
	  defined(__LP64__) && (__LP64__ == 1) || \
	  defined(__amd64__) || defined(__amd64) || \
	  defined(__x86_64__) || defined(__x86_64) || \
	  defined(__ia64__) || defined(_IA64) || defined(__IA64__) ) || \
	defined(_MSC_VER) && ( defined(_WIN64) || defined(_M_X64) || defined(_M_IA64) ) || \
	defined(__INTEL_COMPILER) && ( \
	  defined(__ia64) || defined(__itanium__) || \
	  defined(__x86_64) || defined(__x86_64__) ) || \
    defined(__WORD_SIZE) && __WORD_SIZE == 64
  #define TERARK_WORD_BITS 64
  #define TERARK_IF_WORD_BITS_64(Then, Else) Then
#else
  #define TERARK_WORD_BITS 32
  #define TERARK_IF_WORD_BITS_64(Then, Else) Else
#endif

#if defined(_MSC_VER)
  #define TERARK_IF_MSVC(Then, Else) Then
#else
  #define TERARK_IF_MSVC(Then, Else) Else
#endif

#if !defined(no_break_fallthrough)
  #define no_break_fallthrough  /* fall through */
#endif

#ifdef _MSC_VER
#define TERARK_ASSUME(cond) __assume(cond)
#elif defined(__clang__)
#define TERARK_ASSUME(cond) __builtin_assume(cond)
#elif defined(__GNUC__)
  #if __GNUC__ >= 13
    #define TERARK_ASSUME(cond) [[assume(cond)]]
  #else
    #define TERARK_ASSUME(cond) ((cond) ? static_cast<void>(0) : __builtin_unreachable())
  #endif
#else
#define TERARK_ASSUME(cond) static_cast<void>(0)
#endif

// usage sample:
//   TOPLING_ASSUME_RETURN(ret, < INT_MAX)
//   TOPLING_ASSUME_RETURN(ret, != -1)
#define TOPLING_ASSUME_RETURN(ret, cmp_ImposibleValue) do { \
            auto __r_y_v_ = ret; \
            TERARK_ASSUME(__r_y_v_ cmp_ImposibleValue); \
            return __r_y_v_; \
        } while (0)

#if defined(__gnu_linux__) || defined(__gnu_hurd__) || defined(__FreeBSD__)
    #define TERARK_HAS_WEAK_SYMBOL 1
    #define TERARK_WEAK_SYMBOL __attribute__((weak))
#else
    #define TERARK_WEAK_SYMBOL
#endif

// default is static tls, fast but need LD_PRELOAD if not directly linked
#if defined(__GNUC__) && !defined(TOPLING_USE_DYNAMIC_TLS)
#define TERARK_STATIC_TLS __attribute__((tls_model("initial-exec")))
#else
#define TERARK_STATIC_TLS
#endif

#define TERARK_UNUSED_VAR(x) (void)(x)

#define TERARK_NAME_TYPE(name, ...) \
    struct name : __VA_ARGS__ { \
        typedef __VA_ARGS__ super_class; \
        using super_class::super_class; \
    }

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || defined(__amd64)
#else
namespace terark {
    static constexpr int _MM_HINT_T0 = 0;
    inline void _mm_prefetch(const void* p, int) {
        __builtin_prefetch(p);
    }
}
#endif

extern bool g_Terark_hasValgrind;

#ifdef __GNUC__
#define TOPLING_GCC_PRAGMA(X) _Pragma(#X)
#define TOPLING_GCC_NOLINT(warn) TOPLING_GCC_PRAGMA(GCC diagnostic ignored #warn)
#else
#define TOPLING_GCC_PRAGMA(X)
#define TOPLING_GCC_NOLINT(warn)
#endif

#if defined(__GNUC__) && defined(__clang__) && __clang_major__ >= 20
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif
