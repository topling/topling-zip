#pragma once
#include <type_traits>
#include <limits>
#include <new>
#include <vector>
#include <terark/node_layout.hpp>

namespace terark {

// Key is unsigned integer as index
template<class Key, class Value>
class VectorIndexMap {
  static_assert(std::is_unsigned<Key>::value);
  static constexpr Key nil = std::numeric_limits<Key>::max();
  struct Holder {
    Key key;
    alignas(Value) char value[sizeof(Value)];
    Holder() { key = nil; }
  };
public:
  // this pair.first is redundant with respect to most optimal principle,
  // just for compatible with generic map, and use nil of pair.first for
  // valid/invalid flag
  using vec_t = std::vector<Holder>;
  using value_type = std::pair<const Key, Value>;
  using key_type = Key;
  using mapped_type = Value;
  static_assert(sizeof  (Holder) ==  sizeof(value_type));
  static_assert(alignof (Holder) == alignof(value_type));
  static_assert(offsetof(Holder, value) == offsetof(value_type, second));

  static       value_type* AsKV(      void* mem) { return reinterpret_cast<      value_type*>(mem); }
  static const value_type* AsKV(const void* mem) { return reinterpret_cast<const value_type*>(mem); }
  class iterator {
   #define IterClass iterator
    using PVec = vec_t*;
    using QIter = typename vec_t::iterator;
    using QElem = value_type;
   #include "vec_idx_map_iter.hpp"
   #undef IterClass
  };
  class const_iterator {
   #define IterClass const_iterator
    using PVec = const vec_t*;
    using QIter = typename vec_t::const_iterator;
    using QElem = const value_type;
  public:
    const_iterator(iterator iter) : m_vec(iter.m_vec), m_iter(iter.m_iter) {}
   #include "vec_idx_map_iter.hpp"
   #undef IterClass
  };

  explicit VectorIndexMap(size_t cap = 8) {
    m_vec.reserve(cap);
  }

  iterator begin() noexcept { return {&m_vec, true}; }
  iterator end  () noexcept { return {&m_vec}; }

  const_iterator begin() const noexcept { return {&m_vec, true}; }
  const_iterator end  () const noexcept { return {&m_vec}; }

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  reverse_iterator rbegin() { return {end  ()}; }
  reverse_iterator rend  () { return {begin()}; }
  const_reverse_iterator rbegin() const { return {end  ()}; }
  const_reverse_iterator rend  () const { return {begin()}; }

  const_iterator cbegin() const { return begin(); }
  const_iterator cend  () const { return end  (); }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend  () const { return rend  (); }

  const_iterator find(Key key) const noexcept {
    if (key < m_vec.size() && m_vec[key].key != nil) {
      TERARK_ASSERT_EQ(m_vec[key].key, key);
      return const_iterator(&m_vec, m_vec.begin() + key);
    }
    return const_iterator(&m_vec);
  }
  iterator find(Key key) noexcept {
    if (key < m_vec.size() && m_vec[key].key != nil) {
      TERARK_ASSERT_EQ(m_vec[key].key, key);
      return iterator(&m_vec, m_vec.begin() + key);
    }
    return iterator(&m_vec);
  }
  Value& operator[](const Key key) {
    do_lazy_insert_i(key, DefaultConsFunc<Value>());
    return AsKV(&m_vec[key])->second;
  }
  const Value& at(const Key key) const {
    if (key < m_vec.size() && m_vec[key].key != nil) {
      TERARK_ASSERT_EQ(m_vec[key].key, key);
      return AsKV(&m_vec[key])->second;
    }
    throw std::out_of_range("key does not exists");
  }
  Value& at(const Key key) {
    if (key < m_vec.size() && m_vec[key].key != nil) {
      TERARK_ASSERT_EQ(m_vec[key].key, key);
      return AsKV(&m_vec[key])->second;
    }
    throw std::out_of_range("key does not exists");
  }
  std::pair<iterator, bool> insert(const value_type& kv) {
    return emplace(kv.first, kv.second);
  }
  std::pair<iterator, bool> insert(value_type&& kv) {
    return emplace(kv.first, std::move(kv.second));
  }
  template<class KV2>
  std::pair<iterator, bool> emplace(const KV2& kv) {
    const Key key = kv.first;
    bool ok = do_lazy_insert_i(key, CopyConsFunc<Value>(kv.second));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }
  template<class KV2>
  std::pair<iterator, bool> emplace(KV2&& kv) {
    const Key key = kv.first;
    bool ok = do_lazy_insert_i(key, MoveConsFunc<Value>(std::move(kv.second)));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }
  template<class V2>
  std::pair<iterator, bool> emplace(const Key key, const V2& v) {
    bool ok = do_lazy_insert_i(key, CopyConsFunc(v));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }
  template<class V2>
  std::pair<iterator, bool> emplace(const Key key, V2&& v) {
    bool ok = do_lazy_insert_i(key, MoveConsFunc(std::move(v)));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }

