#pragma once

#ifdef _M_X64
#include <immintrin.h>
#endif
#include <terark/succinct/rank_select_basic.hpp>

namespace terark {

inline size_t
binary_search_byte(const byte_t* data, size_t len, byte_t key) {
	size_t lo = 0;
	size_t hi = len;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (data[mid] < key)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < len && data[lo] == key)
		return lo;
	else
		return len;
}

#if defined(__SSE4_2__) // && (defined(NDEBUG) || !defined(__GNUC__))
	inline int // _mm_cmpestri length param is int32
	sse4_2_search_byte(const byte_t* data, int len, byte_t key) {
		// intrinsic: _mm_cmpestri can not generate "pcmpestri xmm, mem, imm"
		__m128i key128 = { char(key) }; // sizeof(__m128i)==16
		//-----------------^^^^----- to avoid vc warning C4838: conversion from 'byte_t' to 'char'
#if defined(__GNUC__)
		// gcc 7.1 typedef'ed such a type named __m128i_u
		// my_m128i_u has been proved work on gcc 4.8+
		typedef long long my_m128i_u __attribute__((__vector_size__(16), __may_alias__, __aligned__(1)));
#else
		typedef __m128i my_m128i_u;
#endif
	#if 0
		int pos;
		for(pos = 0; pos < len-16; pos += 16) {
		//	memcpy(&str128, data + pos, 16); // load
			int idx = _mm_cmpestri(key128, 1,
				*(const my_m128i_u*)(data+pos), 16, // don't require memory align
			//	str128, 16,
				_SIDD_UBYTE_OPS|_SIDD_CMP_EQUAL_ORDERED|_SIDD_LEAST_SIGNIFICANT);
			if (idx < 16)
				return pos + idx;
		}
	#else
		assert(len <= 16);
		int const pos = 0;
	#endif
	//	memcpy(&str128, data + pos, 16); // load
		int idx = _mm_cmpestri(key128, 1,
			*(const my_m128i_u*)(data+pos), len-pos, // don't require memory align
		//	str128, len-pos,
			_SIDD_UBYTE_OPS|_SIDD_CMP_EQUAL_ORDERED|_SIDD_LEAST_SIGNIFICANT);
	#if 0
		if (idx < len-pos)
	//	if (idx < 16) // _mm_cmpestri returns 16 when not found
			return pos + idx;
		else
			return len;
	#else
		// if search failed, return value will >= len
		// so the above check for idx is not needed
		// the caller will check if the return value is >= len or < len
		return pos + idx;
	#endif
	}
	inline size_t
	fast_search_byte(const byte_t* data, size_t len, byte_t key) {
		if (len <= 16)
			return sse4_2_search_byte(data, int(len), key);
		else
			return binary_search_byte(data, len, key);
	}
	inline size_t
	fast_search_byte_max_35(const byte_t* data, size_t len, byte_t key) {
		assert(len <= 35);
		if (len <= 16) {
			return sse4_2_search_byte(data, int(len), key);
		}
		size_t pos = sse4_2_search_byte(data, 16, key);
		if (pos < 16) {
			return pos;
		}
		if (len <= 32) {
			return 16 + sse4_2_search_byte(data + 16, int(len - 16), key);
		}
		pos = sse4_2_search_byte(data + 16, 16, key);
		if (pos < 16) {
			return 16 + pos;
		}
		return 32 + sse4_2_search_byte(data + 32, int(len - 32), key);
	}
	#define fast_search_byte_max_16 sse4_2_search_byte
#else
	#define fast_search_byte binary_search_byte
	#define fast_search_byte_max_16 binary_search_byte
	#define fast_search_byte_max_35 binary_search_byte
#endif

