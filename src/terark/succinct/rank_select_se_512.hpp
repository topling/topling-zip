#pragma once

#include "rank_select_basic.hpp"

namespace terark {

// rank1   use 2-level cache, time is O(1), 2 memory access
// select0 use 1-level cache, time is O(1+loglog(n))
// select1 use binary search, slower than select0
// rank_select_se, "_se" means "separated"
// rank index is separated from bits
template<class rank_cache_base_t>
class TERARK_DLL_EXPORT rank_select_se_512_tpl
    : public RankSelectConstants<512>, public febitvec {
public:
    typedef boost::mpl::false_ is_mixed;
    typedef rank_cache_base_t index_t;
    rank_select_se_512_tpl();
    rank_select_se_512_tpl(size_t n, bool val = false);
    rank_select_se_512_tpl(size_t n, valvec_no_init);
    rank_select_se_512_tpl(size_t n, valvec_reserve);
    rank_select_se_512_tpl(const rank_select_se_512_tpl&);
    rank_select_se_512_tpl& operator=(const rank_select_se_512_tpl&);
    rank_select_se_512_tpl(rank_select_se_512_tpl&& y) noexcept;
    rank_select_se_512_tpl& operator=(rank_select_se_512_tpl&& y) noexcept;
    ~rank_select_se_512_tpl();
    void clear() noexcept;
    void risk_release_ownership() noexcept;
    void risk_mmap_from(unsigned char* base, size_t length);
    void shrink_to_fit() noexcept;

    void swap(rank_select_se_512_tpl&) noexcept;
    void build_cache(bool speed_select0, bool speed_select1);
    size_t mem_size() const { return m_capacity / 8; }
    inline size_t rank1(size_t bitpos) const noexcept;
    inline size_t rank0(size_t bitpos) const noexcept;
    size_t select0(size_t id) const noexcept terark_pure_func;
    size_t select1(size_t id) const noexcept terark_pure_func;
    size_t max_rank1() const { return m_max_rank1; }
    size_t max_rank0() const { return m_max_rank0; }
    bool isall0() const { return m_max_rank1 == 0; }
    bool isall1() const { return m_max_rank0 == 0; }
protected:
#pragma pack(push,4)
    struct TERARK_DLL_EXPORT RankCache512 {
        rank_cache_base_t base;
        uint64_t          rela;
        explicit RankCache512(index_t l1) {
            static_assert(sizeof(RankCache512) == sizeof(rank_cache_base_t) + sizeof(rela), "bad RankCache512 size");
            base = l1;
            rela = 0;
        }
        operator size_t() const { return base; }
    };
#pragma pack(pop)
    void nullize_cache() noexcept;
    RankCache512* m_rank_cache;
    index_t*   m_sel0_cache;
    index_t*   m_sel1_cache;
    size_t     m_max_rank0;
    size_t     m_max_rank1;
    size_t select0_upper_bound_line_safe(size_t id) const noexcept;
    size_t select1_upper_bound_line_safe(size_t id) const noexcept;
    static inline size_t select0_upper_bound_line(const bm_uint_t* bits, const index_t* sel0, const RankCache512*, size_t id) noexcept;
    static inline size_t select1_upper_bound_line(const bm_uint_t* bits, const index_t* sel1, const RankCache512*, size_t id) noexcept;
public:
    const RankCache512* get_rank_cache() const { return m_rank_cache; }
    const index_t* get_sel0_cache() const { return m_sel0_cache; }
    const index_t* get_sel1_cache() const { return m_sel1_cache; }
    static inline size_t fast_rank0(const bm_uint_t* bits, const RankCache512* rankCache, size_t bitpos) noexcept;
    static inline size_t fast_rank1(const bm_uint_t* bits, const RankCache512* rankCache, size_t bitpos) noexcept;
    static inline size_t fast_select0(const bm_uint_t* bits, const index_t* sel0, const RankCache512* rankCache, size_t id) noexcept;
    static inline size_t fast_select1(const bm_uint_t* bits, const index_t* sel1, const RankCache512* rankCache, size_t id) noexcept;

    size_t excess1(size_t bp) const { return 2*rank1(bp) - bp; }
    static size_t fast_excess1(const bm_uint_t* bits, const RankCache512* rankCache, size_t bitpos)
        { return 2 * fast_rank1(bits, rankCache, bitpos) - bitpos; }

    void prefetch_rank1(size_t bitpos) const noexcept
        { _mm_prefetch((const char*)&m_rank_cache[bitpos/LineBits], _MM_HINT_T0); }
    static void fast_prefetch_rank1(const RankCache512* rankCache, size_t bitpos) noexcept
        { _mm_prefetch((const char*)&rankCache[bitpos/LineBits], _MM_HINT_T0); }
};

template<class rank_cache_base_t>
inline size_t rank_select_se_512_tpl<rank_cache_base_t>::
rank0(size_t bitpos) const noexcept {
    assert(bitpos <= m_size); // bitpos can be m_size
    return bitpos - rank1(bitpos);
}

template<class rank_cache_base_t>
inline size_t rank_select_se_512_tpl<rank_cache_base_t>::
rank1(size_t bitpos) const noexcept {
    assert(bitpos <= m_size); // bitpos can be m_size
    const RankCache512& rc = m_rank_cache[bitpos / 512];
    const uint64_t* pu64 = (const uint64_t*)this->m_words;
    size_t k = bitpos % 512 / 64;
    size_t tail = fast_popcount_trail(pu64[bitpos / 64], bitpos % 64);
    return rc.base + tail + TERARK_GET_BITS_64(rc.rela, k, 9);
}

template<class rank_cache_base_t>
inline size_t rank_select_se_512_tpl<rank_cache_base_t>::
fast_rank0(const bm_uint_t* bits, const RankCache512* rankCache, size_t bitpos) noexcept {
    return bitpos - fast_rank1(bits, rankCache, bitpos);
}

template<class rank_cache_base_t>
inline size_t rank_select_se_512_tpl<rank_cache_base_t>::
fast_rank1(const bm_uint_t* bits, const RankCache512* rankCache, size_t bitpos) noexcept {
    const RankCache512& rc = rankCache[bitpos / 512];
    const uint64_t* pu64 = (const uint64_t*)bits;
    size_t k = bitpos % 512 / 64;
    size_t tail = fast_popcount_trail(pu64[bitpos / 64], bitpos % 64);
    return rc.base + tail + TERARK_GET_BITS_64(rc.rela, k, 9);
}

template<class rank_cache_base_t>
inline size_t rank_select_se_512_tpl<rank_cache_base_t>::
select0_upper_bound_line
(const bm_uint_t* bits, const index_t* sel0, const RankCache512* rankCache, size_t Rank0)
noexcept {
    size_t lo = sel0[Rank0 / LineBits];
    size_t hi = sel0[Rank0 / LineBits + 1];
  #if defined(__AVX512VL__) && defined(__AVX512BW__) && 0
    size_t veclen;
    while ((veclen = hi - lo) > 4) {
        size_t mid = (lo + hi) / 2;
        size_t mid_val = LineBits * mid - rank_cache[mid].base;
        if (mid_val <= Rank0) // upper_bound
            lo = mid + 1;
        else
            hi = mid;
    }
    if (sizeof(rank_cache_base_t) == 8) { // rank_cache.base is uint64
        __mmask16 k = _bzhi_u32(0x5555, veclen*2);
        __m512i vec0 = _mm512_add_epi64(_mm512_set1_epi64(lo), _mm512_set_epi64(0,3, 0,2, 0,1, 0,0));
        vec0 = _mm512_sllv_epi64(vec0, _mm512_set1_epi64(LineShift));
        __m512i vec1 = _mm512_maskz_loadu_epi64(k, &rank_cache[lo]);
        __m512i vec2 = _mm512_sub_epi64(vec0, vec1);
        __m512i key = _mm512_set1_epi64(Rank0);
        __mmask8 cmp = _mm256_mask_cmpgt_epi32_mask(k, vec2, key);
        auto tz = _tzcnt_u32(cmp | (1u << (veclen*2))); // upper bound
        lo += tz / 2;
        TERARK_ASSERT_LT(Rank0, LineBits * lo - rankCache[lo].lev1);
    } else {
        __mmask16 k = _bzhi_u32(-1, veclen);
        __m128i vec0 = _mm_add_epi32(_mm_set1_epi32(lo), _mm_set_epi32(3,2,1,0));
        vec0 = _mm_sllv_epi32(vec0, _mm_set1_epi32(LineShift));
        __m512i vec1 = _mm512_maskz_loadu_epi32(_bzhi_u32(001111, veclen*3), &rank_cache[lo]);
        vec1 = _mm512_mask_permutexvar_epi32(_mm512_setzero_si512(), k,
            _mm512_set_epi32(0,0,0,0,   0,0,3, 0,0,2, 0,0,1, 0,0,0), vec1);
        __m128i vec2 = _mm_sub_epi32(vec0, _mm512_castsi512_si128(vec1));
        __m128i key = _mm_set1_epi32(Rank0);
        __mmask8 cmp = _mm_mask_cmpgt_epi32_mask(k, vec2, key);
        auto tz = _tzcnt_u32(cmp | (1u << veclen)); // upper bound
        lo += tz;
        TERARK_ASSERT_LT(Rank0, LineBits * lo - rankCache[lo].lev1);
    }
  #else
    if (hi - lo < 32) {
        while (LineBits * lo - rankCache[lo].base <= Rank0) lo++;
    }
    else {
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            size_t mid_val = LineBits * mid - rankCache[mid].base;
            if (mid_val <= Rank0) // upper_bound
                lo = mid + 1;
            else
                hi = mid;
        }
    }
  #endif
    return lo;
}