  template<class ValueCons>
  std::pair<size_t, bool> lazy_insert_i(const Key key, ValueCons cons) {
    bool ok = do_lazy_insert_i(key, std::move(cons));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }
protected:
  template<class ValueCons>
  bool do_lazy_insert_i(const Key key, ValueCons cons) {
    if (key < m_vec.size()) {
      if (m_vec[key].key != nil) {
        TERARK_ASSERT_EQ(m_vec[key].key, key);
        return false;
      }
    } else {
      grow_to_idx(key);
    }
    cons(m_vec[key].value);
    m_vec[key].key = key; // assign after cons for exception safe
    m_delcnt--; // used the 'key' slot
    return true;
  }
public:

  size_t erase(const Key key) noexcept {
    if (key < m_vec.size() && nil != m_vec[key].key) {
      TERARK_ASSERT_EQ(key, m_vec[key].key);
      do_erase(key);
      return 1;
    }
    return 0;
  }
  void erase(iterator iter) noexcept {
    assert(iter.m_iter >= m_vec.begin());
    assert(iter.m_iter < m_vec.end());
    const Key key = Key(iter.m_iter - m_vec.begin());
    TERARK_ASSERT_EQ(m_vec[key].key, key);
    do_erase(key);
  }
  void clear() {
    size_t num = m_vec.size();
    if (!std::is_trivially_destructible<Value>::value) {
      for (size_t idx = 0; idx < num; idx++) {
        if (m_vec[idx].key != nil) {
          TERARK_ASSERT_EQ(m_vec[idx].key, idx);
          AsKV(&m_vec[idx])->second.~Value();
        }
      }
    }
    m_vec.clear();
    m_delcnt = 0;
  }
  void swap(VectorIndexMap& y) {
    m_vec.swap(y.m_vec);
    std::swap(m_delcnt, y.m_delcnt);
  }
  size_t size() const noexcept { return m_vec.size() - m_delcnt; }
  void reserve(size_t cap) { m_vec.reserve(cap); }

protected:
  void do_erase(Key key) {
    m_vec[key].key = nil;
    AsKV(&m_vec[key])->second.~Value();
    m_delcnt++;
    while (m_vec.back().key == nil) {
      m_vec.pop_back();
      m_delcnt--;
      if (m_vec.empty()) break;
    }
  }
  void grow_to_idx(size_t key) {
    size_t oldsize = m_vec.size();
    size_t newsize = key + 1;
    m_vec.resize(newsize);
    m_delcnt += newsize - oldsize;
  }
  vec_t m_vec;
  size_t m_delcnt = 0;
};

} // namespace terark

namespace std {

template<class Key, class Value>
void swap(terark::VectorIndexMap<Key, Value>& x,
          terark::VectorIndexMap<Key, Value>& y) {
  x.swap(y);
}

} // namespace std