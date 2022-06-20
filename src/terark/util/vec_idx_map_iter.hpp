private:
  friend class VecorIndexMap;
  PVec  m_vec;
  QIter m_iter;
  IterClass(PVec vec, QIter iter) noexcept : m_vec(vec), m_iter(iter) {
    // ensure iter always point to a valid elem
    assert(iter != vec->end());
    TERARK_ASSERT_EQ(iter->key, iter - vec->begin());
  }
  IterClass(PVec vec, bool/*for_construct_begin*/) noexcept : m_vec(vec) {
    // set iter to first valid element from begin
    auto end = vec->end();
    auto iter = vec->begin();
    while (iter < end && iter->key == nil) ++iter;
    m_iter = iter;
  }
  // construct end iter
  IterClass(PVec vec) noexcept : m_vec(vec), m_iter(vec->end()) {}
public:
  using key_type = Key;
  using mapped_type = Value;
  using difference_type = ptrdiff_t;
  using size_type = size_t;
  using reference = QElem&;
  using pointer = QElem*;

  IterClass() noexcept : m_vec(nullptr) {}
  IterClass& operator++() noexcept {
    assert(m_iter >= m_vec->begin());
    assert(m_iter < m_vec->end());
    // must at a valid element
    TERARK_ASSERT_EQ(m_iter->key, m_iter - m_vec->begin());
    // skip nil(invalid elements)
    do ++m_iter; while (m_iter < m_vec->end() && nil == m_iter->key);
    return *this;
  }
  IterClass operator++(int) noexcept {
    assert(m_iter >= m_vec->begin());
    assert(m_iter < m_vec->end());
    auto tmp = m_iter; ++m_iter; return tmp;
  }
  IterClass& operator--() noexcept {
    assert(m_iter <= m_vec->end());
    assert(m_iter > m_vec->begin());
    // skip nil(invalid elements)
    do --m_iter; while (m_iter > m_vec->begin() && m_iter->key == nil);
    // must reach to a valid element
    TERARK_ASSERT_EQ(m_iter->key, m_iter - m_vec->begin());
    return *this;
  }
  IterClass operator--(int) noexcept {
    assert(m_iter <= m_vec->end());
    assert(m_iter > m_vec->begin());
    auto tmp = m_iter; --m_iter; return tmp;
  }
  QElem* operator->() const noexcept {
    TERARK_ASSERT_EQ(m_iter->key, m_iter - m_vec->begin());
    return AsKV(&*m_iter);
  }
  QElem& operator* () const noexcept {
    TERARK_ASSERT_EQ(m_iter->key, m_iter - m_vec->begin());
    return *AsKV(&*m_iter);
  }
  friend bool operator==(IterClass x, IterClass y) noexcept {
    TERARK_ASSERT_EQ(x.m_vec, y.m_vec);
    return x.m_iter == y.m_iter;
  }
  friend bool operator!=(IterClass x, IterClass y) noexcept {
    TERARK_ASSERT_EQ(x.m_vec, y.m_vec);
    return x.m_iter != y.m_iter;
  }
