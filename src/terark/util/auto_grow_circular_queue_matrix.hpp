#pragma once
#include <terark/stdtypes.hpp>
#include <type_traits>

namespace terark {

template<class T>
class AutoGrowCircularQueueMatrix {
    T**    m_mtrx;
    size_t m_rows;
    size_t m_cols;
    size_t m_capa;
    // Unsigned counters wrap naturally; mask only when indexing storage.
    size_t m_head;
    size_t m_tail;
    unsigned char m_cols_shift;
    void check_pow2(size_t val, const char* func, int line) {
        if ((val & (val-1)) != 0 || val <= 1) {
            TERARK_DIE("%s:%d: invalid val = %zd, must be power of 2 and > 1",
                       func, line, val);
        }
    }
    using MyType = AutoGrowCircularQueueMatrix;
public:
    ~AutoGrowCircularQueueMatrix() {
        if (!std::is_trivially_destructible<T>::value) {
            while (!empty()) {
                pop_front();
            }
        }
        for (size_t i = 0, rows = m_rows; i < rows; i++) {
            if (m_mtrx[i])
                free(m_mtrx[i]);
        }
        free(m_mtrx);
    }
    explicit AutoGrowCircularQueueMatrix(size_t rows = 256, size_t cols = 256) {
        check_pow2(rows, BOOST_CURRENT_FUNCTION, __LINE__);
        check_pow2(cols, BOOST_CURRENT_FUNCTION, __LINE__);
        TERARK_VERIFY_LE(rows, size_t(-1) / sizeof(T*));
        TERARK_VERIFY_LE(cols, size_t(-1) / sizeof(T));
        TERARK_VERIFY_LE(rows, size_t(-1) / cols);
        m_mtrx = (T**)malloc(sizeof(T*) * rows); TERARK_VERIFY(nullptr != m_mtrx);
        m_rows = rows;
        m_cols = cols;
        m_capa = cols * rows;
        m_head = 0;
        m_tail = 0;
        m_cols_shift = 0;
        while (cols >> 1) cols >>= 1, m_cols_shift++;
        std::fill_n(m_mtrx, m_rows, nullptr);
    }
    AutoGrowCircularQueueMatrix(const AutoGrowCircularQueueMatrix&) = delete;
    AutoGrowCircularQueueMatrix& operator=(const AutoGrowCircularQueueMatrix&) = delete;
    AutoGrowCircularQueueMatrix(AutoGrowCircularQueueMatrix&& y) {
        m_mtrx = y.m_mtrx;
        m_rows = y.m_rows;
        m_cols = y.m_cols;
        m_capa = y.m_capa;
        m_head = y.m_head;
        m_tail = y.m_tail;
        m_cols_shift = y.m_cols_shift;
        y.m_mtrx = nullptr;
        y.m_rows = 0;
        y.m_cols = 0;
        y.m_capa = 0;
        y.m_head = 0;
        y.m_tail = 0;
        y.m_cols_shift = 0;
    }
    AutoGrowCircularQueueMatrix& operator=(AutoGrowCircularQueueMatrix&& y) {
        if (&y != this) {
            this->~AutoGrowCircularQueueMatrix();
            new(this)AutoGrowCircularQueueMatrix(std::move(y));
        }
        return *this;
    }
    void swap(AutoGrowCircularQueueMatrix& y) {
        std::swap(m_mtrx, y.m_mtrx);
        std::swap(m_rows, y.m_rows);
        std::swap(m_cols, y.m_cols);
        std::swap(m_capa, y.m_capa);
        std::swap(m_head, y.m_head);
        std::swap(m_tail, y.m_tail);
        std::swap(m_cols_shift, y.m_cols_shift);
    }

