#pragma once

#include "valvec.hpp"

namespace terark {

//
// 直方图的值表示长度，高表示条目数量
//
template<class index_t>
class TERARK_DLL_EXPORT Histogram {
    terark::valvec<index_t> m_small_cnt;
    terark::valvec<std::pair<index_t, index_t> > m_large_cnt_compact;
    struct LargeKeyCntMap;
    std::unique_ptr<LargeKeyCntMap> m_large_cnt;
    index_t& GetLargeKeyCnt(size_t key);
    size_t m_max_small_value;
    Histogram(const Histogram&) = delete;
    Histogram& operator=(const Histogram&) = delete;

public:
    size_t  m_distinct_key_cnt;   ///< 列数，不同 key 的数量
    size_t  m_cnt_sum;            ///< 所有列高度之和
    size_t  m_total_key_len;      ///< 总面积
    index_t m_min_key_len;        ///< 最左列的值
    index_t m_max_key_len;        ///< 最右列的值
    index_t m_min_cnt_key;        ///< 最低列的值
    index_t m_max_cnt_key;        ///< 最高列的值
    size_t  m_cnt_of_min_cnt_key; ///< 最低列的高度
    size_t  m_cnt_of_max_cnt_key; ///< 最高列的高度

    ~Histogram();
    Histogram(size_t max_small_value);
    Histogram();
    index_t& operator[](size_t key) {
        if (key < m_max_small_value)
            return m_small_cnt[key];
        else
            return GetLargeKeyCnt(key);
    }
    void finish();

    template<class OP>
    void for_each(OP op) const {
        const index_t* pCnt = m_small_cnt.data();
        for (size_t key = 0, maxKey = m_small_cnt.size(); key < maxKey; ++key) {
            if (pCnt[key]) {
                op(index_t(key), pCnt[key]);
            }
        }
        for (auto kv : m_large_cnt_compact) {
            op(kv.first, kv.second);
        }
    }
};

typedef Histogram<uint32_t> Uint32Histogram;
typedef Histogram<uint64_t> Uint64Histogram;

} // namespace terark

