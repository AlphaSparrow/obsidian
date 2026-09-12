#include "atomic_utils.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

namespace obsidian {
namespace kernel {
namespace concurrency {

enum class TaskPriority : uint8_t {
    RealTime = 0,   // Critical sub-millisecond execution tasks
    Normal   = 1,   // Standard application tasks
    Low      = 2    // Background analytical & telemetry tasks
};

struct ScheduledTask {
    std::function<void()> work;
    std::chrono::steady_clock::time_point deadline;
    TaskPriority priority;
    uint64_t sequence_id;

    bool operator>(const ScheduledTask& other) const noexcept {
        if (priority != other.priority) {
            return static_cast<uint8_t>(priority) > static_cast<uint8_t>(other.priority);
        }
        if (deadline != other.deadline) {
            return deadline > other.deadline;
        }
        return sequence_id > other.sequence_id;
    }
};

/// Latency-Budgeted Task Scheduler.
///
/// DDIA Chapter 1 (Service Level Objectives & Tail-Latency Management):
/// Prioritizes hard-deadline real-time transactions ahead of batch computations,
/// preventing tail-latency SLA violations under heavy system load.
/// Employs non-blocking SpinLock synchronization to avoid OS kernel scheduler hops.
class TaskScheduler {
public:
    TaskScheduler() : next_sequence_(0) {}

    void schedule(std::function<void()> task, TaskPriority priority = TaskPriority::Normal) {
        schedule_delayed(std::move(task), std::chrono::milliseconds(0), priority);
    }

    void schedule_delayed(std::function<void()> task, std::chrono::milliseconds delay, TaskPriority priority = TaskPriority::Normal) {
        const auto deadline = std::chrono::steady_clock::now() + delay;
        SpinLockGuard lock(spinlock_);
        task_queue_.push(ScheduledTask{
            std::move(task),
            deadline,
            priority,
            ++next_sequence_
        });
    }

    /// Polls and runs all ready tasks up to current timestamp.
    /// Returns the count of tasks executed.
    size_t poll_ready(size_t max_tasks = 64) {
        const auto now = std::chrono::steady_clock::now();
        size_t executed = 0;

        while (executed < max_tasks) {
            std::function<void()> task_to_run;
            {
                SpinLockGuard lock(spinlock_);
                if (task_queue_.empty()) {
                    break;
                }

                if (task_queue_.top().deadline > now) {
                    break;
                }

                task_to_run = std::move(const_cast<ScheduledTask&>(task_queue_.top()).work);
                task_queue_.pop();
            }

            if (task_to_run) {
                task_to_run();
                ++executed;
            }
        }

        return executed;
    }

    OBSIDIAN_NODISCARD size_t pending_tasks() {
        SpinLockGuard lock(spinlock_);
        return task_queue_.size();
    }

private:
    SpinLock spinlock_;
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, std::greater<ScheduledTask>> task_queue_;
    uint64_t next_sequence_;
};

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