template<class rank_cache_base_t>
inline size_t rank_select_se_512_tpl<rank_cache_base_t>::fast_select0
(const bm_uint_t* bits, const index_t* sel0, const RankCache512* rankCache, size_t Rank0)
noexcept {
    size_t lo = select0_upper_bound_line(bits, sel0, rankCache, Rank0);
    assert(Rank0 < LineBits * lo - rankCache[lo].base);
    const uint64_t* pBit64 = (const uint64_t*)(bits + LineWords * (lo-1));
    _mm_prefetch((const char*)(pBit64+0), _MM_HINT_T0);
    _mm_prefetch((const char*)(pBit64+7), _MM_HINT_T0);
    size_t hit = LineBits * (lo-1) - rankCache[lo-1].base;
    size_t line_bitpos = (lo-1) * LineBits;
    uint64_t rcRela = rankCache[lo-1].rela;

#define select0_nth64(n) line_bitpos + 64*n + \
    UintSelect1(~pBit64[n], Rank0 - (hit + 64*n - rank512(rcRela, n)))

  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    __m512i arr0 = _mm512_set_epi64(64*7, 64*6, 64*5, 64*4, 64*3, 64*2, 64*1, 0);
    __m512i shift = _mm512_set_epi64(54, 45, 36, 27, 18, 9, 0, 64);
    __m512i arr1 = _mm512_set1_epi64(rcRela);
    __m512i arr2 = _mm512_srlv_epi64(arr1, shift);
    __m512i arr3 = _mm512_and_epi64(arr2, _mm512_set1_epi64(0x1FF));
    __m512i arr = _mm512_sub_epi64(arr0, arr3);
    __m512i key = _mm512_set1_epi64(Rank0 - hit);
    __mmask8 cmp = _mm512_cmpgt_epi64_mask(arr, key);
    auto tz = _tzcnt_u32(cmp | (1u << 8)); // upper bound
    TERARK_ASSERT_GE(tz, 1);
    TERARK_ASSERT_LE(tz, 8);
    tz -= 1;
    return select0_nth64(tz); // rank512 must use TERARK_GET_BITS_64
  #else
    if (Rank0 < hit + 64*4 - rank512(rcRela, 4)) {
        if (Rank0 < hit + 64*2 - rank512(rcRela, 2))
            if (Rank0 < hit + 64*1 - rank512(rcRela, 1))
                return line_bitpos + UintSelect1(~pBit64[0], Rank0 - hit);
            else
                return select0_nth64(1);
        else
            if (Rank0 < hit + 64*3 - rank512(rcRela, 3))
                return select0_nth64(2);
            else
                return select0_nth64(3);
    } else {
        if (Rank0 < hit + 64*6 - rank512(rcRela, 6))
            if (Rank0 < hit + 64*5 - rank512(rcRela, 5))
                return select0_nth64(4);
            else
                return select0_nth64(5);
        else
            if (Rank0 < hit + 64*7 - rank512(rcRela, 7))
                return select0_nth64(6);
            else
                return select0_nth64(7);
    }
  #endif
#undef select0_nth64
}

