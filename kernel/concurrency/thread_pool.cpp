#include "thread_pool.hpp"

namespace obsidian {
namespace kernel {
namespace concurrency {

namespace {
thread_local int tl_worker_id = -1;
}

ThreadPool::ThreadPool(size_t thread_count)
    : thread_count_(thread_count == 0 ? Thread::hardware_concurrency() : thread_count),
      stopping_(false),
      global_queue_(thread_count_ * 256) {
#if defined(_WIN32)
    wake_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
#endif

    local_queues_.reserve(thread_count_);
    for (size_t i = 0; i < thread_count_; ++i) {
        local_queues_.emplace_back(new WorkStealingDeque<Task>(1024));
    }

    workers_.reserve(thread_count_);
    for (size_t i = 0; i < thread_count_; ++i) {
        workers_.emplace_back([this, i]() {
            worker_loop(i);
        });
        workers_.back().set_affinity(i);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
#if defined(_WIN32)
    if (wake_event_) {
        CloseHandle(wake_event_);
        wake_event_ = nullptr;
    }
#endif
}

void ThreadPool::submit_internal(Task task) {
    if (stopping_.load(std::memory_order_relaxed)) {
        return;
    }

    if (tl_worker_id >= 0 && static_cast<size_t>(tl_worker_id) < thread_count_) {
        if (local_queues_[tl_worker_id]->push_bottom(std::move(task))) {
#if defined(_WIN32)
            if (idle_workers_.load(std::memory_order_relaxed) > 0) {
                SetEvent(wake_event_);
            }
#endif
            return;
        }
    }

    while (!global_queue_.enqueue(task)) {
        if (stopping_.load(std::memory_order_relaxed)) {
            return;
        }
        cpu_pause();
    }

#if defined(_WIN32)
    if (idle_workers_.load(std::memory_order_relaxed) > 0) {
        SetEvent(wake_event_);
    }
#endif
}

void ThreadPool::shutdown() {
    bool expected = false;
    if (stopping_.compare_exchange_strong(expected, true, std::memory_order_release)) {
#if defined(_WIN32)
        if (wake_event_) {
            SetEvent(wake_event_);
        }
#endif
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }
}

void ThreadPool::worker_loop(size_t worker_id) {
    tl_worker_id = static_cast<int>(worker_id);
    WorkStealingDeque<Task>& my_deque = *local_queues_[worker_id];

    uint32_t rng_state = static_cast<uint32_t>(worker_id * 1664525u + 1013904223u + read_tsc());
    auto fast_rand = [&rng_state](uint32_t max_val) -> uint32_t {
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 17;
        rng_state ^= rng_state << 5;
        return max_val > 0 ? (rng_state % max_val) : 0;
    };

    SpinWait spin_wait;
    const uint32_t max_spins = 64;

    while (!stopping_.load(std::memory_order_relaxed)) {
        Task task;

        // 1. Pop from local deque (LIFO - cache warmth)
        if (my_deque.pop_bottom(task)) {
            task();
            spin_wait.reset();
            continue;
        }

        // 2. Pop from global injection queue (FIFO)
        if (global_queue_.dequeue(task)) {
            task();
            spin_wait.reset();
            continue;
        }

        // 3. Steal work from peer workers (FIFO)
        bool stolen = false;
        if (thread_count_ > 1) {
            const size_t victim_start = fast_rand(static_cast<uint32_t>(thread_count_));
            for (size_t attempt = 0; attempt < thread_count_; ++attempt) {
                const size_t victim = (victim_start + attempt) % thread_count_;
                if (victim != worker_id && local_queues_[victim]->steal_top(task)) {
                    stolen = true;
                    break;
                }
            }
        }

        if (stolen) {
            task();
            spin_wait.reset();
            continue;
        }

        // 4. Spin wait backoff
        if (spin_wait.count() < max_spins) {
            spin_wait.spin();
            continue;
        }

        // 5. Park idle worker on event
        idle_workers_.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32)
        if (wake_event_ && !stopping_.load(std::memory_order_relaxed)) {
            WaitForSingleObject(wake_event_, 1);
        }
#else
        thread_yield();
#endif
        idle_workers_.fetch_sub(1, std::memory_order_relaxed);
        spin_wait.reset();
    }
}

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
