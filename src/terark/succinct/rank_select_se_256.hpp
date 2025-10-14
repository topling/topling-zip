#pragma once

#include "rank_select_basic.hpp"

namespace terark {

// rank   use 2-level cache, time is O(1), 2 memory access
// select use 1-level cache, time is O(1+loglog(n))
// rank_select_se, "_se" means "separated"
// rank index is separated from bits
class TERARK_DLL_EXPORT rank_select_se
    : public RankSelectConstants<256>, public febitvec {
public:
    typedef boost::mpl::false_ is_mixed;
    typedef uint32_t index_t;
    rank_select_se();
    rank_select_se(size_t n, bool val = false);
    rank_select_se(size_t n, valvec_no_init);
    rank_select_se(size_t n, valvec_reserve);
    rank_select_se(const rank_select_se&);
    rank_select_se& operator=(const rank_select_se&);
    rank_select_se(rank_select_se&& y) noexcept;
    rank_select_se& operator=(rank_select_se&& y) noexcept;
    ~rank_select_se();
    void clear() noexcept;
    void risk_release_ownership() noexcept;
    void risk_mmap_from(unsigned char* base, size_t length);
    void shrink_to_fit() noexcept;

    void swap(rank_select_se&) noexcept;
    void build_cache(bool speed_select0, bool speed_select1);
    size_t mem_size() const { return m_capacity / 8; }
    inline size_t rank0(size_t bitpos) const noexcept;
    inline size_t rank1(size_t bitpos) const noexcept;
    size_t select0(size_t id) const noexcept terark_pure_func;
    size_t select1(size_t id) const noexcept terark_pure_func;
    size_t max_rank1() const { return m_max_rank1; }
    size_t max_rank0() const { return m_max_rank0; }
    bool isall0() const { return m_max_rank1 == 0; }
    bool isall1() const { return m_max_rank0 == 0; }
protected:
    void nullize_cache() noexcept;
    struct RankCache {
        uint32_t  lev1;
        uint8_t   lev2[4]; // use uint64 for rank-select word
        explicit RankCache(uint32_t l1);
        operator size_t() const { return lev1; }
    };
    RankCache* m_rank_cache;
    uint32_t*  m_sel0_cache;
    uint32_t*  m_sel1_cache;
    size_t     m_max_rank0;
    size_t     m_max_rank1;
    size_t select0_upper_bound_line_safe(size_t id) const noexcept;
    size_t select1_upper_bound_line_safe(size_t id) const noexcept;
    static size_t select0_upper_bound_line(const bm_uint_t* bits, const uint32_t* sel0, const RankCache*, size_t id) noexcept;
    static size_t select1_upper_bound_line(const bm_uint_t* bits, const uint32_t* sel1, const RankCache*, size_t id) noexcept;
public:
    const RankCache* get_rank_cache() const { return m_rank_cache; }
    const uint32_t* get_sel0_cache() const { return m_sel0_cache; }
    const uint32_t* get_sel1_cache() const { return m_sel1_cache; }

    static inline size_t fast_rank0(const bm_uint_t* bits, const RankCache* rankCache, size_t bitpos) noexcept;
    static inline size_t fast_rank1(const bm_uint_t* bits, const RankCache* rankCache, size_t bitpos) noexcept;
    static inline size_t fast_select0(const bm_uint_t* bits, const uint32_t* sel0, const RankCache* rankCache, size_t id) noexcept;
    static inline size_t fast_select1(const bm_uint_t* bits, const uint32_t* sel1, const RankCache* rankCache, size_t id) noexcept;

    size_t excess1(size_t bp) const { return 2*rank1(bp) - bp; }
    static size_t fast_excess1(const bm_uint_t* bits, const RankCache* rankCache, size_t bitpos)
        { return 2 * fast_rank1(bits, rankCache, bitpos) - bitpos; }

    void prefetch_rank1(size_t bitpos) const noexcept
        { _mm_prefetch((const char*)&m_rank_cache[bitpos/LineBits], _MM_HINT_T0); }
    static void fast_prefetch_rank1(const RankCache* rankCache, size_t bitpos) noexcept
        { _mm_prefetch((const char*)&rankCache[bitpos/LineBits], _MM_HINT_T0); }
};

inline size_t rank_select_se::rank0(size_t bitpos) const noexcept {
    assert(bitpos <= m_size);
    return bitpos - rank1(bitpos);
}

inline size_t rank_select_se::rank1(size_t bitpos) const noexcept {
    assert(bitpos <= m_size);
    RankCache rc = m_rank_cache[bitpos / LineBits];
    return rc.lev1 + rc.lev2[(bitpos / 64) % 4] +
        fast_popcount_trail(
            ((const uint64_t*)this->m_words)[bitpos / 64], bitpos % 64);
}

inline size_t rank_select_se::
fast_rank0(const bm_uint_t* bits, const RankCache* rankCache, size_t bitpos) noexcept {
    return bitpos - fast_rank1(bits, rankCache, bitpos);
}

inline size_t rank_select_se::
fast_rank1(const bm_uint_t* bits, const RankCache* rankCache, size_t bitpos) noexcept {
    RankCache rc = rankCache[bitpos / LineBits];
    return rc.lev1 + rc.lev2[(bitpos / 64) % 4] +
        fast_popcount_trail(
            ((const uint64_t*)bits)[bitpos / 64], bitpos % 64);
}

inline size_t rank_select_se::select0_upper_bound_line
(const bm_uint_t* bits, const uint32_t* sel0, const RankCache* rankCache, size_t Rank0)
noexcept {
    size_t lo = sel0[Rank0 / LineBits];
    size_t hi = sel0[Rank0 / LineBits + 1];
  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    size_t sublen;
    while (terark_unlikely((sublen = hi - lo) > 8)) {
        size_t mid = (lo + hi) / 2;
        size_t mid_val = LineBits * mid - rankCache[mid].lev1;
        if (mid_val <= Rank0) // upper_bound
            lo = mid + 1;
        else
            hi = mid;
    }
    {
        __m256i vec0 = _mm256_add_epi32(_mm256_set1_epi32(lo), _mm256_set_epi32(7,6,5,4,3,2,1,0));
        vec0 = _mm256_sllv_epi32(vec0, _mm256_set1_epi32(LineShift));
        __mmask8 k = _bzhi_u32(-1, sublen);
        __m512i vec1 = _mm512_maskz_loadu_epi64(k, &rankCache[lo]);
        __m256i vec2 = _mm512_cvtepi64_epi32(vec1); // keep lev1(low32)
        __m256i vec3 = _mm256_sub_epi32(vec0, vec2);
        __m256i key = _mm256_set1_epi32(Rank0);
        __mmask8 cmp = _mm256_mask_cmpgt_epi32_mask(k, vec3, key);
        auto tz = _tzcnt_u32(cmp | (1u << sublen)); // upper bound
        lo += tz;
        TERARK_ASSERT_LT(Rank0, LineBits * lo - rankCache[lo].lev1);
    }
  #else
    if (hi - lo < 32) {
        while (LineBits * lo - rankCache[lo].lev1 <= Rank0) lo++;
    }
    else {
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            size_t mid_val = LineBits * mid - rankCache[mid].lev1;
            if (mid_val <= Rank0) // upper_bound
                lo = mid + 1;
            else
                hi = mid;
        }
    }
  #endif
    return lo;
}

