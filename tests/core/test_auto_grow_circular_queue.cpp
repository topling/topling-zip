#include <terark/util/auto_grow_circular_queue.hpp>
#include <terark/util/function.hpp>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <deque>

template<int> struct Elem { // for check memory leak
    static size_t g_cnt;
    char val;
    Elem(char c) : val(c) { g_cnt++; }
    Elem(const Elem& y) : val(y.val) { g_cnt++; }
    Elem(Elem&& y) : val(y.val) { g_cnt++; }
    Elem& operator=(const Elem& y) = default;
    Elem& operator=(Elem&& y) = default;
    ~Elem() { g_cnt--; }
    operator char() const { return val; }
};
template<int tag> size_t Elem<tag>::g_cnt;

int main(int argc, char** argv)
{
	terark::AutoGrowCircularQueue<Elem<1> > q1(2);
	terark::AutoGrowCircularQueue2d<Elem<2> > q2(2, 4);
    std::deque<char> qd;
    for (size_t i = 0; i < 1000000; i++) {
        q1.push_back(char(i % 128));
        q2.push_back(char(i % 128));
        qd.push_back(char(i % 128));
        TERARK_VERIFY_EQ(q1.size(), qd.size());
        TERARK_VERIFY_EQ(q2.size(), qd.size());
        TERARK_VERIFY_EQ(q1.size(), Elem<1>::g_cnt);
        TERARK_VERIFY_EQ(q2.size(), Elem<2>::g_cnt);
        if (i % 3 == 0) {
            TERARK_VERIFY_EQ(q1.front(), qd.front());
            TERARK_VERIFY_EQ(q2.front(), qd.front());
            q1.pop_front();
            q2.pop_front();
            qd.pop_front();
        }
        TERARK_VERIFY_EQ(q1.size(), Elem<1>::g_cnt);
        TERARK_VERIFY_EQ(q2.size(), Elem<2>::g_cnt);
    }
    TERARK_VERIFY_EQ(q1.size(), qd.size());
    while (!q1.empty()) {
        TERARK_VERIFY_EQ(q1.size(), qd.size());
        TERARK_VERIFY_EQ(q2.size(), qd.size());
        TERARK_VERIFY_EQ(q1.front(), qd.front());
        TERARK_VERIFY_EQ(q2.front(), qd.front());
        q1.pop_front();
        q2.pop_front();
        qd.pop_front();
        TERARK_VERIFY_EQ(q1.size(), Elem<1>::g_cnt);
        TERARK_VERIFY_EQ(q2.size(), Elem<2>::g_cnt);
    }
    printf("AutoGrowCircularQueue & AutoGrowCircularQueue2d test passed!\n");
	return 0;
}

