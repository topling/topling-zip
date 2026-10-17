#pragma once
#include <terark/util/function.hpp>

namespace terark {

template<class T>
class AutoGrowCircularQueue {
    //static_assert(std::is_trivially_destructible<T>::value, "T must be trivially destructible");
    T*          m_vec;
    size_t      m_cap;
    // Unsigned counters wrap naturally; mask only when indexing storage.
    size_t      m_head;
    size_t      m_tail;
    size_t check_cap(size_t cap) {
        if ((cap & (cap-1)) != 0 || 0 == cap || cap > size_t(-1) / sizeof(T)) {
            TERARK_DIE("invalid cap = %zd, must be a power of 2 that fits allocation", cap);
        }
        return cap;
    }
public:
    ~AutoGrowCircularQueue() {
        size_t count = size();
        if (count) {
            size_t head = m_head & (m_cap - 1);
            size_t first = std::min(count, m_cap - head);
            STDEXT_destroy_range(m_vec + head, m_vec + head + first);
            STDEXT_destroy_range(m_vec, m_vec + count - first);
        }
        free(m_vec);
    }
    explicit AutoGrowCircularQueue(size_t cap = 256) {
        m_vec = (T*)malloc(sizeof(T) * check_cap(cap));
        TERARK_VERIFY(nullptr != m_vec);
        m_cap = cap;
        m_head = 0;
        m_tail = 0;
    }
    AutoGrowCircularQueue(const AutoGrowCircularQueue& y) {
        m_vec = (T*)malloc(sizeof(T) * check_cap(y.m_cap));
        TERARK_VERIFY(nullptr != m_vec);
        m_cap  = y.m_cap ;
        m_head = y.m_head;
        m_tail = y.m_tail;
        memcpy(m_vec, y.m_vec, sizeof(T) * m_cap);
    }
    AutoGrowCircularQueue(AutoGrowCircularQueue&& y) {
        m_vec  = y.m_vec ;
        m_cap  = y.m_cap ;
        m_head = y.m_head;
        m_tail = y.m_tail;
        y.m_vec  = nullptr;
        y.m_cap  = 0;
        y.m_head = 0;
        y.m_tail = 0;
    }
    AutoGrowCircularQueue& operator=(const AutoGrowCircularQueue& y) {
        if (&y != this) {
            this->~AutoGrowCircularQueue();
            new(this)AutoGrowCircularQueue(y);
        }
        return *this;
    }
    AutoGrowCircularQueue& operator=(AutoGrowCircularQueue&& y) {
        if (&y != this) {
            this->~AutoGrowCircularQueue();
            new(this)AutoGrowCircularQueue(std::move(y));
        }
        return *this;
    }
    void swap(AutoGrowCircularQueue& y) {
        std::swap(m_vec, y.m_vec);
        std::swap(m_cap  , y.m_cap  );
        std::swap(m_head , y.m_head );
        std::swap(m_tail , y.m_tail );
    }
    const T& front() const {
        assert(!empty());
        return m_vec[m_head & (m_cap - 1)];
    }
    T& front() {
        assert(!empty());
        return m_vec[m_head & (m_cap - 1)];
    }
    void pop_front() {
        assert(!empty());
        if (!std::is_trivially_destructible<T>::value) {
            m_vec[m_head & (m_cap - 1)].~T();
        }
        ++m_head;
    }
    bool pop_front_then_check_empty() {
        pop_front();
        return empty();
    }
    T pop_front_val() {
        assert(!empty());
        size_t head = m_head & (m_cap - 1);
        T x(std::move(m_vec[head]));
        if (!std::is_trivially_destructible<T>::value) {
            m_vec[head].~T();
        }
        ++m_head;
        return x;
    }
    // caller ensure @param output has enough space
    void pop_all(T* output) {
        pop_n(output, size());
    }
    // caller ensure @param output has enough space
    void pop_n(T* output, size_t n) {
        n = std::min(n, this->size());
        if (0 == n) return;
        size_t head = m_head & (m_cap - 1);
        size_t first = std::min(m_cap - head, n);
        memcpy(output, m_vec + head, sizeof(T) * first);
        if (n > first) memcpy(output + first, m_vec, sizeof(T) * (n - first));
        m_head += n;
    }
    void push_back(const T& x) {
        size_t tail = m_tail;
        if (terark_likely(tail - m_head < m_cap)) {
            new(&m_vec[tail & (m_cap - 1)]) T (x);
            m_tail = tail + 1;
        }
        else { // queue is full
            push_back_slow_path(x);
        }
    }
    template<class... Args>
    void emplace_back(Args&&... args) {
        size_t tail = m_tail;
        if (terark_likely(tail - m_head < m_cap)) {
            new(&m_vec[tail & (m_cap - 1)]) T (std::forward<Args>(args)...);
            m_tail = tail + 1;
        }
        else { // queue is full
            emplace_back_slow_path(std::forward<Args>(args)...);
        }
    }
    const T& back() const {
        assert(!empty());
        return m_vec[(m_tail - 1) & (m_cap - 1)];
    }
    T& back() {
        assert(!empty());
        return m_vec[(m_tail - 1) & (m_cap - 1)];
    }
    size_t capacity() const { return m_cap; }
    size_t remain() const { return m_cap - size(); }
    size_t size() const { return m_tail - m_head; }
    bool  empty() const { return m_head == m_tail; }
    bool  needs_expand() const { return size() == m_cap; }
private:
    terark_no_inline
    void push_back_slow_path(const T& x) {
        double_cap();
        new(&m_vec[m_tail & (m_cap - 1)]) T (x);
        ++m_tail;
    }
    template<class... Args>
    terark_no_inline
    void emplace_back_slow_path(Args&&... args) {
        double_cap();
        new(&m_vec[m_tail & (m_cap - 1)]) T (std::forward<Args>(args)...);
        ++m_tail;
    }
    terark_no_inline
    void double_cap() {
        size_t cap = m_cap;
        TERARK_VERIFY_LE(cap, size_t(-1) / 2);
        size_t new_cap = check_cap(cap * 2);
        m_vec = (T*)realloc(m_vec, sizeof(T) * new_cap);
        TERARK_VERIFY(nullptr != m_vec);
        T*     base = m_vec;
        size_t head = m_head & (cap - 1);
        if (head > cap / 2) {
            memcpy(base + cap + head, base + head, sizeof(T)*(cap - head));
            m_head = cap + head;
            m_tail = m_head + cap;
        }
        else {
            memcpy(base + cap, base, sizeof(T) * head);
            m_head = head;
            m_tail = cap + head;
        }
        m_cap = new_cap;
    }
};

template<class T>
class AutoGrowCircularQueue2d {
    size_t m_cap_col;
    size_t m_size;
    AutoGrowCircularQueue<AutoGrowCircularQueue<T> > m_queue;
    size_t check_cap(size_t cap) {
        if ((cap & (cap-1)) != 0 || 0 == cap) {
            TERARK_DIE("invalid cap = %zd, must be power of 2", cap);
        }
        return cap;
    }
public:
    AutoGrowCircularQueue2d(const AutoGrowCircularQueue2d&) = delete;
    AutoGrowCircularQueue2d& operator=(const AutoGrowCircularQueue2d&) = delete;
    explicit AutoGrowCircularQueue2d(size_t cap_row = 256, size_t cap_col = 256)
      : m_cap_col(this->check_cap(cap_col)), m_size(0), m_queue(cap_row) {}
    void swap(AutoGrowCircularQueue2d& y) {
        std::swap(m_cap_col, y.m_cap_col);
        std::swap(m_size   , y.m_size   );
        m_queue.swap(y.m_queue);
    }
    const T& front() const {
        assert(m_size > 0);
        return m_queue.front().front();
    }
    T& front() {
        assert(m_size > 0);
        return m_queue.front().front();
    }
    const T& back() const {
        assert(m_size > 0);
        return m_queue.back().back();
    }
    T& back() {
        assert(m_size > 0);
        return m_queue.back().back();
    }
    void pop_front() {
        assert(m_size > 0);
        if (terark_unlikely(m_queue.front().pop_front_then_check_empty())) {
            m_queue.pop_front();
        }
        m_size--;
    }
    void push_back(const T& x) {
        if (terark_unlikely(m_queue.empty() || m_queue.back().needs_expand())) {
            m_queue.emplace_back(m_cap_col);
        }
        m_queue.back().push_back(x);
        m_size++;
    }
    template<class... Args>
    void emplace_back(Args&&... args) {
        if (terark_unlikely(m_queue.empty() || m_queue.back().needs_expand())) {
            m_queue.emplace_back(m_cap_col);
        }
        m_queue.back().emplace_back(std::forward<Args>(args)...);
        m_size++;
    }
    size_t size() const { return m_size; }
    bool  empty() const { return m_size == 0; }
    // these two functions are wrong:
    //size_t capacity() const { return m_cap_col * m_queue.capacity(); }
    //bool  needs_expand() const { return size() == capacity(); }
};

}