    const T& front() const { return const_cast<MyType*>(this)->front(); }
    T& front() {
        assert(!empty());
        size_t head_row = (m_head & (m_capa - 1)) >> m_cols_shift;
        size_t head_col = m_head & (m_cols - 1);
        //fprintf(stderr, "%22s: rows %zd, cols %zd, head %zd, tail %zd, head_row %zd, head_col %zd\n", __func__, m_rows, m_cols, m_head, m_tail, m_head >> m_cols_shift, m_head & (m_cols - 1));
        return m_mtrx[head_row][head_col];
    }
    void pop_front() {
        assert(!empty());
        size_t cols_1 = m_cols - 1;
        size_t head = m_head;
        size_t head_row = (head & (m_capa - 1)) >> m_cols_shift;
        if (!std::is_trivially_destructible<T>::value) {
            size_t head_col = head & cols_1;
            m_mtrx[head_row][head_col].~T();
        }
        //fprintf(stderr, "%22s: rows %zd, cols %zd, head %zd, tail %zd, head_row %zd, head_col %zd\n", __func__, m_rows, m_cols, m_head, m_tail, m_head >> m_cols_shift, m_head & (m_cols - 1));
        if (terark_unlikely(((head + 1) & cols_1) == 0)) {
            // A nearly full queue can still use the prefix of this same row.
            if (m_tail - (head + 1) <= m_capa - m_cols)
                free(m_mtrx[head_row]), m_mtrx[head_row] = nullptr;
        }
        m_head = head + 1;
    }
    void push_back(const T& x) { emplace_back(x); }
    template<class... Args> void emplace_back(Args&&... args) {
        if (terark_likely(size() < m_capa)) {
            emplace_back_fast_path(std::forward<Args>(args)...);
        } else { // queue is full
            emplace_back_slow_path(std::forward<Args>(args)...);
        }
    }
    const T& back() const { return const_cast<MyType*>(this)->back(); }
    T& back() {
        assert(!empty());
        size_t tail_row = ((m_tail - 1) & (m_capa - 1)) >> m_cols_shift;
        size_t tail_col = ((m_tail - 1) & (m_cols - 1));
        return m_mtrx[tail_row][tail_col];
    }
    size_t capacity() const { return m_capa; }
    size_t remain() const { return m_capa - size(); }
    size_t size() const { return m_tail - m_head; }
    bool  empty() const { return m_head == m_tail; }
    bool  needs_expand() const { return size() == m_capa; }
private:
    template<class... Args>
    void emplace_back_fast_path(Args&&... args) {
        size_t tail = m_tail;
        //fprintf(stderr, "%22s: rows %zd, cols %zd, head %zd, tail %zd, head_row %zd, head_col %zd\n", __func__, m_rows, m_cols, m_head, m_tail, m_head >> m_cols_shift, m_head & (m_cols - 1));
        size_t tail_row = (tail & (m_capa - 1)) >> m_cols_shift;
        size_t tail_col = tail & (m_cols - 1);
        T*& row = m_mtrx[tail_row];
        if (terark_unlikely(nullptr == row)) {
            row = (T*)malloc(sizeof(T) * m_cols);
            TERARK_VERIFY_EZ(tail_col);
            TERARK_VERIFY(nullptr != row);
        }
        new(&row[tail_col]) T (std::forward<Args>(args)...);
        m_tail = tail + 1;
    }
    template<class... Args>
    terark_no_inline void emplace_back_slow_path(Args&&... args) {
        double_cap();
        emplace_back_fast_path(std::forward<Args>(args)...);
    }
    terark_no_inline void double_cap() {
        //fprintf(stderr, "%22s: rows %zd, cols %zd, head %zd, tail %zd, head_row %zd, head_col %zd\n", __func__, m_rows, m_cols, m_head, m_tail, m_head >> m_cols_shift, m_head & (m_cols - 1));
        TERARK_VERIFY_LE(m_capa, size_t(-1) / 2);
        TERARK_VERIFY_LE(m_rows, size_t(-1) / sizeof(T*) / 2);
        m_mtrx = (T**)realloc(m_mtrx, sizeof(T*) * m_rows * 2);
        TERARK_VERIFY(nullptr != m_mtrx);
        size_t head = m_head & (m_capa - 1);
        size_t head_row = head >> m_cols_shift;
        size_t head_col = head & (m_cols - 1);
        std::fill_n(m_mtrx + m_rows, m_rows, nullptr);
        // Put the wrapped prefix above the old capacity without moving whole rows.
        for (size_t row = 0; row < head_row; row++) {
            m_mtrx[row + m_rows] = m_mtrx[row];
            m_mtrx[row] = nullptr;
        }
        if (head_col) {
            T* split = (T*)malloc(sizeof(T) * m_cols);
            TERARK_VERIFY(nullptr != split);
            memcpy(split, m_mtrx[head_row], sizeof(T) * head_col);
            m_mtrx[head_row + m_rows] = split;
        }
        m_head = head;
        m_tail = head + m_capa;
        m_rows *= 2;
        m_capa *= 2;
    }
};

} // namespace terark
