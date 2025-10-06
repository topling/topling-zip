#pragma once

#include "rank_select_basic.hpp"
#include "rank_select_mixed_basic.hpp"

namespace terark {

class TERARK_DLL_EXPORT rank_select_mixed_il_256 : public RankSelectConstants<256> {
public:
    typedef boost::mpl::true_ is_mixed;
    typedef uint32_t index_t;
    rank_select_mixed_il_256();
    rank_select_mixed_il_256(size_t n, bool val0 = false, bool val1 = false);
    rank_select_mixed_il_256(size_t n, valvec_no_init);
    rank_select_mixed_il_256(size_t n, valvec_reserve);
    rank_select_mixed_il_256(const rank_select_mixed_il_256&);
    rank_select_mixed_il_256& operator=(const rank_select_mixed_il_256&);
    rank_select_mixed_il_256(rank_select_mixed_il_256&& y) noexcept;
    rank_select_mixed_il_256& operator=(rank_select_mixed_il_256&& y) noexcept;

    ~rank_select_mixed_il_256();
    void clear() noexcept;
    void risk_release_ownership() noexcept;
    void risk_mmap_from(unsigned char* base, size_t length);
    void shrink_to_fit() noexcept;

    void swap(rank_select_mixed_il_256&) noexcept;
    const void* data() const noexcept { return m_lines; }
    size_t mem_size() const noexcept { return m_capacity; }

protected:
    struct RankCacheMixed {
        struct {
            uint32_t      base;
            unsigned char rlev[4];
            union {
                uint64_t  bit64[4];
                bm_uint_t words[LineWords];
            };
        } mixed[2];
        template<size_t dimensions> size_t get_base() const { return mixed[dimensions].base; }
    };
    typedef RankCacheMixed bldata_t;

    static size_t fix_resize_size(size_t bits) noexcept {
        rank_select_check_overflow(bits, > , rank_select_mixed_il_256);
        return (bits + LineBits - 1) & ~(LineBits - 1);
    }
    void grow() noexcept;
    void reserve_bytes(size_t bytes_capacity);
    void reserve(size_t bits_capacity);
    void nullize_cache() noexcept;
    const RankCacheMixed* bldata() const { return m_lines; }

    template<size_t dimensions> void bits_range_set0_dx(size_t i, size_t k) noexcept;
    template<size_t dimensions> void bits_range_set1_dx(size_t i, size_t k) noexcept;

    template<size_t dimensions>
    void set_word_dx(size_t word_idx, bm_uint_t bits) noexcept {
        assert(word_idx < num_words_dx<dimensions>());
        m_lines[word_idx / LineWords].mixed[dimensions].words[word_idx % LineWords] = bits;
    }
    template<size_t dimensions>
    bm_uint_t get_word_dx(size_t word_idx) const noexcept {
        assert(word_idx < num_words_dx<dimensions>());
        return m_lines[word_idx / LineWords].mixed[dimensions].words[word_idx % LineWords];
    }
    template<size_t dimensions>
    size_t num_words_dx() const noexcept { return (m_size[dimensions] + WordBits - 1) / WordBits; }