template<class rank_cache_base_t>
inline size_t rank_select_se_512_tpl<rank_cache_base_t>::
select1_upper_bound_line
(const bm_uint_t* bits, const index_t* sel1, const RankCache512* rankCache, size_t Rank1)
noexcept {
    size_t lo = sel1[Rank1 / LineBits];
    size_t hi = sel1[Rank1 / LineBits + 1];
    if (hi - lo < 32) {
        while (rankCache[lo].base <= Rank1) lo++;
    }
    else {
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            size_t mid_val = rankCache[mid].base;
            if (mid_val <= Rank1) // upper_bound
                lo = mid + 1;
            else
                hi = mid;
        }
    }
    return lo;
}

template<class rank_cache_base_t>
inline size_t rank_select_se_512_tpl<rank_cache_base_t>::fast_select1
(const bm_uint_t* bits, const index_t* sel1, const RankCache512* rankCache, size_t Rank1)
noexcept {
    size_t lo = select1_upper_bound_line(bits, sel1, rankCache, Rank1);
    assert(Rank1 < rankCache[lo].base);
    const uint64_t* pBit64 = (const uint64_t*)(bits + LineWords * (lo-1));
    _mm_prefetch((const char*)(pBit64+0), _MM_HINT_T0);
    _mm_prefetch((const char*)(pBit64+7), _MM_HINT_T0);
    size_t hit = rankCache[lo-1].base;
    size_t line_bitpos = (lo-1) * LineBits;
    uint64_t rcRela = rankCache[lo-1].rela;

#define select1_nth64(n) line_bitpos + 64*n + \
     UintSelect1(pBit64[n], Rank1 - (hit + rank512(rcRela, n)))

  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    // manual optimize, group 0 is always 0 and is not stored,
    // the highest bit of 64bits is always 0, so right shift 63 yield 0,
    // _mm512_srlv_epi64 set to 0 if shift count >= 64
    __m512i shift = _mm512_set_epi64(54, 45, 36, 27, 18, 9, 0, 64);
    __m512i arr1 = _mm512_set1_epi64(rcRela);
    __m512i arr2 = _mm512_srlv_epi64(arr1, shift);
    __m512i arr = _mm512_and_epi64(arr2, _mm512_set1_epi64(0x1FF));
    __m512i key = _mm512_set1_epi64(Rank1 - hit);
    __mmask8 cmp = _mm512_cmpgt_epi64_mask(arr, key);
    auto tz = _tzcnt_u32(cmp | (1u << 8)); // upper bound
    TERARK_ASSERT_GE(tz, 1);
    TERARK_ASSERT_LE(tz, 8);
    tz -= 1;
    return select1_nth64(tz); // rank512 must use TERARK_GET_BITS_64
  #else
    if (Rank1 < hit + rank512(rcRela, 4)) {
        if (Rank1 < hit + rank512(rcRela, 2))
            if (Rank1 < hit + rank512(rcRela, 1))
                return line_bitpos + UintSelect1(pBit64[0], Rank1 - hit);
            else
                return select1_nth64(1);
        else
            if (Rank1 < hit + rank512(rcRela, 3))
                return select1_nth64(2);
            else
                return select1_nth64(3);
    } else {
        if (Rank1 < hit + rank512(rcRela, 6))
            if (Rank1 < hit + rank512(rcRela, 5))
                return select1_nth64(4);
            else
                return select1_nth64(5);
        else
            if (Rank1 < hit + rank512(rcRela, 7))
                return select1_nth64(6);
            else
                return select1_nth64(7);
    }
  #endif
#undef select1_nth64
}

typedef rank_select_se_512_tpl<uint32_t> rank_select_se_512;
typedef rank_select_se_512_tpl<uint32_t> rank_select_se_512_32;
typedef rank_select_se_512_tpl<uint64_t> rank_select_se_512_64;

} // namespace terark