inline size_t rank_select_se::fast_select0
(const bm_uint_t* bits, const uint32_t* sel0, const RankCache* rankCache, size_t Rank0)
noexcept {
    size_t lo = select0_upper_bound_line(bits, sel0, rankCache, Rank0);
    const uint64_t* pBit64 = (const uint64_t*)(bits + LineWords * (lo-1));
    _mm_prefetch((const char*)(pBit64+0), _MM_HINT_T0);
    _mm_prefetch((const char*)(pBit64+3), _MM_HINT_T0);
    assert(Rank0 < LineBits * lo - rankCache[lo].lev1);
    size_t line_bitpos = (lo-1) * LineBits;
    const RankCache& rc = rankCache[lo-1];
    size_t hit = LineBits * (lo-1) - rc.lev1;

  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    __m128i arr1 = _mm_set_epi32(64 * 3, 64 * 2, 64 * 1, 0);
    __m128i arr2 = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*(uint32_t*)rc.lev2));
    __m128i arr = _mm_sub_epi32(arr1, arr2); // rc.lev2[0] is always 0
    __m128i key = _mm_set1_epi32(uint32_t(Rank0 - hit));
    __mmask8 cmp = _mm_cmpgt_epi32_mask(arr, key);
    auto tz = _tzcnt_u32(cmp | (1u << 4)); // upper bound
    TERARK_ASSERT_GE(tz, 1);
    TERARK_ASSERT_LE(tz, 4);
    tz -= 1;
    return line_bitpos + 64 * tz + UintSelect1(~pBit64[tz], Rank0 - (hit + 64 * tz - rc.lev2[tz]));
  #else
    if (Rank0 < hit + 64*2 - rc.lev2[2]) {
        if (Rank0 < hit + 64*1 - rc.lev2[1]) { // rc.lev2[0] is always 0
            return line_bitpos + UintSelect1(~pBit64[0], Rank0 - hit);
        }
        return line_bitpos + 64*1 +
            UintSelect1(~pBit64[1], Rank0 - (hit + 64*1 - rc.lev2[1]));
    }
    if (Rank0 < hit + 64*3 - rc.lev2[3]) {
        return line_bitpos + 64*2 +
            UintSelect1(~pBit64[2], Rank0 - (hit + 64*2 - rc.lev2[2]));
    }
    else {
        return line_bitpos + 64 * 3 +
            UintSelect1(~pBit64[3], Rank0 - (hit + 64*3 - rc.lev2[3]));
    }
  #endif
}