    template<size_t dimensions>
    void push_back_dx(bool val) noexcept {
        rank_select_check_overflow(m_size[dimensions], >= , rank_select_mixed_il_256);
        assert(m_size[dimensions] <= m_capacity / sizeof(RankCacheMixed) * LineBits);
        if (terark_unlikely(m_size[dimensions] == m_capacity / sizeof(RankCacheMixed) * LineBits))
            grow();
        size_t i = m_size[dimensions]++;
        val ? set1_dx<dimensions>(i) : set0_dx<dimensions>(i);
    }
    template<size_t dimensions>
    bool is0_dx(size_t i) const noexcept {
        assert(i < m_size[dimensions]);
        return !terark_bit_test(m_lines[i / LineBits].mixed[dimensions].words, i % LineBits);
    }
    template<size_t dimensions>
    bool is1_dx(size_t i) const noexcept {
        assert(i < m_size[dimensions]);
        return terark_bit_test(m_lines[i / LineBits].mixed[dimensions].words, i % LineBits);
    }
    template<size_t dimensions>
    void set0_dx(size_t i) noexcept {
        assert(i < m_size[dimensions]);
        terark_bit_set0(m_lines[i / LineBits].mixed[dimensions].words, i % LineBits);
    }
    template<size_t dimensions>
    void set1_dx(size_t i) noexcept {
        assert(i < m_size[dimensions]);
        terark_bit_set1(m_lines[i / LineBits].mixed[dimensions].words, i % LineBits);
    }
    template<size_t dimensions> void build_cache_dx(bool speed_select0, bool speed_select1);
    template<size_t dimensions> size_t one_seq_len_dx(size_t bitpos) const noexcept terark_pure_func;
    template<size_t dimensions> size_t zero_seq_len_dx(size_t bitpos) const noexcept terark_pure_func;
    template<size_t dimensions> size_t one_seq_revlen_dx(size_t endpos) const noexcept terark_pure_func;
    template<size_t dimensions> size_t zero_seq_revlen_dx(size_t endpos) const noexcept terark_pure_func;
    template<size_t dimensions> inline size_t rank0_dx(size_t bitpos) const noexcept;
    template<size_t dimensions> inline size_t rank1_dx(size_t bitpos) const noexcept;
    template<size_t dimensions> size_t select0_dx(size_t id) const noexcept terark_pure_func;
    template<size_t dimensions> size_t select1_dx(size_t id) const noexcept terark_pure_func;

public:
    template<size_t dimensions>
    rank_select_mixed_dimensions<rank_select_mixed_il_256, dimensions>& get() noexcept {
        static_assert(dimensions < 2, "dimensions must less than 2 !");
        return *reinterpret_cast<rank_select_mixed_dimensions<rank_select_mixed_il_256, dimensions>*>(this);
    }
    template<size_t dimensions>
    const rank_select_mixed_dimensions<rank_select_mixed_il_256, dimensions>& get() const noexcept {
        static_assert(dimensions < 2, "dimensions must less than 2 !");
        return *reinterpret_cast<const rank_select_mixed_dimensions<rank_select_mixed_il_256, dimensions>*>(this);
    }
    rank_select_mixed_dimensions<rank_select_mixed_il_256, 0>& first () noexcept { return get<0>(); }
    rank_select_mixed_dimensions<rank_select_mixed_il_256, 1>& second() noexcept { return get<1>(); }
    rank_select_mixed_dimensions<rank_select_mixed_il_256, 0>& left  () noexcept { return get<0>(); }
    rank_select_mixed_dimensions<rank_select_mixed_il_256, 1>& right () noexcept { return get<1>(); }

protected:
    RankCacheMixed* m_lines;
    size_t m_size[2];
    size_t m_capacity;  // bytes;
    union
    {
        uint64_t m_flags;
        struct
        {
            uint64_t is_first_load_d1  : 1;
            uint64_t has_d0_rank_cache : 1;
            uint64_t has_d0_sel0_cache : 1;
            uint64_t has_d0_sel1_cache : 1;
            uint64_t has_d1_rank_cache : 1;
            uint64_t has_d1_sel0_cache : 1;
            uint64_t has_d1_sel1_cache : 1;
        } m_flags_debug;
    };
    uint32_t*  m_sel0_cache[2];
    uint32_t*  m_sel1_cache[2];
    size_t     m_max_rank0[2];
    size_t     m_max_rank1[2];

    const RankCacheMixed* get_rank_cache_base() const noexcept { return m_lines; }

    template<size_t dimensions> size_t select0_upper_bound_line_safe(size_t id) const noexcept;
    template<size_t dimensions> size_t select1_upper_bound_line_safe(size_t id) const noexcept;

    template<size_t dimensions>
    static size_t select0_upper_bound_line(const bldata_t* bits, const uint32_t* sel0, size_t id) noexcept;
    template<size_t dimensions>
    static size_t select1_upper_bound_line(const bldata_t* bits, const uint32_t* sel1, size_t id) noexcept;

public:
    template<size_t dimensions>
    static inline bool fast_is0_dx(const bldata_t* bits, size_t i) noexcept;
    template<size_t dimensions>
    static inline bool fast_is1_dx(const bldata_t* bits, size_t i) noexcept;

    template<size_t dimensions>
    static inline size_t fast_rank0_dx(const bldata_t* bits, const RankCacheMixed* rankCache, size_t bitpos) noexcept;
    template<size_t dimensions>
    static inline size_t fast_rank1_dx(const bldata_t* bits, const RankCacheMixed* rankCache, size_t bitpos) noexcept;
    template<size_t dimensions>
    static inline size_t fast_select0_dx(const bldata_t* bits, const uint32_t* sel0, const RankCacheMixed* rankCache, size_t id) noexcept;
    template<size_t dimensions>
    static inline size_t fast_select1_dx(const bldata_t* bits, const uint32_t* sel1, const RankCacheMixed* rankCache, size_t id) noexcept;

