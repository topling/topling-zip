// Exercise the public enqueue API with producers sharing one OS thread.
//Makefile: CXXFLAGS: -ffunction-sections -fdata-sections
//Makefile: LDFLAGS: -Wl,--gc-sections
#include "../../src/terark/thread/pipeline.cpp"

#include <condition_variable>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#if TOPLING_PIPELINE_WITH_FIBER
namespace {
struct Task : terark::PipelineTask {
    int id;
    explicit Task(int id) : id(id) {}
};

bool check_enqueue(int capacity, bool first_batch, bool second_batch, int log_level) {
    std::mutex mutex;
    std::condition_variable condition;
    bool consumer_entered = false;
    bool second_entered = false;
    bool release_consumer = false;
    bool finished = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lock(mutex);
        if (condition.wait_until(lock, deadline, [&] { return second_entered; })) {
            release_consumer = true;
            condition.notify_all();
            if (condition.wait_until(lock, deadline, [&] { return finished; })) return;
        }
        fprintf(stderr, "mixed enqueue stalled: capacity=%d batches=%d/%d log=%d\n",
                capacity, first_batch, second_batch, log_level);
        fflush(nullptr);
        _Exit(2);
    });

    std::vector<std::pair<uintptr_t, int>> received;
    terark::PipelineProcessor pipeline;
    pipeline.setEUType(terark::PipelineProcessor::EUType::mixed);
    pipeline.setQueueSize(capacity);
    pipeline.setQueueTimeout(20);
    pipeline.setLogLevel(log_level);
    pipeline | new terark::FunPipelineStage(1, 1,
        [&](terark::PipelineStage*, int, terark::PipelineQueueItem* item) {
            auto task = static_cast<Task*>(item->task);
            if (task->id == 0) {
                std::unique_lock<std::mutex> lock(mutex);
                consumer_entered = true;
                condition.notify_all();
                condition.wait(lock, [&] { return release_consumer; });
            }
            received.emplace_back(item->plserial, task->id);
        }, "enqueue-order");
    pipeline.compile();
    pipeline.enqueue(new Task(0));
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return consumer_entered; });
    }
    // Fill every slot while the consumer is held at its gate.
    for (int id = 1; id <= capacity; ++id) pipeline.enqueue(new Task(id));

    auto produce = [&](int first_id, int count, bool batch) {
        if (batch) {
            std::vector<terark::PipelineTask*> tasks;
            for (int i = 0; i < count; ++i) tasks.push_back(new Task(first_id + i));
            pipeline.enqueue(tasks.data(), tasks.size());
        } else {
            pipeline.enqueue(new Task(first_id));
        }
    };
    const int first_count = first_batch ? capacity + 2 : 1;
    const int second_count = second_batch ? capacity + 1 : 1;
    // Dispatch runs the first producer until its full-queue wait yields with the input lock held.
    boost::fibers::fiber first(boost::fibers::launch::dispatch, [&] {
        produce(100, first_count, first_batch);
    });
    boost::fibers::fiber second(boost::fibers::launch::dispatch, [&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            second_entered = true;
        }
        condition.notify_all();
        produce(200, second_count, second_batch);
    });
    first.join();
    second.join();
    pipeline.stop();
    pipeline.wait();
    {
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
    }
    condition.notify_all();
    watchdog.join();

    std::vector<int> expected;
    for (int id = 0; id <= capacity; ++id) expected.push_back(id);
    for (int i = 0; i < first_count; ++i) expected.push_back(100 + i);
    for (int i = 0; i < second_count; ++i) expected.push_back(200 + i);
    bool ok = received.size() == expected.size();
    for (size_t i = 0; i < received.size() && i < expected.size(); ++i) {
        ok = received[i].first == i + 1 && received[i].second == expected[i] && ok;
    }
    printf("mixed enqueue capacity=%d batches=%d/%d log=%d: %s\n",
           capacity, first_batch, second_batch, log_level, ok ? "passed" : "FAILED");
    return ok;
}
} // namespace
#endif

int main(int argc, char** argv) {
#if TOPLING_PIPELINE_WITH_FIBER
    // The optional mask permits each public overload to be tested separately on the old code.
    int selected = argc > 1 ? atoi(argv[1]) : -1;
    bool ok = true;
    for (int capacity : {1, 3, 4}) {
        for (int mask = 0; mask < 4; ++mask) {
            if (selected >= 0 && selected != mask) continue;
            for (int log_level : {0, 3}) {
                ok = check_enqueue(capacity, mask & 1, mask & 2, log_level) && ok;
            }
        }
    }
    return ok ? 0 : 1;
#else
    puts("mixed enqueue: skipped (fiber support disabled)");
    return 0;
#endif
}
