#include <terark/circular_queue.hpp>
#include <terark/fixed_circular_queue.hpp>

#include <algorithm>
#include <stdio.h>
#include <deque>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
    }
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)

template<class Queue>
void check_full_capacity(Queue& queue) {
    const size_t capacity = queue.capacity();
    CHECK(queue.empty());
    CHECK(queue.size() == 0);
    for (size_t i = 0; i < capacity; ++i) {
        // Check before insertion so legacy code fails without overrunning its storage.
        CHECK(!queue.full());
        queue.push_back(int(i));
        CHECK(queue.size() == i + 1);
    }
    CHECK(queue.full());
    CHECK(!queue.empty());
    CHECK(queue.size() == capacity);
    for (size_t i = 0; i < capacity; ++i) {
        CHECK(queue.front() == int(i));
        queue.pop_front();
    }
    CHECK(queue.empty());
    CHECK(!queue.full());
}

#ifndef CIRCULAR_QUEUE_LEGACY_BASELINE

template<class Queue>
void check_contents(const Queue& queue, const std::deque<int>& expected) {
    CHECK(queue.size() == expected.size());
    CHECK(queue.empty() == expected.empty());
    CHECK(queue.full() == (expected.size() == queue.capacity()));
    CHECK(queue.end() - queue.begin() == ptrdiff_t(expected.size()));
    size_t count = 0;
    for (auto iter = queue.begin(); iter != queue.end(); ++iter) {
        CHECK(count < expected.size());
        CHECK(*iter == expected[count]);
        CHECK(queue[ptrdiff_t(count)] == expected[count]);
        CHECK(queue.virtual_index(queue.real_index(ptrdiff_t(count))) == ptrdiff_t(count));
        ++count;
    }
    CHECK(count == expected.size());
    CHECK(queue.real_index(0) == queue.head_real_index());
    CHECK(queue.real_index(ptrdiff_t(count)) == queue.tail_real_index());
    CHECK(queue.virtual_index(queue.tail_real_index()) == ptrdiff_t(count));
    if (!expected.empty()) {
        CHECK(queue.front() == expected.front());
        CHECK(queue.back() == expected.back());
    }
}

template<class Queue>
void check_counter_wrap(Queue& queue, bool natural_wrap) {
    using Counter = decltype(queue.head_real_index());
    static_assert(std::is_unsigned<Counter>::value, "queue serials must be unsigned");
    queue.clear();
    queue.push_front(-1);
    queue.pop_back();
    const Counter expected = natural_wrap ? std::numeric_limits<Counter>::max()
                                         : Counter(queue.capacity() * 2 - 1);
    CHECK(queue.head_real_index() == expected);
    CHECK(queue.tail_real_index() == expected);

    // Start one position before counter wrap. Fill completely across that boundary.
    std::deque<int> reference;
    for (size_t cycle = 0; cycle < 12; ++cycle) {
        for (size_t i = 0; i < queue.capacity(); ++i) {
            queue.push_back(int(i + cycle * 100));
            reference.push_back(int(i + cycle * 100));
        }
        check_contents(queue, reference);
        CHECK(queue.head_real_index() != queue.tail_real_index());
        while (!reference.empty()) {
            int output = -1;
            queue.pop_front(output);
            CHECK(output == reference.front());
            reference.pop_front();
        }
        check_contents(queue, reference);
    }

    queue.clear();
    for (size_t i = 0; i < queue.capacity(); ++i) {
        queue.push_front(int(i));
        reference.push_front(int(i));
    }
    check_contents(queue, reference);
    while (!reference.empty()) {
        int output = -1;
        queue.pop_back(output);
        CHECK(output == reference.back());
        reference.pop_back();
        check_contents(queue, reference);
    }
}