    template<size_t dimensions>
    void prefetch_bit_dx(size_t i) const noexcept
      { _mm_prefetch((const char*)&m_lines[i / LineBits].mixed[dimensions].bit64[i % LineBits / 64], _MM_HINT_T0); }
    template<size_t dimensions>
    static void fast_prefetch_bit_dx(const bldata_t* m_lines, size_t i) noexcept
      { _mm_prefetch((const char*)&m_lines[i / LineBits].mixed[dimensions].bit64[i % LineBits / 64], _MM_HINT_T0); }

    template<size_t dimensions>
    void prefetch_rank1_dx(size_t /*bitpos*/) const noexcept
      { /*_mm_prefetch((const char*)&m_lines[bitpos / LineBits].mixed[dimensions].rlev, _MM_HINT_T0);*/ }
    template<size_t dimensions>
    static void fast_prefetch_rank1_dx(const RankCacheMixed* /*rankCache*/, size_t /*bitpos*/) noexcept
      { /*_mm_prefetch((const char*)&rankCache[bitpos / LineBits].mixed[dimensions].rlev, _MM_HINT_T0);*/ }
};

template<size_t dimensions>
inline size_t rank_select_mixed_il_256::
rank0_dx(size_t bitpos) const noexcept {
    assert(bitpos <= m_size[dimensions]);
    return bitpos - rank1_dx<dimensions>(bitpos);
}

template<size_t dimensions>
inline size_t rank_select_mixed_il_256::
rank1_dx(size_t bitpos) const noexcept {
    assert(m_flags & (1 << (dimensions == 0 ? 1 : 4)));
    assert(bitpos <= m_size[dimensions]);
    const auto& line = m_lines[bitpos / LineBits].mixed[dimensions];
    return line.base + line.rlev[bitpos % LineBits / 64]
        + fast_popcount_trail(line.bit64[bitpos % LineBits / 64], bitpos % 64);
}

template<size_t dimensions>
inline bool rank_select_mixed_il_256::
fast_is0_dx(const bldata_t* m_lines, size_t i) noexcept {
    return !terark_bit_test(m_lines[i / LineBits].mixed[dimensions].words, i % LineBits);
}

template<size_t dimensions>
inline bool rank_select_mixed_il_256::
fast_is1_dx(const bldata_t* m_lines, size_t i) noexcept {
    return terark_bit_test(m_lines[i / LineBits].mixed[dimensions].words, i % LineBits);
}

template<size_t dimensions>
inline size_t rank_select_mixed_il_256::
fast_rank0_dx(const bldata_t* m_lines, const RankCacheMixed* rankCache, size_t bitpos) noexcept {
    return bitpos - fast_rank1_dx<dimensions>(m_lines, rankCache, bitpos);
}

template<size_t dimensions>
inline size_t rank_select_mixed_il_256::
fast_rank1_dx(const bldata_t* m_lines, const RankCacheMixed*, size_t bitpos) noexcept {
    const auto& line = m_lines[bitpos / LineBits].mixed[dimensions];
    return line.base + line.rlev[bitpos % LineBits / 64]
        + fast_popcount_trail(line.bit64[bitpos % LineBits / 64], bitpos % 64);
}

template<size_t dimensions>
inline size_t rank_select_mixed_il_256::select0_upper_bound_line
(const bldata_t* m_lines, const uint32_t* sel0, size_t Rank0) noexcept {
    size_t lo = sel0[Rank0 / LineBits];
    size_t hi = sel0[Rank0 / LineBits + 1];
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        size_t mid_val = LineBits * mid - m_lines[mid].mixed[dimensions].base;
        if (mid_val <= Rank0) // upper_bound
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}
template<size_t dimensions>
inline size_t rank_select_mixed_il_256::fast_select0_dx
(const bldata_t* m_lines, const uint32_t* sel0, const RankCacheMixed*, size_t Rank0)
noexcept {
    size_t lo = select0_upper_bound_line<dimensions>(m_lines, sel0, Rank0);
    assert(Rank0 < LineBits * lo - m_lines[lo].mixed[dimensions].base);
    const auto& xx = m_lines[lo - 1].mixed[dimensions];
    size_t hit = LineBits * (lo - 1) - xx.base;
    size_t index = (lo-1) * LineBits; // base bit index

  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    __m128i arr1 = _mm_set_epi32(64 * 3, 64 * 2, 64 * 1, 0);
    __m128i arr2 = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*(uint32_t*)xx.rlev));
    __m128i arr = _mm_sub_epi32(arr1, arr2); // rlev[0] is always 0
    __m128i key = _mm_set1_epi32(uint32_t(Rank0 - hit));
    __mmask8 cmp = _mm_cmpge_epi32_mask(arr, key);
    auto tz = _tzcnt_u32(cmp);
    TERARK_ASSERT_LT(tz, 4);
    return index + 64 * tz + UintSelect1(~xx.bit64[tz], Rank0 - (hit + 64 * tz - xx.rlev[tz]));
  #else
    if (Rank0 < hit + 64*2 - xx.rlev[2]) {
        if (Rank0 < hit + 64*1 - xx.rlev[1]) { // xx.rlev[0] is always 0
            return index + 64*0 + UintSelect1(~xx.bit64[0], Rank0 - hit);
        }
        return index + 64*1 + UintSelect1(
                ~xx.bit64[1], Rank0 - (hit + 64*1 - xx.rlev[1]));
    }
    if (Rank0 < hit + 64*3 - xx.rlev[3]) {
        return index + 64*2 + UintSelect1(
                ~xx.bit64[2], Rank0 - (hit + 64*2 - xx.rlev[2]));
    }
    else {
        return index + 64*3 + UintSelect1(
                ~xx.bit64[3], Rank0 - (hit + 64*3 - xx.rlev[3]));
    }
  #endif
}

