// Exercise the private production queue classes directly. Build with function/data sections and
// --gc-sections so unrelated pipeline operations do not add dependencies to this regression test.
//Makefile: CXXFLAGS: -ffunction-sections -fdata-sections
//Makefile: LDFLAGS: -Wl,--gc-sections
#include "../../src/terark/thread/pipeline.cpp"

#include <atomic>
#include <stdio.h>

int main() {
#if TOPLING_PIPELINE_WITH_FIBER
    terark::MixedQueue queue(8);
    std::atomic<unsigned> errors{0};
    std::atomic<bool> finished{false};
    std::thread producer([&] {
        terark::FiberYield fy;
        for (unsigned i = 0; i < 5000; i++) {
            terark::PipelineQueueItem item(i, nullptr);
            if (i & 1) {
                while (!queue.push_back(item, 100, &fy)) std::this_thread::yield();
            } else {
                queue.push_back(item, &fy);
            }
        }
    });
    std::thread consumer([&] {
        terark::FiberYield fy;
        terark::PipelineQueueItem output;
        for (unsigned i = 0; i < 5000; i++) {
            while (!queue.pop_front(output, 100, &fy)) std::this_thread::yield();
            if (output.plserial != i) errors.fetch_add(1);
        }
        finished.store(true);
    });
    const terark::MixedQueue& constant = queue;
    while (!finished.load()) {
        if (queue.size() > 8 || constant.peekSize() > 8) errors.fetch_add(1);
        queue.empty();
        std::this_thread::yield();
    }
    producer.join();
    consumer.join();
    if (!queue.empty() || queue.size() != 0) errors.fetch_add(1);
    printf("mixed queue: %u errors\n", errors.load());
    return errors.load() ? 1 : 0;
#else
    puts("mixed queue: skipped (fiber support disabled)");
    return 0;
#endif
}
