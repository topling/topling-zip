#pragma once

#include "valvec.hpp"

namespace terark {

//
// 直方图的 key 表示长度，高表示条目数量
// 即：X 轴的值表示 key， Y 轴的值表示相应 key 的数量
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
    size_t  m_cnt_sum;            ///< 相当于   f(x) 的积分，如果 x 为单条数据长度，y = f(x) 为相应长度的数据条数，该值即为总的数据条数
    size_t  m_total_key_len;      ///< 相当于 x*f(x) 的积分，如果 x 为单条数据长度，y = f(x) 为相应长度的数据条数，该值即为总的数据长度
    index_t m_min_key_len;        ///< 最左列的 key，忽略 y 为 0 的点，该值即为 y 非 0 的最小 x
    index_t m_max_key_len;        ///< 最右列的 key，忽略 y 为 0 的点，该值即为 y 非 0 的最大 x
    index_t m_min_cnt_key;        ///< 最低列的 key，即最小的 y 对应的 x，可能有多个这样的 x，取这些 x 中最小的那个
    index_t m_max_cnt_key;        ///< 最高列的 key，即最大的 y 对应的 x，可能有多个这样的 x，取这些 x 中最小的那个
    size_t  m_cnt_of_min_cnt_key; ///< 最低列的高度，即 y 的最小值
    size_t  m_cnt_of_max_cnt_key; ///< 最高列的高度，即 y 的最大值

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

