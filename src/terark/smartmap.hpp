#pragma once

#include <map>
#include <type_traits>

namespace terark {

template<class Key, class Value, int InlineCap,
         class BigMap = std::map<Key, Value> >
class SmartMap {
public:
    static_assert(InlineCap > 0);
    using key_type = const Key;
    using value_type = std::pair<const Key, Value>;
    using mapped_type = Value;

    ~SmartMap() {
        if (m_big_cnt) {
            m_big_map.~BigMap();
        }
        if (!std::is_trivially_destructible<value_type>::value) {
            for (int i = m_inline_cnt; i > 0; ) {
                m_inline_kv[--i].~value_type();
            }
        }
    }

    SmartMap() { }

    Value& operator[](const Key& key) {
        for (int i = 0; i < m_inline_cnt; i++) {
            if (key == m_inline_kv[i].first)
                return m_inline_kv[i].second;
        }
        if (m_inline_cnt < InlineCap) {
            new  (&m_inline_kv[m_inline_cnt]) value_type(key, Value());
            return m_inline_kv[m_inline_cnt++].second;
        }
        if (0 == m_big_cnt) {
            new(&m_big_map) BigMap();
        }
        Value& ret = m_big_map[key];
        m_big_cnt = (int)m_big_map.size();
        return ret;
    }

    template<class OnKeyValuePair>
    void for_each(OnKeyValuePair fn) {
        for (int i = 0; i < m_inline_cnt; i++) {
            fn(m_inline_kv[i]);
        }
        if (m_big_cnt) {
            for (auto& kv : m_big_map) {
                fn(kv);
            }
        }
    }

    bool  empty() const { return 0 == m_inline_cnt; }
    size_t size() const { return m_inline_cnt + m_big_cnt; }

private:
    int m_inline_cnt = 0;
    int m_big_cnt = 0;
    union {
        value_type m_inline_kv[InlineCap];
    };
    union {
        BigMap m_big_map;
    };
};

} // namespace terark