template<size_t dimensions>
inline size_t rank_select_mixed_il_256::select1_upper_bound_line
(const bldata_t* m_lines, const uint32_t* sel1, size_t Rank1) noexcept {
    size_t lo = sel1[Rank1 / LineBits];
    size_t hi = sel1[Rank1 / LineBits + 1];
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        size_t mid_val = m_lines[mid].mixed[dimensions].base;
        if (mid_val <= Rank1) // upper_bound
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}
template<size_t dimensions>
inline size_t rank_select_mixed_il_256::fast_select1_dx
(const bldata_t* m_lines, const uint32_t* sel1, const RankCacheMixed*, size_t Rank1)
noexcept {
    size_t lo = select1_upper_bound_line<dimensions>(m_lines, sel1, Rank1);
    assert(Rank1 < m_lines[lo].mixed[dimensions].base);
    const auto& xx = m_lines[lo - 1].mixed[dimensions];
    size_t hit = xx.base;
    assert(Rank1 >= hit);
    size_t index = (lo-1) * LineBits; // base bit index
  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    __m128i arr = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*(uint32_t*)xx.rlev));
    __m128i key = _mm_set1_epi32(uint32_t(Rank1 - hit));
    __mmask8 cmp = _mm_cmpge_epi32_mask(arr, key);
    auto tz = _tzcnt_u32(cmp);
    TERARK_ASSERT_LT(tz, 4);
    return index + 64 * tz + UintSelect1(xx.bit64[tz], Rank1 - (hit + xx.rlev[tz]));
  #else
    if (Rank1 < hit + xx.rlev[2]) {
        if (Rank1 < hit + xx.rlev[1]) { // xx.rlev[0] is always 0
            return index + UintSelect1(xx.bit64[0], Rank1 - hit);
        }
        return index + 64*1 + UintSelect1(
                 xx.bit64[1], Rank1 - (hit + xx.rlev[1]));
    }
    if (Rank1 < hit + xx.rlev[3]) {
        return index + 64*2 + UintSelect1(
                 xx.bit64[2], Rank1 - (hit + xx.rlev[2]));
    }
    else {
        return index + 64*3 + UintSelect1(
                 xx.bit64[3], Rank1 - (hit + xx.rlev[3]));
    }
  #endif
}

TERARK_NAME_TYPE(rank_select_mixed_il_256_0, rank_select_mixed_dimensions<rank_select_mixed_il_256, 0>);
TERARK_NAME_TYPE(rank_select_mixed_il_256_1, rank_select_mixed_dimensions<rank_select_mixed_il_256, 1>);

} // namespace terark
