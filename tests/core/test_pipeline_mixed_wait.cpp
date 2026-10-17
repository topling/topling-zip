// Exercise the production queue with several fibers sharing each operating system thread.
//Makefile: CXXFLAGS: -ffunction-sections -fdata-sections
//Makefile: LDFLAGS: -Wl,--gc-sections
#include "../../src/terark/thread/pipeline.cpp"

#include <atomic>
#include <condition_variable>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TOPLING_PIPELINE_WITH_FIBER
namespace {
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;
using Item = terark::PipelineQueueItem;

class Deadline {
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    std::thread thread;
public:
    Deadline() : thread([this] {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(5), [&] { return finished; })) {
            fprintf(stderr, "mixed queue regression exceeded its overall deadline\n");
            fflush(nullptr);
            _Exit(2);
        }
    }) {}
    ~Deadline() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            finished = true;
        }
        condition.notify_one();
        thread.join();
    }
};

bool empty_wait(size_t capacity) {
    terark::MixedQueue queue(capacity);
    terark::FiberYield fy;
    Item output;
    bool popped = false;
    boost::fibers::fiber consumer([&] { popped = queue.pop_front(output, 200, &fy); });
    boost::fibers::fiber producer([&] {
        // The old queue yields once and then blocks this thread before we can resume.
        boost::this_fiber::yield();
        queue.push_back(Item(42, nullptr), &fy);
    });
    consumer.join();
    producer.join();
    bool ok = popped && output.plserial == 42 && queue.empty();
    printf("empty wait capacity=%zu: %s\n", capacity, ok ? "passed" : "FAILED");
    return ok;
}

bool full_wait(size_t capacity, bool timed) {
    terark::MixedQueue queue(capacity);
    terark::FiberYield fy;
    for (size_t i = 0; i < capacity; ++i) queue.push_back(Item(i, nullptr), &fy);
    bool ok = queue.size() == capacity && !queue.push_back(Item(99, nullptr), 0, &fy);
    bool pushed = false;
    Item output;
    boost::fibers::fiber producer([&] {
        if (timed) {
            pushed = queue.push_back(Item(capacity, nullptr), 200, &fy);
        } else {
            queue.push_back(Item(capacity, nullptr), &fy);
            pushed = true;
        }
    });
    boost::fibers::fiber consumer([&] {
        boost::this_fiber::yield();
        ok = queue.pop_front(output, 200, &fy) && output.plserial == 0 && ok;
    });
    producer.join();
    consumer.join();
    ok = pushed && queue.size() == capacity && ok;
    for (size_t i = 1; i <= capacity; ++i) {
        ok = queue.pop_front(output, 0, &fy) && output.plserial == i && ok;
    }
    ok = queue.empty() && ok;
    printf("%s full wait capacity=%zu: %s\n", timed ? "timed" : "untimed", capacity,
           ok ? "passed" : "FAILED");
    return ok;
}

bool timeout_wait(bool full) {
    terark::MixedQueue queue(1);
    terark::FiberYield fy;
    if (full) queue.push_back(Item(1, nullptr), &fy);
    bool timer_ran = false;
    bool sibling_progressed = false;
    bool result = true;
    long elapsed = 0;
    boost::fibers::fiber waiter([&] {
        Item output;
        const auto start = Clock::now();
        result = full ? queue.push_back(Item(2, nullptr), 30, &fy)
                      : queue.pop_front(output, 30, &fy);
        elapsed = std::chrono::duration_cast<Milliseconds>(Clock::now() - start).count();
        sibling_progressed = timer_ran;
    });
    boost::fibers::fiber timer([&] {
        boost::this_fiber::sleep_for(Milliseconds(1));
        timer_ran = true;
    });
    waiter.join();
    timer.join();
    bool ok = !result && elapsed >= 25 && sibling_progressed;
    printf("%s timeout: %s, elapsed=%ld ms, sibling=%d\n", full ? "full" : "empty",
           ok ? "passed" : "FAILED", elapsed, sibling_progressed);
    return ok;
}

bool cross_thread(size_t capacity) {
    terark::MixedQueue queue(capacity);
    std::atomic<unsigned> errors{0};
    std::atomic<unsigned> started{0};
    auto run = [&](bool produce) {
        terark::FiberYield fy;
        bool finished = false;
        unsigned diagnostic_runs = 0;
        started.fetch_add(1);
        while (started.load() != 2) std::this_thread::yield();
        boost::fibers::fiber worker([&] {
            for (size_t i = 0; i < 4096; ++i) {
                if (produce) {
                    if (!queue.push_back(Item(i, nullptr), 500, &fy)) ++errors;
                } else {
                    Item output;
                    if (!queue.pop_front(output, 500, &fy) || output.plserial != i) ++errors;
                }
            }
            finished = true;
        });
        boost::fibers::fiber diagnostics([&] {
            do {
                const terark::MixedQueue& constant = queue;
                if (queue.size() > capacity || constant.peekSize() > capacity) ++errors;
                queue.empty();
                ++diagnostic_runs;
                boost::this_fiber::sleep_for(Milliseconds(1));
            } while (!finished);
        });
        worker.join();
        diagnostics.join();
        if (!diagnostic_runs) ++errors;
    };
    std::thread producer(run, true);
    std::thread consumer(run, false);
    producer.join();
    consumer.join();
    bool ok = errors.load() == 0 && queue.empty();
    printf("cross-thread FIFO capacity=%zu: %s\n", capacity, ok ? "passed" : "FAILED");
    return ok;
}
} // namespace
#endif

int main(int argc, char** argv) {
#if TOPLING_PIPELINE_WITH_FIBER
    Deadline deadline;
    bool ok = true;
    auto selected = [&](const char* name) { return argc == 1 || strcmp(argv[1], name) == 0; };
    for (size_t capacity : {size_t(1), size_t(3), size_t(4)}) {
        if (selected("empty")) ok = empty_wait(capacity) && ok;
        if (selected("full")) ok = full_wait(capacity, true) && ok;
        if (selected("untimed")) ok = full_wait(capacity, false) && ok;
        if (selected("cross")) ok = cross_thread(capacity) && ok;
    }
    if (selected("timeout")) {
        ok = timeout_wait(false) && ok;
        ok = timeout_wait(true) && ok;
    }
    return ok ? 0 : 1;
#else
    printf("mixed queue waits: skipped (fiber support disabled)\n");
    return 0;
#endif
}
