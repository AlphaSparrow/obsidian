#include "atomic_utils.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace obsidian {
namespace kernel {
namespace concurrency {

/// Chase-Lev Lock-Free Work-Stealing Deque.
///
/// DDIA Workload Parallelization & Distributed Dataflow (Chapter 10/11):
/// Dynamic task load-balancing across worker threads with minimal synchronization.
///
/// Principles:
///   1. Owner Thread (Bottom): Pushes and pops tasks LIFO in O(1) wait-free time.
///      LIFO maximizes temporal cache locality, ensuring recently created data
///      resides hot in CPU L1/L2 caches.
///   2. Thief Threads (Top): Steal tasks FIFO in O(1) lock-free time via CAS.
///      FIFO steals the oldest tasks at the root of sub-graphs, maximizing stolen work.
class WorkStealingDeque {
public:
    using Task = std::function<void()>;

    explicit WorkStealingDeque(size_t initial_capacity = 1024)
        : mask_(initial_capacity - 1),
          buffer_(new std::atomic<Task*>[initial_capacity]) {
        top_.store(0, std::memory_order_relaxed);
        bottom_.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < initial_capacity; ++i) {
            buffer_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    ~WorkStealingDeque() {
        const int64_t b = bottom_.load(std::memory_order_relaxed);
        const int64_t t = top_.load(std::memory_order_relaxed);
        for (int64_t i = t; i < b; ++i) {
            const size_t idx = static_cast<size_t>(i) & mask_;
            Task* task = buffer_[idx].load(std::memory_order_relaxed);
            delete task;
        }
    }

    WorkStealingDeque(const WorkStealingDeque&) = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    void push_bottom(Task task) {
        const int64_t b = bottom_.load(std::memory_order_relaxed);
        const int64_t t = top_.load(std::memory_order_acquire);

        if (b - t > static_cast<int64_t>(mask_)) {
            task();
            return;
        }

        const size_t idx = static_cast<size_t>(b) & mask_;
        buffer_[idx].store(new Task(std::move(task)), std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
    }

    bool pop_bottom(Task& task) {
        const int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = top_.load(std::memory_order_relaxed);

        if (t <= b) {
            const size_t idx = static_cast<size_t>(b) & mask_;
            Task* task_ptr = buffer_[idx].load(std::memory_order_relaxed);
            if (t == b) {
                if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    bottom_.store(b + 1, std::memory_order_relaxed);
                    return false;
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }
            task = std::move(*task_ptr);
            delete task_ptr;
            return true;
        }

        bottom_.store(b + 1, std::memory_order_relaxed);
        return false;
    }

    bool steal_top(Task& task) {
        int64_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const int64_t b = bottom_.load(std::memory_order_acquire);

        if (t < b) {
            const size_t idx = static_cast<size_t>(t) & mask_;
            Task* task_ptr = buffer_[idx].load(std::memory_order_relaxed);
            if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return false;
            }
            task = std::move(*task_ptr);
            delete task_ptr;
            return true;
        }

        return false;
    }

    OBSIDIAN_NODISCARD bool empty() const noexcept {
        const int64_t b = bottom_.load(std::memory_order_relaxed);
        const int64_t t = top_.load(std::memory_order_relaxed);
        return b <= t;
    }

private:
    const size_t mask_;
    std::unique_ptr<std::atomic<Task*>[]> buffer_;

    alignas(kCacheLineSize) std::atomic<int64_t> top_{0};
    alignas(kCacheLineSize) std::atomic<int64_t> bottom_{0};
};

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
