#pragma GCC diagnostic ignored "-Wclass-memaccess"
#include <stdio.h>
#include <deque>
#include <map>

// pre include headers in auto_grow_circular_queue.hpp &
//                        auto_grow_circular_queue_matrix.hpp
#include <terark/util/function.hpp>
#include <type_traits>
#include <string.h>

class DebugMallocLeakCheck {
    struct MallocMemInfo {
        size_t size;
        size_t malloc_seqno;
        size_t realloc_seqno;
        size_t free_seqno;
        const char* file;
        int line;
    };
    size_t m_malloc_seqno = 1;
    size_t m_realloc_seqno = 1;
    size_t m_free_seqno = 1;
    std::map<void*, MallocMemInfo> m_mem_map;
public:
    void* debug_malloc(size_t size, const char* file, int line) {
        void* mem = malloc(size);
        m_mem_map[mem] = {size, m_malloc_seqno++, m_realloc_seqno, m_free_seqno, file, line};
        return mem;
    }
    void* debug_realloc(void* old, size_t size, const char* file, int line) {
        void* mem = realloc(old, size);
        m_mem_map[mem] = {size, m_malloc_seqno, m_realloc_seqno++, m_free_seqno, file, line};
        if (mem != old) {
            if (nullptr != old) {
                TERARK_VERIFY_EQ(m_mem_map.count(old), 1);
                m_mem_map.erase(old);
            }
        }
        return mem;
    }
    void debug_free(void* mem) {
        if (mem) {
            m_free_seqno++;
            TERARK_VERIFY_EQ(m_mem_map.count(mem), 1);
            m_mem_map.erase(mem);
            free(mem);
        }
    }
    size_t allocated_blocks() const { return m_mem_map.size(); }
    void debug_malloc_check_memleak() {
        if (m_mem_map.empty()) {
            fprintf(stderr, "No memory leaks\n");
        } else {
            for (auto& [mem, info] : m_mem_map) {
                fprintf(stderr, "memory leak %p size %zd at %s:%d: {%zd %zd %zd}\n",
                        mem, info.size, info.file, info.line,
                        info.malloc_seqno, info.realloc_seqno, info.free_seqno);
            }
            TERARK_DIE("%zd blocks memory leaks", m_mem_map.size());
        }
    }
    ~DebugMallocLeakCheck() {
        debug_malloc_check_memleak();
    }
};

DebugMallocLeakCheck g_leak_check;

#define malloc(size)       g_leak_check.debug_malloc (size, __FILE__, __LINE__)
#define realloc(old, size) g_leak_check.debug_realloc(old, size, __FILE__, __LINE__)
#define free               g_leak_check.debug_free

#include <terark/util/auto_grow_circular_queue.hpp>
#include <terark/util/auto_grow_circular_queue_matrix.hpp>

template<int> struct Elem { // for check memory leak
    static size_t g_cnt;
    char val;
    Elem(char c) : val(c) { g_cnt++; }
    Elem(const Elem& y) : val(y.val) { g_cnt++; }
    Elem(Elem&& y) : val(y.val) { g_cnt++; }
    Elem& operator=(const Elem& y) = default;
    Elem& operator=(Elem&& y) = default;
    ~Elem() { g_cnt--; val = -1; }
    operator char() const { return val; }
};
template<int tag> size_t Elem<tag>::g_cnt;

template<class Queue> void check_full_capacity(Queue& q) {
    for (int i = 0; i < 8; i++) {
        q.push_back(i);
        TERARK_VERIFY_EQ(q.capacity(), 8);
        TERARK_VERIFY_EQ(q.remain(), 7 - i);
    }
    TERARK_VERIFY(q.needs_expand());
    q.push_back(8);
    TERARK_VERIFY_EQ(q.capacity(), 16);
    for (int i = 0; i < 9; i++) {
        TERARK_VERIFY_EQ(q.front(), i);
        q.pop_front();
    }
    TERARK_VERIFY_EQ(q.remain(), q.capacity());
}

void test_full_capacity() {
    terark::AutoGrowCircularQueue<int> q(8);
    terark::AutoGrowCircularQueueMatrix<int> matrix(2, 4);
    check_full_capacity(q);
    check_full_capacity(matrix);
}

