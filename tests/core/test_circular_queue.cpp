#include <terark/circular_queue.hpp>
#include <terark/fixed_circular_queue.hpp>

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <limits>
#include <memory>
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

struct Owned {
    static int live;
    static int copies;
    static int copies_before_throw;
    static int moves_before_throw;
    static bool reject_assignment;
    std::unique_ptr<int> value;

    explicit Owned(int number) : value(new int(number)) { ++live; }
    Owned(const Owned& other) {
        if (copies_before_throw == 0) {
            throw std::runtime_error("copy rejected");
        }
        if (copies_before_throw > 0) {
            --copies_before_throw;
        }
        if (other.value) {
            value.reset(new int(*other.value));
        }
        ++copies;
        ++live;
    }
    Owned(Owned&& other) {
        if (moves_before_throw == 0) {
            throw std::runtime_error("move rejected");
        }
        if (moves_before_throw > 0) {
            --moves_before_throw;
        }
        value = std::move(other.value);
        ++live;
    }
    Owned& operator=(Owned&& other) {
        if (reject_assignment) {
            throw std::runtime_error("assignment rejected");
        }
        value = std::move(other.value);
        return *this;
    }
    ~Owned() { --live; }
};
int Owned::live = 0;
int Owned::copies = 0;
int Owned::copies_before_throw = -1;
int Owned::moves_before_throw = -1;
bool Owned::reject_assignment = false;

template<class Queue>
void fill_owned(Queue& queue) {
    queue.emplace_front(11);
    queue.emplace_back(22);
    queue.emplace_back(33);
}

template<class Queue>
void check_copy_ownership() {
    if constexpr (std::is_copy_constructible<Queue>::value &&
                  std::is_copy_assignable<Queue>::value) {
        CHECK(Owned::live == 0);
        {
            Queue source(8);
            fill_owned(source);
            const auto head = source.head_real_index();
            const auto tail = source.tail_real_index();
            const int copies = Owned::copies;
            Queue copy(source);
            CHECK(Owned::copies == copies + 3);
            CHECK(copy.capacity() == 8);
            CHECK(copy.size() == source.size());
            CHECK(copy.head_real_index() == head);
            CHECK(copy.tail_real_index() == tail);
            for (ptrdiff_t i = 0; i < 3; ++i) {
                CHECK(*copy[i].value == *source[i].value);
                CHECK(copy[i].value.get() != source[i].value.get());
            }
            *source.front().value = 44;
            CHECK(*copy.front().value == 11);
            Queue assigned(8);
            assigned.emplace_back(99);
            assigned = source;
            CHECK(Owned::live == 9);
            CHECK(*assigned.front().value == 44);
            CHECK(assigned.head_real_index() == head);
            CHECK(assigned.tail_real_index() == tail);
            Queue& self = assigned;
            assigned = self;
            assigned = std::move(self);
            CHECK(assigned.size() == 3);
            CHECK(*assigned.front().value == 44);
            CHECK(source.size() == 3);
            CHECK(source.head_real_index() == head);
            CHECK(source.tail_real_index() == tail);
        }
        CHECK(Owned::live == 0);
    } else {
        CHECK(false && "copying live nontrivial elements must be supported");
    }
}

template<class Queue>
void check_move_ownership(bool transfers_storage) {
    if constexpr (std::is_move_constructible<Queue>::value &&
                  std::is_move_assignable<Queue>::value) {
        CHECK(Owned::live == 0);
        {
            Queue source(8);
            fill_owned(source);
            const auto head = source.head_real_index();
            const auto tail = source.tail_real_index();
            int* first = source.front().value.get();
            if (transfers_storage) {
                Owned::moves_before_throw = 0; // Moving dynamic queues must not move elements.
            }
            Queue moved(std::move(source));
            CHECK(source.empty());
            CHECK(moved.size() == 3);
            CHECK(moved.head_real_index() == head);
            CHECK(moved.tail_real_index() == tail);
            CHECK(moved.front().value.get() == first);
            CHECK(Owned::live == 3);
            Queue assigned(8);
            assigned.emplace_back(99);
            assigned = std::move(moved);
            Owned::moves_before_throw = -1;
            CHECK(moved.empty());
            CHECK(assigned.size() == 3);
            CHECK(assigned.front().value.get() == first);
            CHECK(Owned::live == 3);
            Queue& self = assigned;
            assigned = std::move(self);
            CHECK(assigned.size() == 3);
            CHECK(assigned.front().value.get() == first);
            if (transfers_storage) {
                CHECK(source.capacity() == 0);
                source.init(8);
            }
            source.emplace_front(55);
            CHECK(*source.front().value == 55);
        }
        CHECK(Owned::live == 0);
    } else {
        CHECK(false && "moving live nontrivial elements must be supported");
    }
}

