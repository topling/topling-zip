private:
  friend class VectorPtrMap;
  PVec m_vec;
  Key  m_key;
  IterClass(PVec vec, Key key) noexcept : m_vec(vec), m_key(key) {
    assert(key < vec->size());
  }
  IterClass(PVec vec, bool/*for_construct_begin*/) noexcept : m_vec(vec) {
    auto  data = vec->data();
    auto  size = vec->size();
    size_t idx = 0;
    while (idx < size && VectorPtrMap_IsNull(data[idx])) ++idx;
    m_key = idx;
  }
  // construct end iter
  IterClass(PVec vec) noexcept : m_vec(vec), m_key(vec->size()) {}
public:
  struct value_type {
    const Key first;
    QPtr& second;
    value_type(const Key k, QPtr& v) : first(k), second(v) {}
  };
  struct pointer : value_type {
    using value_type::value_type;
    value_type* operator->() const { return const_cast<pointer*>(this); }
  };
  using key_type = Key;
  using mapped_type = QPtr;
  using difference_type = ptrdiff_t;
  using size_type = size_t;
  using reference = value_type;
  using iterator_category = std::bidirectional_iterator_tag;

  IterClass() noexcept : m_vec(nullptr) {}
  IterClass& operator++() noexcept {
    auto data = m_vec->data();
    auto size = m_vec->size();
    assert(m_key < size);
    do ++m_key; while (m_key < size && VectorPtrMap_IsNull(data[m_key]));
    return *this;
  }
  IterClass operator++(int) noexcept {
    assert(m_key < m_vec->size());
    auto tmp = *this; ++*this; return tmp;
  }
  IterClass& operator--() noexcept {
    TERARK_ASSERT_GT(m_key, 0);
    auto data = m_vec->data();
    do --m_key; while (m_key > 0 && VectorPtrMap_IsNull(data[m_key]));
    return *this;
  }
  IterClass operator--(int) noexcept {
    TERARK_ASSERT_GT(m_key, 0);
    auto tmp = *this; --*this; return tmp;
  }
  pointer operator->() const noexcept {
    TERARK_ASSERT_LT(m_key, m_vec->size());
    return pointer(Key(m_key), (*m_vec)[m_key]);
  }
  reference operator*() const noexcept {
    TERARK_ASSERT_LT(m_key, m_vec->size());
    return reference(Key(m_key), (*m_vec)[m_key]);
  }
  friend bool operator==(IterClass x, IterClass y) noexcept {
    TERARK_ASSERT_EQ(x.m_vec, y.m_vec);
    return x.m_key == y.m_key;
  }
  friend bool operator!=(IterClass x, IterClass y) noexcept {
    TERARK_ASSERT_EQ(x.m_vec, y.m_vec);
    return x.m_key != y.m_key;
  }