void test_2d_full_chunks() {
    size_t blocks = g_leak_check.allocated_blocks();
    terark::AutoGrowCircularQueue2d<int> q(2, 4);
    for (int i = 0; i < 4; i++) q.push_back(i);
    TERARK_VERIFY_EQ(g_leak_check.allocated_blocks(), blocks + 2);
    for (int i = 4; i < 8; i++) q.push_back(i);
    TERARK_VERIFY_EQ(g_leak_check.allocated_blocks(), blocks + 3);
    for (int i = 0; i < 8; i++) {
        TERARK_VERIFY_EQ(q.front(), i);
        q.pop_front();
    }
    TERARK_VERIFY(q.empty());
    terark::AutoGrowCircularQueue2d<int> single(1, 1);
    for (int i = 0; i < 16; i++) single.push_back(i);
    for (int i = 0; i < 16; i++) {
        TERARK_VERIFY_EQ(single.front(), i);
        single.pop_front();
    }
}

void test_batch_pop() {
    for (size_t offset = 0; offset < 8; offset++) {
        for (size_t count = 0; count <= 8; count++) {
            for (size_t n = 0; n <= 9; n++) {
                terark::AutoGrowCircularQueue<int> q(8);
                for (size_t i = 0; i < offset; i++) {
                    q.push_back(-1);
                    q.pop_front();
                }
                for (size_t i = 0; i < count; i++) q.push_back(int(i));
                int output[10];
                std::fill_n(output, 10, -1);
                q.pop_n(output, n);
                size_t popped = std::min(n, count);
                for (size_t i = 0; i < popped; i++) TERARK_VERIFY_EQ(output[i], int(i));
                TERARK_VERIFY_EQ(output[popped], -1);
                TERARK_VERIFY_EQ(q.size(), count - popped);
                std::fill_n(output, 10, -1);
                q.pop_all(output);
                for (size_t i = popped; i < count; i++) {
                    TERARK_VERIFY_EQ(output[i - popped], int(i));
                }
                TERARK_VERIFY_EQ(output[count - popped], -1);
                TERARK_VERIFY(q.empty());
                q.pop_all(output);
                TERARK_VERIFY_EQ(output[count - popped], -1);
                q.push_back(42);
                TERARK_VERIFY_EQ(q.pop_front_val(), 42);
            }
        }
    }
}

template<class Queue> void check_random_operations(Queue& q) {
    std::deque<char> expected;
    uint64_t random = 0x123456789abcdef0;
    for (size_t i = 0; i < 100000; i++) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        if (expected.empty() || (random & 3) != 0) {
            char value = char((random >> 8) & 127);
            q.emplace_back(value);
            expected.push_back(value);
        } else {
            TERARK_VERIFY_EQ(q.front(), expected.front());
            q.pop_front();
            expected.pop_front();
        }
        TERARK_VERIFY_EQ(q.size(), expected.size());
        if (!expected.empty()) {
            const Queue& cq = q;
            TERARK_VERIFY_EQ(cq.front(), expected.front());
            TERARK_VERIFY_EQ(q.back(), expected.back());
        }
    }
    while (!expected.empty()) {
        TERARK_VERIFY_EQ(q.front(), expected.front());
        q.pop_front();
        expected.pop_front();
    }
}

void test_wrapped_growth_and_lifetime() {
    for (size_t offset = 0; offset < 16; offset++) {
        for (size_t count = 0; count <= 17; count++) {
            terark::AutoGrowCircularQueue<Elem<4>> q(8);
            terark::AutoGrowCircularQueueMatrix<Elem<5>> matrix(2, 4);
            for (size_t i = 0; i < offset; i++) {
                q.emplace_back(-1);
                q.pop_front();
                matrix.emplace_back(-1);
                matrix.pop_front();
            }
            for (size_t i = 0; i < count; i++) {
                q.emplace_back(char(i));
                matrix.emplace_back(char(i));
                TERARK_VERIFY_EQ(Elem<4>::g_cnt, q.size());
                TERARK_VERIFY_EQ(Elem<5>::g_cnt, matrix.size());
            }
            if (count > 8) {
                for (size_t i = 0; i < count; i++) {
                    TERARK_VERIFY_EQ(q.front(), char(i));
                    TERARK_VERIFY_EQ(matrix.front(), char(i));
                    q.pop_front();
                    matrix.pop_front();
                }
            }
        }
        TERARK_VERIFY_EQ(Elem<4>::g_cnt, 0);
        TERARK_VERIFY_EQ(Elem<5>::g_cnt, 0);
    }
    {
        terark::AutoGrowCircularQueue<Elem<4>> q(1);
        terark::AutoGrowCircularQueue2d<Elem<5>> q2d(2, 2);
        terark::AutoGrowCircularQueueMatrix<Elem<6>> matrix(2, 2);
        check_random_operations(q);
        check_random_operations(q2d);
        check_random_operations(matrix);
    }
    TERARK_VERIFY_EQ(Elem<4>::g_cnt, 0);
    TERARK_VERIFY_EQ(Elem<5>::g_cnt, 0);
    TERARK_VERIFY_EQ(Elem<6>::g_cnt, 0);
}