inline size_t rank_select_se::select1_upper_bound_line
(const bm_uint_t* bits, const uint32_t* sel1, const RankCache* rankCache, size_t Rank1)
noexcept {
    size_t lo = sel1[Rank1 / LineBits];
    size_t hi = sel1[Rank1 / LineBits + 1];
  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    size_t sublen;
    while (terark_unlikely((sublen = hi - lo) > 8)) {
        size_t mid = (lo + hi) / 2;
        size_t mid_val = rankCache[mid].lev1;
        if (mid_val <= Rank1) // upper_bound
            lo = mid + 1;
        else
            hi = mid;
    }
    {
        __mmask8 k = _bzhi_u32(-1, sublen);
        __m512i vec1 = _mm512_maskz_loadu_epi64(k, &rankCache[lo]);
        __m256i vec2 = _mm512_cvtepi64_epi32(vec1); // keep lev1(low32)
        __m256i key = _mm256_set1_epi32(Rank1);
        __mmask8 cmp = _mm256_mask_cmpgt_epi32_mask(k, vec2, key);
        auto tz = _tzcnt_u32(cmp | (1u << sublen)); // upper bound
        lo += tz;
        TERARK_ASSERT_LT(Rank1, rankCache[lo].lev1);
    }
  #else
    if (hi - lo < 32) {
        while (rankCache[lo].lev1 <= Rank1) lo++;
    }
    else {
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            size_t mid_val = rankCache[mid].lev1;
            if (mid_val <= Rank1) // upper_bound
                lo = mid + 1;
            else
                hi = mid;
        }
    }
  #endif
    return lo;
}

inline size_t rank_select_se::fast_select1
(const bm_uint_t* bits, const uint32_t* sel1, const RankCache* rankCache, size_t Rank1)
noexcept {
    size_t lo = select1_upper_bound_line(bits, sel1, rankCache, Rank1);
    const uint64_t* pBit64 = (const uint64_t*)(bits + LineWords * (lo-1));
    _mm_prefetch((const char*)(pBit64+0), _MM_HINT_T0);
    _mm_prefetch((const char*)(pBit64+3), _MM_HINT_T0);
    assert(Rank1 < rankCache[lo].lev1);
    size_t line_bitpos = (lo-1) * LineBits;
    const RankCache& rc = rankCache[lo-1];
    size_t hit = rc.lev1;

  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    __m128i arr = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*(uint32_t*)rc.lev2));
    __m128i key = _mm_set1_epi32(uint32_t(Rank1 - hit));
    __mmask8 cmp = _mm_cmpgt_epi32_mask(arr, key);
    auto tz = _tzcnt_u32(cmp | (1u << 4)); // upper bound
    TERARK_ASSERT_GE(tz, 1);
    TERARK_ASSERT_LE(tz, 4);
    tz -= 1;
    return line_bitpos + 64 * tz + UintSelect1(pBit64[tz], Rank1 - (hit + rc.lev2[tz]));
  #else
    if (Rank1 < hit + rc.lev2[2]) {
        if (Rank1 < hit + rc.lev2[1]) { // rc.lev2[0] is always 0
            return line_bitpos + UintSelect1(pBit64[0], Rank1 - hit);
        }
        return line_bitpos + 64*1 +
             UintSelect1(pBit64[1], Rank1 - (hit + rc.lev2[1]));
    }
    if (Rank1 < hit + rc.lev2[3]) {
        return line_bitpos + 64*2 +
             UintSelect1(pBit64[2], Rank1 - (hit + rc.lev2[2]));
    }
    else {
        return line_bitpos + 64*3 +
             UintSelect1(pBit64[3], Rank1 - (hit + rc.lev2[3]));
    }
  #endif
}

typedef rank_select_se rank_select_se_256;
typedef rank_select_se rank_select_se_256_32;

} // namespace terark