#if defined(__AVX512VL__) && defined(__AVX512BW__)
  #if defined(__AVX512_PREFER_256_VECTORS__)
	inline size_t
	avx512_search_byte_max64_256(const byte_t* data, size_t len, byte_t key) {
		TERARK_ASSERT_LE(len, 64);
		uint64_t  u1 = _bzhi_u64(-1, len);
		__mmask32 k0 = __mmask32(u1);
		__mmask32 k1 = __mmask32(u1 >> 32);
		__m256i   d0 = _mm256_maskz_loadu_epi8(k0, data);
		__m256i   d1 = _mm256_maskz_loadu_epi8(k1, data + 32);
		__mmask32 m0 = _mm256_mask_cmpeq_epi8_mask(k0, d0, _mm256_set1_epi8(key));
		__mmask32 m1 = _mm256_mask_cmpeq_epi8_mask(k1, d1, _mm256_set1_epi8(key));
		return _tzcnt_u64((uint64_t(m1) << 32) | uint64_t(m0));
	}
	#define avx512_search_byte_max64 avx512_search_byte_max64_256
  #else
	inline size_t
	avx512_search_byte_max64_512(const byte_t* data, size_t len, byte_t key) {
		TERARK_ASSERT_LE(len, 64);
		__mmask64 k = _bzhi_u64(-1, len);
		__m512i   d = _mm512_maskz_loadu_epi8(k, data);
		return _tzcnt_u64(_mm512_mask_cmpeq_epi8_mask(k, d, _mm512_set1_epi8(key)));
	}
	#define avx512_search_byte_max64 avx512_search_byte_max64_512
  #endif
	inline size_t
	avx512_search_byte_max16(const byte_t* data, size_t len, byte_t key) {
		TERARK_ASSERT_LE(len, 16);
		// _bzhi_u16 maybe slower than _bzhi_u32
		// _bzhi_u32 will return 32 when not found, it is ok for the caller
		// checks if return value < len or >= len
		__mmask16 k = __mmask16(_bzhi_u32(-1, len));
		__m128i   d = _mm_maskz_loadu_epi8(k, data); // load minimal bytes
		return _tzcnt_u32(_mm_mask_cmpeq_epi8_mask(k, d, _mm_set1_epi8(key)));
	 // return _tzcnt_u32(_mm_mask_cmpeq_epi8_mask(_bzhi_u32(-1, len), *(__m128i_u*)data, _mm_set1_epi8(key)));
	 // `*(__m128i_u*)data` may causing compiler issue a full vector load
	}
	inline size_t
	avx512_search_byte_max32(const byte_t* data, size_t len, byte_t key) {
		TERARK_ASSERT_LE(len, 32);
		__mmask32 k = _bzhi_u32(-1, len);
		__m256i   d = _mm256_maskz_loadu_epi8(k, data); // load minimal bytes
		return _tzcnt_u32(_mm256_mask_cmpeq_epi8_mask(k, d, _mm256_set1_epi8(key)));
	 // return _tzcnt_u32(_mm256_mask_cmpeq_epi8_mask(_bzhi_u32(-1, len), *(__m256i_u*)data, _mm256_set1_epi8(key)));
	 // `*(__m256i_u*)data` may causing compiler issue a full vector load
	}
	inline size_t
	avx512_fast_search_byte(const byte_t* data, size_t len, byte_t key) {
		size_t lo = 0, hi = len;
		while (hi - lo > 64) {
			size_t mid = (lo + hi) / 2;
			if (data[mid] < key)
				lo = mid + 1;
			else
				hi = mid;
		}
		size_t sublen = hi - lo;
		size_t pos = avx512_search_byte_max64(data + lo, sublen, key);
		if (pos < sublen) // not needed to check data[lo+pos] == key
			return lo + pos;
		else
			return len;
	}
	#define fast_search_byte avx512_fast_search_byte

	// trick: override sse4_2_search_byte with avx512 version,
	// because avx512 is a superset of sse4.2,
	// avx512 version is always faster than sse4.2 version,
	// fast_search_byte_max_16 is also overridden by sse4_2_search_byte
	#define sse4_2_search_byte avx512_search_byte_max16

	#define fast_search_byte_max_35 avx512_search_byte_max64

	#if !defined(__SSE4_2__)
		#error "AVX512VL and AVX512BW is enabled but SSE4.2 is disabled"
	#endif