template<class Queue>
void check_iterators(Queue& queue) {
    queue.clear();
    queue.push_front(-1);
    queue.pop_back();
    for (size_t i = 0; i < queue.capacity(); ++i) {
        queue.emplace_back(int(i));
    }
    using Iterator = typename Queue::iterator;
    using ConstIterator = typename Queue::const_iterator;
    static_assert(std::is_convertible<Iterator, ConstIterator>::value,
                  "mutable iterators must convert to const iterators");
    static_assert(!std::is_convertible<ConstIterator, Iterator>::value,
                  "const iterators must not convert to mutable iterators");
    const Queue& constant = queue;
    const ptrdiff_t count = ptrdiff_t(queue.capacity());
    CHECK(queue.begin() != queue.end());
    CHECK(queue.begin() + count == queue.end());
    CHECK(count + queue.begin() == queue.end());
    CHECK(queue.end() - count == queue.begin());
    CHECK(queue.end() + -count == queue.begin());
    CHECK(queue.begin() - -count == queue.end());
    CHECK(queue.begin() - queue.end() == -count);
    CHECK(constant.end() - queue.begin() == count);
    CHECK(queue.end() - constant.begin() == count);
    CHECK(constant.begin() == queue.begin());
    CHECK(queue.end() == constant.end());
    CHECK(constant.begin() < queue.end());
    CHECK(queue.begin() <= constant.begin());
    CHECK(constant.end() >= queue.end());
    CHECK(queue.end() > constant.begin());
    auto iter = queue.begin();
    CHECK(iter++ == queue.begin());
    CHECK(iter == queue.begin() + 1);
    CHECK(--iter == queue.begin());
    iter = queue.end();
    CHECK(iter-- == queue.end());
    CHECK(iter == queue.end() - 1);
    CHECK(++iter == queue.end());
    iter += -count;
    CHECK(iter == queue.begin());
    iter -= -count;
    CHECK(iter == queue.end());
    for (ptrdiff_t i = 0; i < count; ++i) {
        CHECK(queue.begin()[i] == i);
        CHECK(constant.begin()[i] == i);
    }
    std::reverse(queue.begin(), queue.end());
    for (ptrdiff_t i = 0; i < count; ++i) {
        CHECK(queue[i] == count - 1 - i);
    }
    std::sort(queue.begin(), queue.end());
    CHECK(std::is_sorted(constant.begin(), constant.end()));
    queue.clear();
}

template<class Queue>
void check_saved_serials(Queue& queue) {
    queue.clear();
    queue.push_front(-1);
    queue.pop_back();
    for (size_t i = 0; i < queue.capacity(); ++i) {
        queue.emplace_back(int(i));
    }
    const size_t middle = queue.capacity() / 2;
    const auto saved = queue.real_index(ptrdiff_t(middle));
    if (middle > 0) {
        queue.pop_front();
        CHECK(queue.virtual_index(saved) == ptrdiff_t(middle - 1));
    }
    queue.pop_lower(saved);
    CHECK(queue.size() == queue.capacity() - middle - 1);
    if (!queue.empty()) {
        CHECK(queue.front() == int(middle + 1));
    }
    queue.clear();
    for (size_t i = 0; i < queue.capacity(); ++i) {
        queue.emplace_front(int(i));
    }
    queue.pop_upper(queue.real_index(ptrdiff_t(middle)));
    CHECK(queue.size() == middle);
    queue.clear();
}

template<class Queue>
void check_random_operations(Queue& queue) {
    queue.clear();
    std::deque<int> reference;
    std::mt19937 random(0x51455545);
    for (size_t step = 0; step < 4000; ++step) {
        const unsigned operation = random() % 9;
        const int value = int(random() & 0x7fffffff);
        if ((operation == 0 || reference.empty()) && !queue.full()) {
            queue.emplace_back(value);
            reference.push_back(value);
        } else if (operation == 1 && !queue.full()) {
            queue.emplace_front(value);
            reference.push_front(value);
        } else if (operation == 2 && !reference.empty()) {
            queue.pop_front();
            reference.pop_front();
        } else if (operation == 3 && !reference.empty()) {
            queue.pop_back();
            reference.pop_back();
        } else if (operation == 4 && !reference.empty()) {
            const size_t index = random() % reference.size();
            queue.pop_lower(queue.real_index(ptrdiff_t(index)));
            reference.erase(reference.begin(), reference.begin() + ptrdiff_t(index + 1));
        } else if (operation == 5 && !reference.empty()) {
            const size_t index = random() % reference.size();
            queue.pop_upper(queue.real_index(ptrdiff_t(index)));
            reference.erase(reference.begin() + ptrdiff_t(index), reference.end());
        } else if (operation == 6 && !reference.empty()) {
            const size_t index = random() % reference.size();
            queue[ptrdiff_t(index)] = value;
            reference[index] = value;
        } else if (operation == 7 && (random() & 15) == 0) {
            queue.clear();
            reference.clear();
        }
        check_contents(queue, reference);
    }
    queue.clear();
}

