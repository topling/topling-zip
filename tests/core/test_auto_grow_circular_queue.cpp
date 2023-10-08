#include <terark/util/auto_grow_circular_queue.hpp>
#include <terark/util/function.hpp>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <deque>

int main(int argc, char** argv)
{
	terark::AutoGrowCircularQueue<char> aq(2);
    std::deque<char> dq;
    for (size_t i = 0; i < 1000000; i++) {
        aq.push_back(char(i % 128));
        dq.push_back(char(i % 128));
        TERARK_VERIFY_EQ(aq.size(), dq.size());
        if (i % 3 == 0) {
            TERARK_VERIFY_EQ(aq.front(), dq.front());
            aq.pop_front();
            dq.pop_front();
        }
    }
    TERARK_VERIFY_EQ(aq.size(), dq.size());
    while (!aq.empty()) {
        TERARK_VERIFY_EQ(aq.size(), dq.size());
        TERARK_VERIFY_EQ(aq.front(), dq.front());
        aq.pop_front();
        dq.pop_front();
    }
    printf("AutoGrowCircularQueue test passed!\n");
	return 0;
}