#endif

/*
	inline size_t
	fast_search_byte_rs_seq(const byte_t* data, size_t len, byte_t key) {
		assert(len >= 32);
		size_t pc = 0;
		for(size_t i = 0; i < 256/TERARK_WORD_BITS; ++i) {
			size_t w = unaligned_load<size_t>(data + i*sizeof(size_t));
			if (i*TERARK_WORD_BITS < key) {
				pc += fast_popcount(w);
			} else {
				pc += fast_popcount_trail(w, key % TERARK_WORD_BITS);
				break;
			}
		}
		return pc;
	}
*/

	inline size_t
	popcount_rs_256(const byte_t* data) {
		size_t w = unaligned_load<uint64_t>(data + 4, 3);
		return data[3] + fast_popcount(w);
	}

	inline size_t
	fast_search_byte_rs_idx(const byte_t* data, byte_t key) {
		size_t i = key / TERARK_WORD_BITS;
		size_t w = unaligned_load<size_t>(data + 4 + i*sizeof(size_t));
		size_t b = data[i];
		return b + fast_popcount_trail(w, key % TERARK_WORD_BITS);
	}

	inline size_t
	fast_search_byte_rs_idx(const byte_t* data, size_t len, byte_t key) {
		if (terark_unlikely(!terark_bit_test((size_t*)(data + 4), key)))
			return len;
		assert(len >= 36);
		size_t i = key / TERARK_WORD_BITS;
		size_t w = unaligned_load<size_t>(data + 4 + i*sizeof(size_t));
		size_t b = data[i];
		TOPLING_ASSUME_RETURN(b + fast_popcount_trail(w, key % TERARK_WORD_BITS), < len);
	}

	inline size_t
	fast_search_byte_may_rs(const byte_t* data, size_t len, byte_t key) {
		if (len < 36)
			return fast_search_byte_max_35(data, len, key);
		else
			return fast_search_byte_rs_idx(data, len, key);
	}

    inline size_t rs_next_one_pos(const void* rs, size_t ch) {
        size_t i = ch/64;
        uint64_t w = unaligned_load<uint64_t>(rs, i);
        w >>= (ch & 63);
        w >>= 1;
        if (w) {
            return ch + 1 + fast_ctz64(w);
        }
        else {
            assert(i < 3);
            do w = unaligned_load<uint64_t>(rs, ++i); while (0==w);
            assert(i < 4);
            return i * 64 + fast_ctz64(w);
        }
    }

    inline size_t rs_prev_one_pos(const void* rs, size_t ch) {
        size_t i = ch/64;
        uint64_t w = unaligned_load<uint64_t>(rs, i);
        w <<= 63 - (ch & 63);
        w <<= 1;
        if (w) {
            return ch - 1 - fast_clz64(w);
        }
        else {
            assert(i > 0);
            do w = unaligned_load<uint64_t>(rs, --i); while (0==w);
            assert(i < 3); // i would not be wrapped back to size_t(-1, -2, ...)
            return i * 64 + terark_bsr_u64(w);
        }
    }

    inline size_t rs_select1(const byte_t* rs, size_t rank1) {
        assert(rs[0] == 0); // rs[0] is always 0
        if (rank1 < rs[2]) {
            if (rank1 < rs[1]) {
                uint64_t w = unaligned_load<uint64_t>(rs + 4);
                return 0*64 + UintSelect1(w, rank1);
            }
            else {
                uint64_t w = unaligned_load<uint64_t>(rs + 4 + 8);
                return 1*64 + UintSelect1(w, rank1 - rs[1]);
            }
        }
        else {
            if (rank1 < rs[3]) {
                uint64_t w = unaligned_load<uint64_t>(rs + 4 + 16);
                return 2*64 + UintSelect1(w, rank1 - rs[2]);
            }
            else {
                uint64_t w = unaligned_load<uint64_t>(rs + 4 + 24);
                return 3*64 + UintSelect1(w, rank1 - rs[3]);
            }
        }
    }


} // namespace terark
