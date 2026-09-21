#include "task_scheduler.hpp"

namespace obsidian {
namespace kernel {
namespace concurrency {

TaskScheduler::TaskScheduler() : next_sequence_(0) {}

void TaskScheduler::schedule(std::function<void()> task, TaskPriority priority) {
    if (!task) return;

    SpinLockGuard guard(lock_);
    switch (priority) {
        case TaskPriority::RealTime:
            realtime_lane_.push_back(std::move(task));
            break;
        case TaskPriority::Normal:
            normal_lane_.push_back(std::move(task));
            break;
        case TaskPriority::Low:
            low_lane_.push_back(std::move(task));
            break;
    }
}

void TaskScheduler::schedule_at(std::function<void()> task,
                                std::chrono::steady_clock::time_point deadline,
                                TaskPriority priority) {
    if (!task) return;

    SpinLockGuard guard(lock_);
    delayed_queue_.push(ScheduledTask{
        std::move(task),
        deadline,
        priority,
        ++next_sequence_
    });
}

size_t TaskScheduler::poll_ready(size_t max_tasks) {
    const auto now = std::chrono::steady_clock::now();
    size_t executed = 0;

    // Buffer to hold popped tasks to execute outside the lock to minimize lock contention
    std::vector<std::function<void()>> batch;
    batch.reserve(max_tasks);

    {
        SpinLockGuard guard(lock_);

        // 1. Drain matured delayed tasks into priority lanes
        while (!delayed_queue_.empty() && delayed_queue_.top().deadline <= now) {
            auto& top_task = const_cast<ScheduledTask&>(delayed_queue_.top());
            switch (top_task.priority) {
                case TaskPriority::RealTime:
                    realtime_lane_.push_back(std::move(top_task.work));
                    break;
                case TaskPriority::Normal:
                    normal_lane_.push_back(std::move(top_task.work));
                    break;
                case TaskPriority::Low:
                    low_lane_.push_back(std::move(top_task.work));
                    break;
            }
            delayed_queue_.pop();
        }

        // 2. Fetch tasks in priority order: RealTime -> Normal -> Low
        auto extract_lane = [&](std::vector<std::function<void()>>& lane) {
            while (!lane.empty() && batch.size() < max_tasks) {
                batch.push_back(std::move(lane.back()));
                lane.pop_back();
            }
        };

        extract_lane(realtime_lane_);
        if (batch.size() < max_tasks) {
            extract_lane(normal_lane_);
        }
        if (batch.size() < max_tasks) {
            extract_lane(low_lane_);
        }
    }

    // 3. Execute tasks with lock released!
    for (auto& task : batch) {
        if (task) {
            task();
            ++executed;
        }
    }

    total_executed_.fetch_add(executed, std::memory_order_relaxed);
    return executed;
}

size_t TaskScheduler::pending_tasks() const noexcept {
    SpinLockGuard guard(lock_);
    return delayed_queue_.size() + realtime_lane_.size() + normal_lane_.size() + low_lane_.size();
}

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
