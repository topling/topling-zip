#pragma once
#include <type_traits>
#include <limits>
#include <new>
#include <terark/node_layout.hpp>
#include <terark/valvec.hpp>

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
  using vec_t = valvec<Holder>;
  using value_type = std::pair<const Key, Value>;
  using key_type = Key;
  using mapped_type = Value;
  static_assert(sizeof  (Holder) ==  sizeof(value_type));
  static_assert(alignof (Holder) == alignof(value_type));
  static_assert(offsetof(Holder, value) == offsetof(value_type, second));

  static       value_type* AsKV(      void* mem) terark_nonnull terark_returns_nonnull { return reinterpret_cast<      value_type*>(mem); }
  static const value_type* AsKV(const void* mem) terark_nonnull terark_returns_nonnull { return reinterpret_cast<const value_type*>(mem); }
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
  ~VectorIndexMap() { clear(); }

  iterator begin() noexcept { return {&m_vec, true}; }
  iterator end  () noexcept { return {&m_vec}; }

  const_iterator begin() const noexcept { return {&m_vec, true}; }
  const_iterator end  () const noexcept { return {&m_vec}; }

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  reverse_iterator rbegin() noexcept { return {end  ()}; }
  reverse_iterator rend  () noexcept { return {begin()}; }
  const_reverse_iterator rbegin() const noexcept { return {end  ()}; }
  const_reverse_iterator rend  () const noexcept { return {begin()}; }

  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator cend  () const noexcept { return end  (); }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  const_reverse_iterator crend  () const noexcept { return rend  (); }

  const Value* get_value_ptr(Key key) const noexcept {
    if (key < m_vec.size() && m_vec[key].key != nil) {
      TERARK_ASSERT_EQ(m_vec[key].key, key);
      return &AsKV(&m_vec[key])->second;
    }
    return nullptr;
  }
  Value* get_value_ptr(Key key) noexcept {
    if (key < m_vec.size() && m_vec[key].key != nil) {
      TERARK_ASSERT_EQ(m_vec[key].key, key);
      return &AsKV(&m_vec[key])->second;
    }
    return nullptr;
  }
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
    TERARK_ASSERT_NE(key, nil);
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
    TERARK_ASSERT_NE(kv.first, nil);
    return emplace(kv.first, kv.second);
  }
  std::pair<iterator, bool> insert(value_type&& kv) {
    TERARK_ASSERT_NE(kv.first, nil);
    return emplace(kv.first, std::move(kv.second));
  }
  template<class KV2>
  std::pair<iterator, bool> emplace(const KV2& kv) {
    const Key key = kv.first;
    TERARK_ASSERT_NE(key, nil);
    bool ok = do_lazy_insert_i(key, CopyConsFunc<Value>(kv.second));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }
  template<class KV2>
  std::pair<iterator, bool> emplace(KV2&& kv) {
    const Key key = kv.first;
    TERARK_ASSERT_NE(key, nil);
    bool ok = do_lazy_insert_i(key, MoveConsFunc<Value>(std::move(kv.second)));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }
  template<class V2>
  std::pair<iterator, bool> emplace(const Key key, const V2& v) {
    TERARK_ASSERT_NE(key, nil);
    bool ok = do_lazy_insert_i(key, CopyConsFunc<Value>(v));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }
  template<class V2>
  std::pair<iterator, bool> emplace(const Key key, V2&& v) {
    TERARK_ASSERT_NE(key, nil);
    bool ok = do_lazy_insert_i(key, MoveConsFunc<Value>(std::move(v)));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }

  template<class ValueCons>
  std::pair<size_t, bool> lazy_insert_i(const Key key, ValueCons cons) {
    TERARK_ASSERT_NE(key, nil);
    bool ok = do_lazy_insert_i(key, std::move(cons));
    return std::make_pair(iterator(&m_vec, m_vec.begin() + key), ok);
  }
protected:
  template<class ValueCons>
  bool do_lazy_insert_i(const Key key, ValueCons cons) {
    TERARK_ASSERT_NE(key, nil);
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
    TERARK_ASSERT_GT(m_delcnt, 0);
    m_delcnt--; // used the 'key' slot
    return true;
  }