void check_empty_ownership() {
    using Queue = terark::circular_queue<int, true>;
    Queue original;
    Queue copied(original);
    Queue moved(std::move(original));
    CHECK(original.empty() && original.capacity() == 0);
    CHECK(copied.empty() && copied.capacity() == 0);
    CHECK(moved.empty() && moved.capacity() == 0);
    Queue assigned(8);
    assigned.emplace_back(1);
    assigned = copied;
    CHECK(assigned.empty() && assigned.capacity() == 0);
    assigned = std::move(moved);
    CHECK(assigned.empty() && assigned.capacity() == 0);
    Queue& self = assigned;
    assigned = self;
    assigned = std::move(self);
    Queue occupied(8);
    occupied.emplace_back(7);
    Queue destination(std::move(occupied));
    Queue copied_from_moved(occupied);
    CHECK(copied_from_moved.empty() && copied_from_moved.capacity() == 0);
    occupied = copied_from_moved;
    occupied.init(8);
    occupied.emplace_back(9);
    CHECK(occupied.front() == 9);
    CHECK(destination.front() == 7);
}

struct DestructorRecord {
    static int destroyed;
    int value;
    explicit DestructorRecord(int number) : value(number) {}
    DestructorRecord(const DestructorRecord&) = default;
    DestructorRecord& operator=(const DestructorRecord&) = default;
    ~DestructorRecord() {
        if (value == 42) {
            ++destroyed;
        }
    }
};
int DestructorRecord::destroyed = 0;

void check_fixed_overwrite_destruction() {
    DestructorRecord::destroyed = 0;
    {
        terark::fixed_circular_queue<DestructorRecord, 4> empty;
        terark::fixed_circular_queue<DestructorRecord, 4> occupied;
        occupied.emplace_back(42);
        occupied = empty;
        CHECK(occupied.empty());
    }
    CHECK(DestructorRecord::destroyed == 1);
}

template<class Queue>
void check_copy_exceptions(bool strong_assignment) {
    if constexpr (std::is_copy_constructible<Queue>::value &&
                  std::is_copy_assignable<Queue>::value) {
        CHECK(Owned::live == 0);
        {
            Queue source(8);
            fill_owned(source);
            Owned::copies_before_throw = 1;
            bool caught = false;
            try {
                Queue copy(source);
            } catch (const std::runtime_error&) {
                caught = true;
            }
            Owned::copies_before_throw = -1;
            CHECK(caught);
            CHECK(source.size() == 3);
            CHECK(*source.front().value == 11);
            CHECK(Owned::live == 3);
            Queue assigned(8);
            assigned.emplace_back(99);
            Owned::copies_before_throw = 1;
            caught = false;
            try {
                assigned = source;
            } catch (const std::runtime_error&) {
                caught = true;
            }
            Owned::copies_before_throw = -1;
            CHECK(caught);
            CHECK(source.size() == 3);
            CHECK(*source.front().value == 11);
            if (strong_assignment) {
                CHECK(assigned.size() == 1);
                CHECK(*assigned.front().value == 99);
                CHECK(Owned::live == 4);
                assigned.clear();
            } else {
                CHECK(assigned.empty());
            }
            CHECK(Owned::live == 3);
            assigned.emplace_front(55);
            CHECK(*assigned.front().value == 55);
        }
        CHECK(Owned::live == 0);
    } else {
        CHECK(false && "copy exceptions need supported element-wise copying");
    }
}

template<class Queue>
void check_fixed_move_exceptions() {
    if constexpr (std::is_move_constructible<Queue>::value &&
                  std::is_move_assignable<Queue>::value) {
        CHECK(Owned::live == 0);
        {
            Queue source(8);
            fill_owned(source);
            Owned::moves_before_throw = 1;
            bool caught = false;
            try {
                Queue moved(std::move(source));
            } catch (const std::runtime_error&) {
                caught = true;
            }
            Owned::moves_before_throw = -1;
            CHECK(caught);
            CHECK(source.size() == 3);
            CHECK(Owned::live == 3);
            Queue assigned(8);
            assigned.emplace_back(99);
            Owned::moves_before_throw = 1;
            caught = false;
            try {
                assigned = std::move(source);
            } catch (const std::runtime_error&) {
                caught = true;
            }
            Owned::moves_before_throw = -1;
            CHECK(caught);
            CHECK(source.size() == 3);
            CHECK(assigned.empty());
            CHECK(Owned::live == 3);
            assigned.emplace_front(55);
        }
        CHECK(Owned::live == 0);
    } else {
        CHECK(false && "move exceptions need supported element-wise moving");
    }
}

