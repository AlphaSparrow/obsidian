#pragma once

#include "atomic_utils.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

namespace obsidian {
namespace kernel {
namespace concurrency {

enum class TaskPriority : uint8_t {
    RealTime = 0,   // Critical sub-millisecond execution tasks (market orders, risk checks)
    Normal   = 1,   // Standard pipeline & processing tasks
    Low      = 2    // Background analytical, logging & telemetry tasks
};

struct ScheduledTask {
    std::function<void()> work;
    std::chrono::steady_clock::time_point deadline;
    TaskPriority priority;
    uint64_t sequence_id;

    bool operator>(const ScheduledTask& other) const noexcept {
        if (deadline != other.deadline) {
            return deadline > other.deadline;
        }
        if (priority != other.priority) {
            return static_cast<uint8_t>(priority) > static_cast<uint8_t>(other.priority);
        }
        return sequence_id > other.sequence_id;
    }
};

/// Multi-Lane Latency-Budgeted Task Scheduler.
///
/// Features:
/// - Priority lanes (RealTime, Normal, Low) minimizing inversion.
/// - Fine-grained spinlock isolation for delayed vs immediate queues.
/// - Millisecond / microsecond deadline resolution.
class TaskScheduler {
public:
    TaskScheduler();
    ~TaskScheduler() = default;

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    /// Schedules an immediate task with the specified priority.
    void schedule(std::function<void()> task, TaskPriority priority = TaskPriority::Normal);

    /// Schedules a delayed task to execute after the given delay duration.
    template <typename Rep, typename Period>
    void schedule_delayed(std::function<void()> task,
                          std::chrono::duration<Rep, Period> delay,
                          TaskPriority priority = TaskPriority::Normal) {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::nanoseconds>(delay);
        schedule_at(std::move(task), deadline, priority);
    }

    /// Schedules a task to execute at an absolute time point.
    void schedule_at(std::function<void()> task,
                     std::chrono::steady_clock::time_point deadline,
                     TaskPriority priority = TaskPriority::Normal);

    /// Polls and runs ready tasks up to current timestamp.
    /// RealTime tasks are executed first, then Normal, then Low.
    /// Returns the count of tasks executed.
    size_t poll_ready(size_t max_tasks = 64);

    /// Returns the count of pending tasks across all queues.
    OBSIDIAN_NODISCARD size_t pending_tasks() const noexcept;

    /// Returns the total tasks successfully executed by this scheduler.
    OBSIDIAN_NODISCARD uint64_t executed_tasks() const noexcept {
        return total_executed_.load(std::memory_order_relaxed);
    }

private:
    alignas(kCacheLineSize) mutable SpinLock lock_;
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, std::greater<ScheduledTask>> delayed_queue_;

    // Fast-path immediate priority lanes
    std::vector<std::function<void()>> realtime_lane_;
    std::vector<std::function<void()>> normal_lane_;
    std::vector<std::function<void()>> low_lane_;

    uint64_t next_sequence_{0};
    std::atomic<uint64_t> total_executed_{0};
};

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
