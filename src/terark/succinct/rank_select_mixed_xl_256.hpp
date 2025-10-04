#pragma once

#include "rank_select_basic.hpp"
#include "rank_select_mixed_basic.hpp"

namespace terark {

template<size_t Arity>
class TERARK_DLL_EXPORT rank_select_mixed_xl_256 : public RankSelectConstants<256> {
public:
    typedef boost::mpl::true_ is_mixed;
    typedef uint32_t index_t;
    rank_select_mixed_xl_256();
    rank_select_mixed_xl_256(size_t n, bool val = false);
    rank_select_mixed_xl_256(size_t n, valvec_no_init);
    rank_select_mixed_xl_256(size_t n, valvec_reserve);
    rank_select_mixed_xl_256(const rank_select_mixed_xl_256&);
    rank_select_mixed_xl_256& operator=(const rank_select_mixed_xl_256&);
    rank_select_mixed_xl_256(rank_select_mixed_xl_256&& y) noexcept;
    rank_select_mixed_xl_256& operator=(rank_select_mixed_xl_256&& y) noexcept;

    ~rank_select_mixed_xl_256();
    void clear() noexcept;
    void risk_release_ownership() noexcept;
    void risk_mmap_from(unsigned char* base, size_t length);
    void shrink_to_fit() noexcept;

    void swap(rank_select_mixed_xl_256&) noexcept;
    const void* data() const { return m_lines; }
    size_t mem_size() const { return m_capacity; }

protected:
    struct RankCacheMixed {
        union {
            uint64_t  bit64[LineWords * Arity * sizeof(bm_uint_t) / 8];
            bm_uint_t words[LineWords * Arity];
        };
        struct {
            uint32_t      base;
            unsigned char rlev[4];
        } mixed[Arity];
        template<size_t dimensions> size_t get_base() const { return mixed[dimensions].base; }
    };
    typedef RankCacheMixed bldata_t;

    static size_t fix_resize_size(size_t bits) {
        rank_select_check_overflow(bits, > , rank_select_mixed_xl_256);
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
    void set_word_dx(size_t word_idx, bm_uint_t bits) {
        assert(word_idx < num_words_dx<dimensions>());
        m_lines[word_idx / LineWords].words[word_idx % LineWords * Arity + dimensions] = bits;
    }
    template<size_t dimensions>
    bm_uint_t get_word_dx(size_t word_idx) const {
        assert(word_idx < num_words_dx<dimensions>());
        return m_lines[word_idx / LineWords].words[word_idx % LineWords * Arity + dimensions];
    }
    template<size_t dimensions>
    size_t num_words_dx() const { return (m_size[dimensions] + WordBits - 1) / WordBits; }

    template<size_t dimensions>
    void push_back_dx(bool val) noexcept {
        rank_select_check_overflow(m_size[dimensions], >= , rank_select_mixed_xl_256);
        assert(m_size[dimensions] <= m_capacity / sizeof(RankCacheMixed) * LineBits);
        if (terark_unlikely(m_size[dimensions] == m_capacity / sizeof(RankCacheMixed) * LineBits))
            grow();
        size_t i = m_size[dimensions]++;
        val ? set1_dx<dimensions>(i) : set0_dx<dimensions>(i);
    }
    template<size_t dimensions>
    bool is0_dx(size_t i) const {
        assert(i < m_size[dimensions]);
        return !terark_bit_test(&m_lines[i / LineBits].words[i % LineBits / WordBits * Arity + dimensions], i % WordBits);
    }
    template<size_t dimensions>
    bool is1_dx(size_t i) const {
        assert(i < m_size[dimensions]);
        return terark_bit_test(&m_lines[i / LineBits].words[i % LineBits / WordBits * Arity + dimensions], i % WordBits);
    }
    template<size_t dimensions>
    void set0_dx(size_t i) {
        assert(i < m_size[dimensions]);
        terark_bit_set0(&m_lines[i / LineBits].words[i % LineBits / WordBits * Arity + dimensions], i % WordBits);
    }
    template<size_t dimensions>
    void set1_dx(size_t i) {
        assert(i < m_size[dimensions]);
        terark_bit_set1(&m_lines[i / LineBits].words[i % LineBits / WordBits * Arity + dimensions], i % WordBits);
    }
    template<size_t dimensions> void build_cache_dx(bool speed_select0, bool speed_select1) {
        build_cache_impl(speed_select0, speed_select1, dimensions,
            &rank_select_mixed_xl_256<Arity>::bits_range_set0_dx<dimensions>);
    }
    void build_cache_impl(bool speed_select0, bool speed_select1, size_t dimensions,
        void (rank_select_mixed_xl_256<Arity>::*bits_range_set0)(size_t i, size_t k));
    template<size_t dimensions> size_t one_seq_len_dx(size_t bitpos) const noexcept;
    template<size_t dimensions> size_t zero_seq_len_dx(size_t bitpos) const noexcept;
    template<size_t dimensions> size_t one_seq_revlen_dx(size_t endpos) const noexcept;
    template<size_t dimensions> size_t zero_seq_revlen_dx(size_t endpos) const noexcept;
    template<size_t dimensions> inline size_t rank0_dx(size_t bitpos) const noexcept;
    template<size_t dimensions> inline size_t rank1_dx(size_t bitpos) const noexcept;
    template<size_t dimensions> size_t select0_dx(size_t id) const noexcept;
    template<size_t dimensions> size_t select1_dx(size_t id) const noexcept;

public:
    template<size_t dimensions>
    rank_select_mixed_dimensions<rank_select_mixed_xl_256, dimensions>& get() {
        static_assert(dimensions < Arity, "dimensions must less than Arity !");
        return *reinterpret_cast<rank_select_mixed_dimensions<rank_select_mixed_xl_256, dimensions>*>(this);
    }
    template<size_t dimensions>
    const rank_select_mixed_dimensions<rank_select_mixed_xl_256, dimensions>& get() const {
        static_assert(dimensions < Arity, "dimensions must less than Arity !");
        return *reinterpret_cast<const rank_select_mixed_dimensions<rank_select_mixed_xl_256, dimensions>*>(this);
    }

protected:
    RankCacheMixed* m_lines;
    size_t m_size[Arity];
    size_t m_capacity;  // bytes;
    union
    {
        // m_flags must be member of this
        // for d<0> and d<1> build cache
        // d stands for Deg()
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
            uint64_t has_d2_rank_cache : 1;
            uint64_t has_d2_sel0_cache : 1;
            uint64_t has_d2_sel1_cache : 1;
            uint64_t has_d3_rank_cache : 1;
            uint64_t has_d3_sel0_cache : 1;
            uint64_t has_d3_sel1_cache : 1;
        } m_flags_debug;
    };
    uint32_t*  m_sel0_cache[Arity];
    uint32_t*  m_sel1_cache[Arity];
    size_t     m_max_rank0[Arity];
    size_t     m_max_rank1[Arity];

