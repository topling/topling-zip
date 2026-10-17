#include <terark/circular_queue.hpp>
#include <terark/util/concurrent_queue.hpp>

#include <stdio.h>
#include <deque>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
    }
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)

struct NonConstFullQueue : std::deque<int> {
    bool full() { return size() == 3; }
};

void test_capacity_limits() {
    using Ring = terark::circular_queue<int, false>;
    terark::util::concurrent_queue<Ring> physical_limit;
    physical_limit.queue().init(4);
    for (int i = 0; i < 4; i++) CHECK(physical_limit.try_push_back(i));
    CHECK(physical_limit.full());
    CHECK(physical_limit.peekFull());
    CHECK(!physical_limit.try_push_back(99));
    CHECK(physical_limit.size() == 4);
    CHECK(physical_limit.pop_front() == 0);

    terark::util::concurrent_queue<Ring> requested_limit(2);
    requested_limit.queue().init(4);
    CHECK(requested_limit.try_push_back(1));
    CHECK(requested_limit.try_push_back(2));
    CHECK(requested_limit.full());
    CHECK(requested_limit.peekFull());
    CHECK(!requested_limit.try_push_back(3));

    terark::util::concurrent_queue<NonConstFullQueue> legacy_query(10);
    for (int i = 0; i < 3; i++) CHECK(legacy_query.try_push_back(i));
    CHECK(legacy_query.full());
    CHECK(legacy_query.peekFull());
    CHECK(!legacy_query.try_push_back(99));

    terark::util::concurrent_queue<std::deque<int>> ordinary(2);
    CHECK(ordinary.try_push_back(1));
    CHECK(ordinary.try_push_back(2));
    CHECK(ordinary.full());
    CHECK(!ordinary.try_push_back(3));
}

} // namespace

int main() {
    int failures = 0;
    auto run = [&](const std::string& name, auto test) {
        try {
            test();
            printf("PASS %s\n", name.c_str());
        } catch (const std::exception& error) {
            printf("FAIL %s: %s\n", name.c_str(), error.what());
            ++failures;
        }
    };
    run("capacity limits", test_capacity_limits);
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
