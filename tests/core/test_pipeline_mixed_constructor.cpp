// Exercise the private production execution units without unrelated pipeline link dependencies.
//Makefile: CXXFLAGS: -ffunction-sections -fdata-sections
//Makefile: LDFLAGS: -Wl,--gc-sections
#include "../../src/terark/thread/pipeline.cpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <stdio.h>

#if TOPLING_PIPELINE_WITH_FIBER
namespace {

struct ConstructorGate {
    std::atomic<bool> worker_started{false};
    std::atomic<bool> gate_timed_out{false};
    std::chrono::steady_clock::time_point deadline;

    explicit ConstructorGate(std::chrono::steady_clock::time_point deadline) : deadline(deadline) {}
};

class GatedMakeFunc {
    std::shared_ptr<ConstructorGate> gate;
    bool wait_on_destroy = false;

public:
    explicit GatedMakeFunc(std::shared_ptr<ConstructorGate> gate) : gate(std::move(gate)) {}
    GatedMakeFunc(const GatedMakeFunc& other) : gate(other.gate) {}
    GatedMakeFunc(GatedMakeFunc&& other) noexcept : gate(other.gate) {
        other.wait_on_destroy = true;
    }
    ~GatedMakeFunc() {
        if (!wait_on_destroy) return;
        while (!gate->worker_started.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() >= gate->deadline) {
                gate->gate_timed_out.store(true, std::memory_order_relaxed);
                return;
            }
            std::this_thread::yield();
        }
    }
    auto operator()() const {
        return [gate = gate] {
            gate->worker_started.store(true, std::memory_order_release);
        };
    }
};

} // namespace
#endif

int main() {
#if TOPLING_PIPELINE_WITH_FIBER
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (int iteration = 0; iteration < 32; ++iteration) {
        auto gate = std::make_shared<ConstructorGate>(deadline);
        GatedMakeFunc make_func(gate);
        // Moving the launch callable into std::thread marks its source temporary. Its destructor
        // waits until the worker callback runs, before the constructor can advance any further.
        terark::MixedExecUnit unit(1, make_func);
        unit.join();
        if (gate->gate_timed_out.load(std::memory_order_relaxed)) {
            fprintf(stderr, "mixed constructor: worker gate timed out at %d\n", iteration);
            return 1;
        }
        if (unit.get_fiber_yield()->sched() == nullptr) {
            fprintf(stderr, "mixed constructor: worker initialization overwritten at %d\n",
                    iteration);
            return 1;
        }
    }
    puts("mixed constructor: 32 ordered constructions passed");
#else
    puts("mixed constructor: skipped (pipeline fibers disabled)");
#endif
    return 0;
}