    void instatiate_member_template();
    template<size_t Index, size_t MyArity, class MyClass>
    friend void instantiate_member_template_aux(MyClass*);

    const RankCacheMixed* get_rank_cache_base() const { return m_lines; }

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
      { fast_prefetch_bit_dx<dimensions>(m_lines, i); }
    template<size_t dimensions>
    static void fast_prefetch_bit_dx(const bldata_t* m_lines, size_t i) noexcept
      { _mm_prefetch((const char*)&m_lines[i / LineBits].words[i % LineBits / WordBits * Arity + dimensions], _MM_HINT_T0);
        // _mm_prefetch((const char*)&m_lines[i / LineBits].mixed[dimensions], _MM_HINT_T0);
        // _mm_prefetch(((const char*)&m_lines[i / LineBits]), _MM_HINT_T0);
        // _mm_prefetch(((const char*)&m_lines[i / LineBits]) + sizeof(bldata_t) - 1, _MM_HINT_T0);
      }

    template<size_t dimensions>
    void prefetch_rank1_dx(size_t /*bitpos*/) const noexcept
      { /*_mm_prefetch((const char*)&m_lines[bitpos / LineBits].mixed[dimensions].rlev, _MM_HINT_T0);*/ }
    template<size_t dimensions>
    static void fast_prefetch_rank1_dx(const RankCacheMixed* /*rankCache*/, size_t /*bitpos*/) noexcept
      { /*_mm_prefetch((const char*)&rankCache[bitpos / LineBits].mixed[dimensions].rlev, _MM_HINT_T0);*/ }
};

template<size_t Arity>
template<size_t dimensions>
inline size_t rank_select_mixed_xl_256<Arity>::
rank0_dx(size_t bitpos) const noexcept {
    assert(bitpos <= m_size[dimensions]);
    return bitpos - rank1_dx<dimensions>(bitpos);
}

template<size_t Arity>
template<size_t dimensions>
inline size_t rank_select_mixed_xl_256<Arity>::
rank1_dx(size_t bitpos) const noexcept {
    assert(m_flags & (1 << (1 + 3 * dimensions)));
    assert(bitpos <= m_size[dimensions]);
    const auto& line = m_lines[bitpos / LineBits];
    return line.mixed[dimensions].base + line.mixed[dimensions].rlev[bitpos % LineBits / 64]
        + fast_popcount_trail(line.bit64[bitpos % LineBits / 64 * Arity + dimensions], bitpos % 64);
}

template<size_t Arity>
template<size_t dimensions>
inline bool rank_select_mixed_xl_256<Arity>::
fast_is0_dx(const bldata_t* m_lines, size_t i) noexcept {
    return !terark_bit_test(&m_lines[i / LineBits].words[i % LineBits / WordBits * Arity + dimensions], i % WordBits);
}

template<size_t Arity>
template<size_t dimensions>
inline bool rank_select_mixed_xl_256<Arity>::
fast_is1_dx(const bldata_t* m_lines, size_t i) noexcept {
    return terark_bit_test(&m_lines[i / LineBits].words[i % LineBits / WordBits * Arity + dimensions], i % WordBits);
}

template<size_t Arity>
template<size_t dimensions>
inline size_t rank_select_mixed_xl_256<Arity>::
fast_rank0_dx(const bldata_t* m_lines, const RankCacheMixed* rankCache, size_t bitpos) noexcept {
    return bitpos - fast_rank1_dx<dimensions>(m_lines, rankCache, bitpos);
}

template<size_t Arity>
template<size_t dimensions>
inline size_t rank_select_mixed_xl_256<Arity>::
fast_rank1_dx(const bldata_t* m_lines, const RankCacheMixed*, size_t bitpos) noexcept {
    const auto& line = m_lines[bitpos / LineBits];
    return line.mixed[dimensions].base + line.mixed[dimensions].rlev[bitpos % LineBits / 64]
        + fast_popcount_trail(line.bit64[bitpos % LineBits / 64 * Arity + dimensions], bitpos % 64);
}

template<size_t Arity>
template<size_t dimensions>
inline size_t rank_select_mixed_xl_256<Arity>::select0_upper_bound_line
(const bldata_t* m_lines, const uint32_t* sel0, size_t Rank0) noexcept {
    size_t lo, hi;
    lo = sel0[Rank0 / LineBits];
    hi = sel0[Rank0 / LineBits + 1];
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
template<size_t Arity>
template<size_t dimensions>
inline size_t rank_select_mixed_xl_256<Arity>::fast_select0_dx
(const bldata_t* m_lines, const uint32_t* sel0, const RankCacheMixed*, size_t Rank0)
noexcept {
    size_t lo = select0_upper_bound_line<dimensions>(m_lines, sel0, Rank0);
    assert(Rank0 < LineBits * lo - m_lines[lo].mixed[dimensions].base);
    const auto& xx = m_lines[lo - 1];
    size_t hit = LineBits * (lo - 1) - xx.mixed[dimensions].base;
    size_t index = (lo - 1) * LineBits; // base bit index

  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    __m128i arr1 = _mm_set_epi32(64 * 3, 64 * 2, 64 * 1, 0);
    __m128i arr2 = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*(uint32_t*)xx.mixed[dimensions].rlev));
    __m128i arr = _mm_sub_epi32(arr1, arr2); // rlev[0] is always 0
    __m128i key = _mm_set1_epi32(uint32_t(Rank0 - hit));
    __mmask8 cmp = _mm_cmpge_epi32_mask(arr, key);
    auto tz = _tzcnt_u32(cmp);
    TERARK_ASSERT_LT(tz, 4);
    return index + 64 * tz + UintSelect1(
        ~xx.bit64[tz * Arity + dimensions], Rank0 - (hit + 64 * tz - xx.mixed[dimensions].rlev[tz]));
  #else
    if (Rank0 < hit + 64 * 2 - xx.mixed[dimensions].rlev[2]) {
        if (Rank0 < hit + 64 * 1 - xx.mixed[dimensions].rlev[1]) { // xx.rlev[0] is always 0
            return index + 64 * 0 + UintSelect1(~xx.bit64[dimensions], Rank0 - hit);
        }
        return index + 64 * 1 + UintSelect1(
            ~xx.bit64[1 * Arity + dimensions], Rank0 - (hit + 64 * 1 - xx.mixed[dimensions].rlev[1]));
    }
    if (Rank0 < hit + 64 * 3 - xx.mixed[dimensions].rlev[3]) {
        return index + 64 * 2 + UintSelect1(
            ~xx.bit64[2 * Arity + dimensions], Rank0 - (hit + 64 * 2 - xx.mixed[dimensions].rlev[2]));
    }
    else {
        return index + 64 * 3 + UintSelect1(
            ~xx.bit64[3 * Arity + dimensions], Rank0 - (hit + 64 * 3 - xx.mixed[dimensions].rlev[3]));
    }
  #endif
}

