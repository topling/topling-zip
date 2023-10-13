#pragma once
#include <terark/util/function.hpp>
#include <memory>
#include <type_traits>
#include <string.h>

namespace terark {

template<class T>
class AutoGrowCircularQueue {
    //static_assert(std::is_trivially_destructible<T>::value, "T must be trivially destructible");
    T*          m_vec;
    size_t      m_cap;
    size_t      m_head;
    size_t      m_tail;
    size_t check_cap(size_t cap) {
        if ((cap & (cap-1)) != 0 || 0 == cap) {
            TERARK_DIE("invalid cap = %zd, must be power of 2", cap);
        }
        return cap;
    }
public:
    ~AutoGrowCircularQueue() {
        if (m_head < m_tail) {
            STDEXT_destroy_range(m_vec + m_head, m_vec + m_tail);
        }
        else if (m_tail < m_head) {
            STDEXT_destroy_range(m_vec + m_head, m_vec + m_cap);
            STDEXT_destroy_range(m_vec, m_vec + m_tail);
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
        assert(((m_tail - m_head) & (m_cap - 1)) > 0);
        return m_vec[m_head];
    }
    T& front() {
        assert(((m_tail - m_head) & (m_cap - 1)) > 0);
        return m_vec[m_head];
    }
    void pop_front() {
        assert(((m_tail - m_head) & (m_cap - 1)) > 0);
        if (!std::is_trivially_destructible<T>::value) {
            m_vec[m_head].~T();
        }
        m_head = (m_head + 1) & (m_cap - 1);
    }
    bool pop_front_then_check_empty() {
        assert(((m_tail - m_head) & (m_cap - 1)) > 0);
        if (!std::is_trivially_destructible<T>::value) {
            m_vec[m_head].~T();
        }
        m_head = (m_head + 1) & (m_cap - 1);
        return m_head == m_tail;
    }
    T pop_front_val() {
        assert(((m_tail - m_head) & (m_cap - 1)) > 0);
        T x(std::move(m_vec[m_head]));
        if (!std::is_trivially_destructible<T>::value) {
            m_vec[m_head].~T();
        }
        m_head = (m_head + 1) & (m_cap - 1);
        return x;
    }
    // caller ensure @param output has enough space
    void pop_all(T* output) {
        T*     base = m_vec;
        size_t head = m_head;
        size_t tail = m_tail;
        if (head < tail) {
            memcpy(output, base + head, sizeof(T)*(tail - head));
        }
        else {
            size_t len1 = m_cap - head;
            size_t len2 = tail;
            memcpy(output, base + head, sizeof(T)*len1);
            memcpy(output + len1, base, sizeof(T)*len2);
        }
        m_head = m_tail = 0;
    }
    // caller ensure @param output has enough space
    void pop_n(T* output, size_t n) {
        n = std::min(n, this->size());
        T*     base = m_vec;
        size_t head = m_head;
        size_t tail = m_tail;
        if (head < tail) {
            memcpy(output, base + head, sizeof(T)*n);
        }
        else {
            size_t len1 = std::min(m_cap - head, n);
            memcpy(output, base + head, sizeof(T)*len1);
            if (n > len1) {
                size_t len2 = n - len1;
                memcpy(output + len1, base, sizeof(T)*len2);
            }
        }
        m_head = (head + n) & (m_cap - 1);
    }
    void push_back(const T& x) {
        size_t head = m_head;
        size_t tail = m_tail;
        size_t cap1 = m_cap - 1;
        if (terark_likely(((head - tail) & cap1) != 1)) {
            new(&m_vec[tail]) T (x);
            m_tail = (tail + 1) & cap1;
        }
        else { // queue is full
            push_back_slow_path(x);
        }
    }
    template<class... Args>
    void emplace_back(Args&&... args) {
        size_t head = m_head;
        size_t tail = m_tail;
        size_t cap1 = m_cap - 1;
        if (terark_likely(((head - tail) & cap1) != 1)) {
            new(&m_vec[tail]) T (std::forward<Args>(args)...);
            m_tail = (tail + 1) & cap1;
        }
        else { // queue is full
            emplace_back_slow_path(std::forward<Args>(args)...);
        }
    }
    const T& back() const {
        assert(((m_tail - m_head) & (m_cap - 1)) > 0);
        return m_vec[(m_tail - 1) & (m_cap - 1)];
    }
    T& back() {
        assert(((m_tail - m_head) & (m_cap - 1)) > 0);
        return m_vec[(m_tail - 1) & (m_cap - 1)];
    }
    size_t capacity() const { return m_cap; }
    size_t size() const { return (m_tail - m_head) & (m_cap - 1); }
    bool  empty() const { return m_head == m_tail; }
    bool  needs_expand() const { return size() == m_cap - 1; }
private:
    terark_no_inline
    void push_back_slow_path(const T& x) {
        double_cap();
        new(&m_vec[m_tail]) T (x);
        m_tail = (m_tail + 1) & (m_cap - 1);
    }
    template<class... Args>
    terark_no_inline
    void emplace_back_slow_path(Args&&... args) {
        double_cap();
        new(&m_vec[m_tail]) T (std::forward<Args>(args)...);
        m_tail = (m_tail + 1) & (m_cap - 1);
    }
    terark_no_inline
    void double_cap() {
        size_t cap = m_cap;
        m_vec = (T*)realloc(m_vec, sizeof(T) * cap * 2);
        TERARK_VERIFY(nullptr != m_vec);
        T*     base = m_vec;
        size_t head = m_head;
        if (head > cap / 2) {
            memcpy(base + cap + head, base + head, sizeof(T)*(cap - head));
            m_head = cap + head;
        }
        else {
            size_t tail = m_tail;
            if (head > 1) {
                assert(tail >= 1);
                assert(tail <= cap - 1);
                memcpy(base + cap, base, sizeof(T)*(tail));
                m_tail = cap + tail;
            } else if (1 == head) {
                assert(0 == tail);
                m_tail = cap;
            } else {
                // do nothing
                assert(head == 0);
                assert(tail == cap - 1);
            }
        }
        m_cap = cap * 2;
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
    //size_t capacity() const { return (m_cap_col - 1) * m_queue.capacity(); }
    //bool  needs_expand() const { return size() == capacity(); }
};

}