void check_pop_back_assignment_exception() {
    CHECK(Owned::live == 0);
    {
        terark::circular_queue<Owned, true> queue(4);
        queue.emplace_back(7);
        Owned output(0);
        const auto tail = queue.tail_real_index();
        Owned::reject_assignment = true;
        bool caught = false;
        try {
            queue.pop_back(output);
        } catch (const std::runtime_error&) {
            caught = true;
        }
        Owned::reject_assignment = false;
        CHECK(caught);
        CHECK(queue.size() == 1);
        CHECK(queue.tail_real_index() == tail);
        CHECK(*queue.back().value == 7);
        CHECK(Owned::live == 2);
        queue.pop_back(output);
        CHECK(*output.value == 7);
        CHECK(queue.empty());
    }
    CHECK(Owned::live == 0);
}

template<class Queue>
void check_move_only_ownership() {
    if constexpr (std::is_move_constructible<Queue>::value &&
                  std::is_move_assignable<Queue>::value) {
        Queue source(8);
        source.emplace_front(new int(11));
        source.emplace_back(new int(22));
        Queue moved(std::move(source));
        CHECK(source.empty());
        CHECK(*moved.front() == 11);
        Queue assigned(8);
        assigned.emplace_back(new int(99));
        assigned = std::move(moved);
        CHECK(moved.empty());
        CHECK(assigned.size() == 2);
        CHECK(*assigned.back() == 22);
        Queue& self = assigned;
        assigned = std::move(self);
        CHECK(assigned.size() == 2);
    } else {
        CHECK(false && "move-only elements must be supported");
    }
}

struct alignas(4096) AlignedElement {
    int value;
    explicit AlignedElement(int number) : value(number) {}
};

void check_over_aligned_storage() {
    using Queue = terark::circular_queue<AlignedElement, true>;
    Queue source(4);
    source.emplace_front(11);
    source.emplace_back(22);
    Queue copy(source);
    Queue moved(std::move(source));
    for (ptrdiff_t i = 0; i < 2; ++i) {
        CHECK(uintptr_t(&copy[i]) % alignof(AlignedElement) == 0);
        CHECK(uintptr_t(&moved[i]) % alignof(AlignedElement) == 0);
        CHECK(copy[i].value == moved[i].value);
    }
    Queue assigned(4);
    assigned.emplace_back(99);
    assigned = copy;
    CHECK(uintptr_t(&assigned.front()) % alignof(AlignedElement) == 0);
    assigned = std::move(moved);
    CHECK(uintptr_t(&assigned.front()) % alignof(AlignedElement) == 0);
}

#endif

int failures = 0;
int cases = 0;
const char* only_case = nullptr;

template<class Function>
void run(const char* name, Function function) {
    if (only_case && strcmp(only_case, name) != 0) {
        return;
    }
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

int main(int argc, char** argv) {
    if (argc > 2) {
        return 2;
    }
    only_case = argc == 2 ? argv[1] : nullptr;
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
    run("dynamic copy and assignment", check_copy_ownership<terark::circular_queue<Owned, true>>);
    run("dynamic false copy and assignment", check_copy_ownership<terark::circular_queue<Owned>>);
    run("fixed copy and assignment", check_copy_ownership<terark::fixed_circular_queue<Owned, 8>>);
    run("dynamic move and assignment", [] {
        check_move_ownership<terark::circular_queue<Owned, true>>(true);
    });
    run("dynamic false move and assignment", [] {
        check_move_ownership<terark::circular_queue<Owned>>(true);
    });
    run("fixed move and assignment", [] {
        check_move_ownership<terark::fixed_circular_queue<Owned, 8>>(false);
    });
    run("dynamic empty ownership", check_empty_ownership);
    run("fixed overwrite destruction", check_fixed_overwrite_destruction);
    run("dynamic copy exceptions", [] {
        check_copy_exceptions<terark::circular_queue<Owned, true>>(true);
    });
    run("fixed copy exceptions", [] {
        check_copy_exceptions<terark::fixed_circular_queue<Owned, 8>>(false);
    });
    run("fixed move exceptions",
        check_fixed_move_exceptions<terark::fixed_circular_queue<Owned, 8>>);
    run("dynamic pop_back assignment exception", check_pop_back_assignment_exception);
    run("dynamic move-only ownership", [] {
        check_move_only_ownership<terark::circular_queue<std::unique_ptr<int>, true>>();
    });
    run("fixed move-only ownership", [] {
        check_move_only_ownership<terark::fixed_circular_queue<std::unique_ptr<int>, 8>>();
    });
    run("dynamic over-aligned storage", check_over_aligned_storage);
#endif
    printf("%d cases, %d failures\n", cases, failures);
    return cases == 0 ? 2 : failures != 0;
}
