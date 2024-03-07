#include "hash_common.hpp"
#include "valvec.hpp"

namespace terark {

TERARK_DLL_EXPORT
terark_flatten
size_t __hsm_stl_next_prime(size_t __n) {
	static const size_t primes[] =
	{
		5,11,19,37,   53ul,         97ul,         193ul,       389ul,
		769ul,        1543ul,       3079ul,       6151ul,      12289ul,
		24593ul,      49157ul,      98317ul,      196613ul,    393241ul,
		786433ul,     1572869ul,    3145739ul,    6291469ul,   12582917ul,
		25165843ul,   50331653ul,   100663319ul,  201326611ul, 402653189ul,
		805306457ul,  1610612741ul, 3221225473ul, 4294967291ul,
#ifndef TERARK_WORD_BITS
	#error "TERARK_WORD_BITS is not defined"
#endif
#if	TERARK_WORD_BITS == 64
      /* 30    */ (size_t)8589934583ull,
      /* 31    */ (size_t)17179869143ull,
      /* 32    */ (size_t)34359738337ull,
      /* 33    */ (size_t)68719476731ull,
      /* 34    */ (size_t)137438953447ull,
      /* 35    */ (size_t)274877906899ull,
      /* 36    */ (size_t)549755813881ull,
      /* 37    */ (size_t)1099511627689ull,
      /* 38    */ (size_t)2199023255531ull,
      /* 39    */ (size_t)4398046511093ull,
      /* 40    */ (size_t)8796093022151ull,
      /* 41    */ (size_t)17592186044399ull,
      /* 42    */ (size_t)35184372088777ull,
      /* 43    */ (size_t)70368744177643ull,
      /* 44    */ (size_t)140737488355213ull,
      /* 45    */ (size_t)281474976710597ull,
      /* 46    */ (size_t)562949953421231ull,
      /* 47    */ (size_t)1125899906842597ull,
      /* 48    */ (size_t)2251799813685119ull,
      /* 49    */ (size_t)4503599627370449ull,
      /* 50    */ (size_t)9007199254740881ull,
      /* 51    */ (size_t)18014398509481951ull,
      /* 52    */ (size_t)36028797018963913ull,
      /* 53    */ (size_t)72057594037927931ull,
      /* 54    */ (size_t)144115188075855859ull,
      /* 55    */ (size_t)288230376151711717ull,
      /* 56    */ (size_t)576460752303423433ull,
      /* 57    */ (size_t)1152921504606846883ull,
      /* 58    */ (size_t)2305843009213693951ull,
      /* 59    */ (size_t)4611686018427387847ull,
      /* 60    */ (size_t)9223372036854775783ull,
      /* 61    */ (size_t)18446744073709551557ull,
#endif // TERARK_WORD_BITS == 64
	};
      const size_t extent = sizeof(primes) / sizeof(primes[0]);
      const size_t pos = lower_bound_0(primes, extent, __n);
      return primes[std::min(pos, extent-1)];
}

template<class Uint, int Bits> const Uint dummy_bucket<Uint, Bits>::tail;
template<class Uint, int Bits> const Uint dummy_bucket<Uint, Bits>::delmark;
template<class Uint, int Bits> const Uint dummy_bucket<Uint, Bits>::maxlink;

template struct dummy_bucket<unsigned char     , 8 * sizeof(unsigned char)>;
template struct dummy_bucket<unsigned short    , 8 * sizeof(unsigned short)>;
template struct dummy_bucket<unsigned int      , 8 * sizeof(unsigned int)>;
template struct dummy_bucket<unsigned long     , 8 * sizeof(unsigned long)>;
template struct dummy_bucket<unsigned long long, 8 * sizeof(unsigned long long)>;

} // namespace terark