public:

  size_t erase(const Key key) noexcept {
    TERARK_ASSERT_NE(key, nil);
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
  void clear() noexcept {
    if (!std::is_trivially_destructible<Value>::value) {
      size_t num = m_vec.size();
      size_t live_cnt = 0;
      for (size_t idx = 0; idx < num; idx++) {
        if (m_vec[idx].key != nil) {
          TERARK_ASSERT_EQ(m_vec[idx].key, idx);
          AsKV(&m_vec[idx])->second.~Value();
          live_cnt++;
        }
      }
      TERARK_VERIFY_EQ(live_cnt + m_delcnt, num);
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
  void do_erase(Key key) noexcept {
    TERARK_ASSERT_NE(key, nil);
    m_vec[key].key = nil;
    AsKV(&m_vec[key])->second.~Value();
    TERARK_IF_DEBUG(memset(m_vec[key].value, 0xCC, sizeof(Value)),);
    m_delcnt++;
    while (m_vec.back().key == nil) {
      m_vec.pop_back();
      m_delcnt--;
      if (m_vec.empty()) break;
    }
  }
  void grow_to_idx(size_t key) {
    TERARK_ASSERT_NE(key, nil);
    size_t oldsize = m_vec.size();
    size_t newsize = key + 1;
    m_vec.resize(newsize);
    m_delcnt += newsize - oldsize;
  }
  vec_t m_vec;
  size_t m_delcnt = 0;
};

// VectorPtrMap can not discriminate 'not exists' and 'null', this is subtle
// in many situations, and is error-prone!
#if 0 // disable the code and keep it not deleted for caution!

template<class T>
bool VectorPtrMap_IsNull(const T* p) { return nullptr == p; }
template<class Ptr>
auto VectorPtrMap_IsNull(const Ptr& x) -> decltype(x.empty()) {
  return x.empty();
}
template<class Ptr>
auto VectorPtrMap_IsNull(const Ptr& x) -> decltype(x == nullptr) {
  return x == nullptr;
}
template<class T>
void VectorPtrMap_Reset(T*& p) { p = nullptr; } // not own raw ptr
template<class Ptr>
auto VectorPtrMap_Reset(Ptr& x) -> decltype(x.reset()) { x.reset(); }
template<class Ptr>
auto VectorPtrMap_Reset(Ptr& x) -> decltype(x.clear()) { x.clear(); }

template<class Key, class Ptr>
class VectorPtrMap {
  static_assert(std::is_unsigned<Key>::value);
  static_assert(sizeof(Key) <= sizeof(size_t));

public:
  using vec_t = valvec<Ptr>;
  using key_type = Key;
  using mapped_type = Ptr;
  typedef std::pair<const Key, Ptr> value_type;
  class iterator {
   #define IterClass iterator
    using PVec = vec_t*;
    using QPtr = Ptr; // Qualified Ptr
    using QIter = typename vec_t::iterator;
   #include "vec_ptr_map_iter.hpp"
   #undef IterClass
  };
  class const_iterator {
   #define IterClass const_iterator
    using PVec = const vec_t*;
    using QPtr = const Ptr; // Qualified Ptr
    using QIter = typename vec_t::const_iterator;
  public:
    const_iterator(iterator iter) : m_vec(iter.m_vec), m_key(iter.m_key) {}
   #include "vec_ptr_map_iter.hpp"
   #undef IterClass
  };

  explicit VectorPtrMap(size_t cap = 8) {
    m_vec.reserve(cap);
  }
  ~VectorPtrMap() { clear(); }

  iterator begin() noexcept { return {&m_vec, true}; }
  iterator end  () noexcept { return {&m_vec}; }

  const_iterator begin() const noexcept { return {&m_vec, true}; }
  const_iterator end  () const noexcept { return {&m_vec}; }

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  reverse_iterator rbegin() noexcept { return {end  ()}; }
  reverse_iterator rend  () noexcept { return {begin()}; }
  const_reverse_iterator rbegin() const noexcept { return {end  ()}; }
  const_reverse_iterator rend  () const noexcept { return {begin()}; }

  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator cend  () const noexcept { return end  (); }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  const_reverse_iterator crend  () const noexcept { return rend  (); }

  const_iterator find(Key key) const noexcept {
    if (key < m_vec.size() && !VectorPtrMap_IsNull(m_vec[key])) {
      return const_iterator(&m_vec, key);
    }
    return const_iterator(&m_vec);
  }
  iterator find(Key key) noexcept {
    if (key < m_vec.size() && !VectorPtrMap_IsNull(m_vec[key])) {
      return iterator(&m_vec, key);
    }
    return iterator(&m_vec);
  }
  Ptr& operator[](const Key key) {
    do_lazy_insert_i(key, DefaultConsFunc<Ptr>());
    return m_vec[key];
  }
  const Ptr& at(const Key key) const {
    if (key < m_vec.size() && !VectorPtrMap_IsNull(m_vec[key])) {
      return m_vec[key];
    }
    throw std::out_of_range("key does not exists");
  }
  Ptr& at(const Key key) {
    if (key < m_vec.size() && !VectorPtrMap_IsNull(m_vec[key])) {
      return m_vec[key];
    }
    throw std::out_of_range("key does not exists");
  }
  std::pair<iterator, bool> insert(const std::pair<const Key, Ptr>& kv) {
    return emplace(kv.first, kv.second);
  }
  std::pair<iterator, bool> insert(const std::pair<const Key, Ptr>&& kv) {
    return emplace(kv.first, std::move(kv.second));
  }
  template<class KV2>
  std::pair<iterator, bool> emplace(const KV2& kv) {
    const Key key = kv.first;
    bool ok = do_lazy_insert_i(key, CopyConsFunc<Ptr>(kv.second));
    return std::make_pair(iterator(&m_vec, key), ok);
  }
  template<class KV2>
  std::pair<iterator, bool> emplace(KV2&& kv) {
    const Key key = kv.first;
    bool ok = do_lazy_insert_i(key, MoveConsFunc<Ptr>(std::move(kv.second)));
    return std::make_pair(iterator(&m_vec, key), ok);
  }
  template<class V2>
  std::pair<iterator, bool> emplace(const Key key, const V2& v) {
    bool ok = do_lazy_insert_i(key, CopyConsFunc<Ptr>(v));
    return std::make_pair(iterator(&m_vec, key), ok);
  }
  template<class V2>
  std::pair<iterator, bool> emplace(const Key key, V2&& v) {
    bool ok = do_lazy_insert_i(key, MoveConsFunc<Ptr>(std::move(v)));
    return std::make_pair(iterator(&m_vec, key), ok);
  }
  template<class Filler>
  std::pair<size_t, bool> lazy_insert_i(const Key key, Filler fill) {
    bool ok = do_lazy_insert_i(key, std::move(fill));
    return std::make_pair(iterator(&m_vec, key), ok);
  }

protected:
  template<class Filler>
  bool do_lazy_insert_i(const Key key, Filler fill) {
    if (key < m_vec.size()) {
      if (!VectorPtrMap_IsNull(m_vec[key])) {
        return false;
      }
    } else {
      grow_to_idx(key);
    }
    fill(&m_vec[key]);
    // operator[] will fill Ptr as null
    // TERARK_VERIFY_F(!VectorPtrMap_IsNull(m_vec[key]), "fill ptr must not null");
    TERARK_ASSERT_GT(m_delcnt, 0);
    m_delcnt--; // used the 'key' slot
    return true;
  }

public:
  size_t erase(const Key key) noexcept {
    if (key < m_vec.size() && !VectorPtrMap_IsNull(m_vec[key])) {
      do_erase(key);
      return 1;
    }
    return 0;
  }
  void erase(iterator iter) noexcept {
    assert(iter.m_key < m_vec.size());
    const Key key = Key(iter.m_key);
    do_erase(key);
  }
  void clear() noexcept {
    if (!std::is_trivially_destructible<Ptr>::value) {
      size_t num = m_vec.size();
      size_t live_cnt = 0;
      for (size_t idx = 0; idx < num; idx++) {
        if (!VectorPtrMap_IsNull(m_vec[idx])) {
          VectorPtrMap_Reset(m_vec[idx]);
          live_cnt++;
        }
      }
    }
    m_vec.clear();
    m_delcnt = 0;
  }
  void swap(VectorPtrMap& y) {
    m_vec.swap(y.m_vec);
    std::swap(m_delcnt, y.m_delcnt);
  }
  size_t size() const noexcept { return m_vec.size() - m_delcnt; }
  void reserve(size_t cap) { m_vec.reserve(cap); }

protected:
  void do_erase(Key key) noexcept {
    VectorPtrMap_Reset(m_vec[key]);
    m_delcnt++;
    /* DO NOT revoke extra elements
    while (VectorPtrMap_IsNull(m_vec.back())) {
      m_vec.pop_back();
      m_delcnt--;
      if (m_vec.empty()) break;
    }
    */
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
#endif

} // namespace terark

namespace std {

template<class Key, class Value>
void swap(terark::VectorIndexMap<Key, Value>& x,
          terark::VectorIndexMap<Key, Value>& y) {
  x.swap(y);
}

#if 0
template<class Key, class Value>
void swap(terark::VectorPtrMap<Key, Value>& x,
          terark::VectorPtrMap<Key, Value>& y) {
  x.swap(y);
}
#endif

} // namespace std