struct Tracked {
    static int live;
    static bool reject;
    int value;
    explicit Tracked(int value) : value(value) {
        if (reject) {
            throw std::runtime_error("constructor rejected");
        }
        ++live;
    }
    Tracked(const Tracked& other) : Tracked(other.value) {}
    Tracked(Tracked&& other) : Tracked(other.value) { other.value = -1; }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&& other) {
        value = other.value;
        other.value = -1;
        return *this;
    }
    ~Tracked() { --live; }
};
int Tracked::live = 0;
bool Tracked::reject = false;

template<class Queue>
void check_lifetime() {
    CHECK(Tracked::live == 0);
    {
        Queue queue(4);
        queue.emplace_front(1);
        queue.emplace_back(2);
        const auto head = queue.head_real_index();
        const auto tail = queue.tail_real_index();
        Tracked::reject = true;
        bool caught = false;
        try {
            queue.emplace_front(3);
        } catch (const std::runtime_error&) {
            caught = true;
        }
        CHECK(caught);
        caught = false;
        try {
            queue.emplace_back(3);
        } catch (const std::runtime_error&) {
            caught = true;
        }
        Tracked::reject = false;
        CHECK(caught);
        CHECK(queue.head_real_index() == head);
        CHECK(queue.tail_real_index() == tail);
        CHECK(Tracked::live == 2);
        queue.emplace_front(3);
        queue.emplace_back(4);
        CHECK(queue.full());
        CHECK(Tracked::live == 4);
        {
            Tracked output(0);
            queue.pop_front(output);
            CHECK(output.value == 3);
            queue.pop_back(output);
            CHECK(output.value == 4);
        }
        CHECK(Tracked::live == 2);
        queue.clear();
        CHECK(Tracked::live == 0);
        for (int i = 0; i < 4; ++i) {
            queue.emplace_back(i);
        }
    }
    CHECK(Tracked::live == 0);
}

#endif

int failures = 0;
int cases = 0;

template<class Function>
void run(const char* name, Function function) {
    ++cases;
    try {
        function();
        printf("PASS %s\n", name);
    } catch (const std::exception& error) {
        ++failures;
        fprintf(stderr, "FAIL %s: %s\n", name, error.what());
    }
}

template<class Queue>
void run_queue(const char* name, size_t capacity, bool natural_wrap) {
    run(name, [=] {
        Queue queue{ptrdiff_t(capacity)};
        check_full_capacity(queue);
#ifndef CIRCULAR_QUEUE_LEGACY_BASELINE
        check_counter_wrap(queue, natural_wrap);
        check_iterators(queue);
        check_saved_serials(queue);
        check_random_operations(queue);
#else
        (void)natural_wrap;
#endif
    });
}

} // namespace

int main() {
    run_queue<terark::circular_queue<int, true>>("dynamic true capacity 1", 1, true);
    run_queue<terark::circular_queue<int, true>>("dynamic true capacity 2", 2, true);
    run_queue<terark::circular_queue<int, true>>("dynamic true capacity 4", 4, true);
    run_queue<terark::circular_queue<int, true>>("dynamic true capacity 8", 8, true);
    run_queue<terark::circular_queue<int, false>>("dynamic false capacity 1", 1, false);
    run_queue<terark::circular_queue<int, false>>("dynamic false capacity 2", 2, false);
    run_queue<terark::circular_queue<int, false>>("dynamic false capacity 3", 3, false);
    run_queue<terark::circular_queue<int, false>>("dynamic false capacity 5", 5, false);
    run_queue<terark::circular_queue<int, false>>("dynamic false capacity 8", 8, false);
    run_queue<terark::fixed_circular_queue<int, 1>>("fixed capacity 1", 1, true);
    run_queue<terark::fixed_circular_queue<int, 2>>("fixed capacity 2", 2, true);
    run_queue<terark::fixed_circular_queue<int, 3>>("fixed capacity 3", 3, false);
    run_queue<terark::fixed_circular_queue<int, 4>>("fixed capacity 4", 4, true);
    run_queue<terark::fixed_circular_queue<int, 5>>("fixed capacity 5", 5, false);
    run_queue<terark::fixed_circular_queue<int, 8>>("fixed capacity 8", 8, true);
#ifndef CIRCULAR_QUEUE_LEGACY_BASELINE
    run("dynamic object lifetime", check_lifetime<terark::circular_queue<Tracked, true>>);
    run("fixed object lifetime", check_lifetime<terark::fixed_circular_queue<Tracked, 4>>);
#endif
    printf("%d cases, %d failures\n", cases, failures);
    return failures != 0;
}
