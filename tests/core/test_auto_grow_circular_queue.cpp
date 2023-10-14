#pragma GCC diagnostic ignored "-Wclass-memaccess"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <deque>
#include <map>

// pre include headers in auto_grow_circular_queue.hpp &
//                        auto_grow_circular_queue_matrix.hpp
#include <terark/util/function.hpp>
#include <memory>
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
        }
    }
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

int main(int argc, char** argv)
{
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