int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "full") == 0) {
        test_full_capacity();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "2d") == 0) {
        test_2d_full_chunks();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "matrix") == 0) {
        terark::AutoGrowCircularQueueMatrix<int> matrix(2, 4);
        check_full_capacity(matrix);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "batch") == 0) {
        test_batch_pop();
        return 0;
    }
    test_full_capacity();
    test_2d_full_chunks();
    test_batch_pop();
    test_wrapped_growth_and_lifetime();
    terark::AutoGrowCircularQueue<Elem<1> > q1(2);
    terark::AutoGrowCircularQueue2d<Elem<2> > q2(2, 4);
    terark::AutoGrowCircularQueueMatrix<Elem<3> > q3(2, 4);
    std::deque<char> qd;
    for (size_t i = 0; i < 1000000; i++) {
        q1.push_back(char(i % 128));
        q2.push_back(char(i % 128));
        q3.push_back(char(i % 128));
        qd.push_back(char(i % 128));
        TERARK_VERIFY_EQ(q1.size(), qd.size());
        TERARK_VERIFY_EQ(q2.size(), qd.size());
        TERARK_VERIFY_EQ(q3.size(), qd.size());
        TERARK_VERIFY_EQ(q1.size(), Elem<1>::g_cnt);
        TERARK_VERIFY_EQ(q2.size(), Elem<2>::g_cnt);
        TERARK_VERIFY_EQ(q3.size(), Elem<3>::g_cnt);
        TERARK_VERIFY_EQ(q1.back(), qd.back());
        TERARK_VERIFY_EQ(q2.back(), qd.back());
        TERARK_VERIFY_EQ(q3.back(), qd.back());
        if (i*i % 3 < 2) {
            TERARK_VERIFY_EQ(q1.front(), qd.front());
            TERARK_VERIFY_EQ(q2.front(), qd.front());
            TERARK_VERIFY_EQ(q3.front(), qd.front());
            q1.pop_front();
            q2.pop_front();
            q3.pop_front();
            qd.pop_front();
        }
        if (i*i % 7 < 2) {
          q1.push_back(char(i % 128));
          q2.push_back(char(i % 128));
          q3.push_back(char(i % 128));
          qd.push_back(char(i % 128));
        }
        if (i*i % 11 < 6 && !qd.empty()) {
            TERARK_VERIFY_EQ(q1.front(), qd.front());
            TERARK_VERIFY_EQ(q2.front(), qd.front());
            TERARK_VERIFY_EQ(q3.front(), qd.front());
            q1.pop_front();
            q2.pop_front();
            q3.pop_front();
            qd.pop_front();
        }
        TERARK_VERIFY_EQ(q1.size(), Elem<1>::g_cnt);
        TERARK_VERIFY_EQ(q2.size(), Elem<2>::g_cnt);
        TERARK_VERIFY_EQ(q3.size(), Elem<3>::g_cnt);
        if (i*i % 6151 <= 3) {
            decltype(q1)(2  ).swap(q1);
            decltype(q2)(2,4).swap(q2);
            decltype(q3)(2,4).swap(q3);
            decltype(qd)(   ).swap(qd);
        }
    }
    TERARK_VERIFY_EQ(q1.size(), qd.size());
    TERARK_VERIFY_EQ(q2.size(), qd.size());
    TERARK_VERIFY_EQ(q3.size(), qd.size());
    while (!q1.empty()) {
        TERARK_VERIFY_EQ(q1.size(), qd.size());
        TERARK_VERIFY_EQ(q2.size(), qd.size());
        TERARK_VERIFY_EQ(q3.size(), qd.size());
        TERARK_VERIFY_EQ(q1.front(), qd.front());
        TERARK_VERIFY_EQ(q2.front(), qd.front());
        TERARK_VERIFY_EQ(q3.front(), qd.front());
        q1.pop_front();
        q2.pop_front();
        q3.pop_front();
        qd.pop_front();
        TERARK_VERIFY_EQ(q1.size(), Elem<1>::g_cnt);
        TERARK_VERIFY_EQ(q2.size(), Elem<2>::g_cnt);
        TERARK_VERIFY_EQ(q3.size(), Elem<3>::g_cnt);
    }
    printf("AutoGrowCircularQueue & AutoGrowCircularQueue2d test passed!\n");
    //malloc(123);
    return 0;
}