template<size_t Arity>
template<size_t dimensions>
inline size_t rank_select_mixed_xl_256<Arity>::select1_upper_bound_line
(const bldata_t* m_lines, const uint32_t* sel1, size_t Rank1) noexcept {
    size_t lo, hi;
    lo = sel1[Rank1 / LineBits];
    hi = sel1[Rank1 / LineBits + 1];
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
template<size_t Arity>
template<size_t dimensions>
inline size_t rank_select_mixed_xl_256<Arity>::fast_select1_dx
(const bldata_t* m_lines, const uint32_t* sel1, const RankCacheMixed*, size_t Rank1)
noexcept {
    size_t lo = select1_upper_bound_line<dimensions>(m_lines, sel1, Rank1);
    assert(Rank1 < m_lines[lo].mixed[dimensions].base);
    const auto& xx = m_lines[lo - 1];
    size_t hit = xx.mixed[dimensions].base;
    assert(Rank1 >= hit);
    size_t index = (lo - 1) * LineBits; // base bit index
  #if defined(__AVX512VL__) && defined(__AVX512BW__)
    __m128i arr = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*(uint32_t*)xx.mixed[dimensions].rlev));
    __m128i key = _mm_set1_epi32(uint32_t(Rank1 - hit));
    __mmask8 cmp = _mm_cmpge_epi32_mask(arr, key);
    auto tz = _tzcnt_u32(cmp);
    TERARK_ASSERT_LT(tz, 4);
    return index + 64 * tz + UintSelect1(
        xx.bit64[tz * Arity + dimensions], Rank1 - (hit + xx.mixed[dimensions].rlev[tz]));
  #else
    if (Rank1 < hit + xx.mixed[dimensions].rlev[2]) {
        if (Rank1 < hit + xx.mixed[dimensions].rlev[1]) { // xx.rlev[0] is always 0
            return index + UintSelect1(xx.bit64[dimensions], Rank1 - hit);
        }
        return index + 64 * 1 + UintSelect1(
            xx.bit64[1 * Arity + dimensions], Rank1 - (hit + xx.mixed[dimensions].rlev[1]));
    }
    if (Rank1 < hit + xx.mixed[dimensions].rlev[3]) {
        return index + 64 * 2 + UintSelect1(
            xx.bit64[2 * Arity + dimensions], Rank1 - (hit + xx.mixed[dimensions].rlev[2]));
    }
    else {
        return index + 64 * 3 + UintSelect1(
            xx.bit64[3 * Arity + dimensions], Rank1 - (hit + xx.mixed[dimensions].rlev[3]));
    }
  #endif
}

