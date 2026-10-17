#include <terark/circular_queue.hpp>
#include <terark/util/concurrent_queue.hpp>

#include <atomic>
#include <chrono>
#include <stdio.h>
#include <deque>
#include <stdexcept>
#include <string>
#include <thread>

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

thread_local unsigned waiter_bit = 0;
std::atomic<unsigned> empty_waiters{0};
std::atomic<unsigned> full_waiters{0};
std::atomic<int> throw_copy_value{-1};
std::atomic<int> throw_assign_value{-1};

struct Item {
    int value;
    explicit Item(int n = 0) : value(n) {}
    Item(const Item& other) : value(other.value) {
        if (value == throw_copy_value.load()) throw std::runtime_error("copy");
    }
    Item& operator=(const Item& other) {
        if (other.value == throw_assign_value.load()) throw std::runtime_error("assignment");
        value = other.value;
        return *this;
    }
};

struct ObservedQueue : std::deque<Item> {
    size_type size() const {
        size_type count = std::deque<Item>::size();
        if (count >= 4) full_waiters.fetch_or(waiter_bit);
        return count;
    }
    bool empty() const {
        bool result = std::deque<Item>::empty();
        if (result) empty_waiters.fetch_or(waiter_bit);
        return result;
    }
};

template<class Predicate>
bool await(Predicate ready, int milliseconds) {
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::yield();
    }
    return true;
}

enum BatchKind { blocking, timed, nowait };
using Queue = terark::util::concurrent_queue<ObservedQueue>;

size_t batch_push(Queue& queue, const Item* input, size_t count, BatchKind kind) {
    if (kind == blocking) return queue.push_back_n(input, count);
    if (kind == timed) return queue.push_back_n(input, count, 100);
    return queue.push_back_n_nowait(input, count);
}

size_t batch_pop(Queue& queue, Item* output, size_t count, BatchKind kind) {
    if (kind == blocking) return queue.pop_front_n(output, count);
    if (kind == timed) return queue.pop_front_n(output, count, 100);
    return queue.pop_front_n_nowait(output, count);
}

void test_batch_push(BatchKind kind, bool throwing) {
    Queue queue(4);
    empty_waiters.store(0);
    std::atomic<unsigned> completed{0};
    Item output[2];
    auto consume = [&](unsigned index) {
        waiter_bit = 1U << index;
        queue.pop_front(output[index]);
        completed.fetch_add(1);
    };
    std::thread first(consume, 0), second(consume, 1);
    bool synchronized = await([] { return empty_waiters.load() == 3; }, 2000);
    Item input[] = {Item(10), Item(20), Item(99)};
    throw_copy_value.store(throwing ? 99 : -1);
    bool caught = false;
    size_t inserted = 0;
    try {
        inserted = batch_push(queue, input, throwing ? 3 : 2, kind);
    } catch (const std::runtime_error&) {
        caught = true;
    }
    throw_copy_value.store(-1);
    bool all_awake = await([&] { return completed.load() == 2; }, 250);
    // Always wake any missed waiter before reporting failure, so regressions cannot hang the test.
    queue.push_back(Item(30));
    queue.push_back(Item(40));
    first.join();
    second.join();
    CHECK(synchronized);
    CHECK(caught == throwing);
    CHECK(throwing || inserted == 2);
    CHECK(all_awake);
    CHECK(output[0].value + output[1].value == 30);
}

void test_batch_pop(BatchKind kind, bool throwing) {
    Queue queue(4);
    for (int value : {10, 20, 99, 40}) queue.push_back(Item(value));
    full_waiters.store(0);
    std::atomic<unsigned> completed{0};
    auto produce = [&](unsigned index) {
        waiter_bit = 1U << index;
        queue.push_back(Item(50 + int(index)));
        completed.fetch_add(1);
    };
    std::thread first(produce, 0), second(produce, 1);
    bool synchronized = await([] { return full_waiters.load() == 3; }, 2000);
    Item output[3];
    throw_assign_value.store(throwing ? 99 : -1);
    bool caught = false;
    size_t popped = 0;
    try {
        popped = batch_pop(queue, output, throwing ? 3 : 2, kind);
    } catch (const std::runtime_error&) {
        caught = true;
    }
    throw_assign_value.store(-1);
    bool all_awake = await([&] { return completed.load() == 2; }, 250);
    Item extra;
    queue.pop_front(extra);
    queue.pop_front(extra);
    first.join();
    second.join();
    CHECK(synchronized);
    CHECK(caught == throwing);
    CHECK(throwing || popped == 2);
    CHECK(all_awake);
    CHECK(output[0].value == 10 && output[1].value == 20);
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
    for (BatchKind kind : {blocking, timed, nowait}) {
        for (bool throwing : {false, true}) {
            std::string name = std::to_string(int(kind)) + (throwing ? " throwing" : " normal");
            run("batch push " + name, [&] { test_batch_push(kind, throwing); });
            run("batch pop " + name, [&] { test_batch_pop(kind, throwing); });
        }
    }
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