TERARK_NAME_TYPE(rank_select_mixed_xl_256_2_0, rank_select_mixed_dimensions<rank_select_mixed_xl_256<2>, 0>);
TERARK_NAME_TYPE(rank_select_mixed_xl_256_2_1, rank_select_mixed_dimensions<rank_select_mixed_xl_256<2>, 1>);

typedef rank_select_mixed_xl_256_2_0 rank_select_mixed_xl_256_0; // alias to old name
typedef rank_select_mixed_xl_256_2_1 rank_select_mixed_xl_256_1; // alias to old name

TERARK_NAME_TYPE(rank_select_mixed_xl_256_3_0, rank_select_mixed_dimensions<rank_select_mixed_xl_256<3>, 0>);
TERARK_NAME_TYPE(rank_select_mixed_xl_256_3_1, rank_select_mixed_dimensions<rank_select_mixed_xl_256<3>, 1>);
TERARK_NAME_TYPE(rank_select_mixed_xl_256_3_2, rank_select_mixed_dimensions<rank_select_mixed_xl_256<3>, 2>);

TERARK_NAME_TYPE(rank_select_mixed_xl_256_4_0, rank_select_mixed_dimensions<rank_select_mixed_xl_256<4>, 0>);
TERARK_NAME_TYPE(rank_select_mixed_xl_256_4_1, rank_select_mixed_dimensions<rank_select_mixed_xl_256<4>, 1>);
TERARK_NAME_TYPE(rank_select_mixed_xl_256_4_2, rank_select_mixed_dimensions<rank_select_mixed_xl_256<4>, 2>);
TERARK_NAME_TYPE(rank_select_mixed_xl_256_4_3, rank_select_mixed_dimensions<rank_select_mixed_xl_256<4>, 3>);

} // namespace terark
