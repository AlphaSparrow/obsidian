#include "runtime/scheduler/task_scheduler.hpp"

namespace obsidian {
namespace runtime {
namespace scheduler {

TaskId TaskScheduler::ScheduleInterval(
    std::string name,
    int64_t start_time_ns,
    int64_t interval_ns,
    ScheduledCallback callback,
    CatchupPolicy catchup) {
    if (!callback || interval_ns <= 0) return 0;

    TaskId id = next_task_id_.fetch_add(1, std::memory_order_relaxed);

    auto task = std::make_shared<ScheduledTask>();
    task->task_id = id;
    task->name = std::move(name);
    task->next_run_time_ns = start_time_ns;
    task->interval_ns = interval_ns;
    task->policy = TriggerPolicy::Interval;
    task->catchup = catchup;
    task->callback = std::move(callback);
    task->is_cancelled = false;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        task_map_[id] = task;
        task_queue_.push(task);
    }

    tasks_scheduled_.fetch_add(1, std::memory_order_relaxed);
    return id;
}

TaskId TaskScheduler::ScheduleDelayed(
    std::string name,
    int64_t execute_at_ns,
    ScheduledCallback callback) {
    if (!callback) return 0;

    TaskId id = next_task_id_.fetch_add(1, std::memory_order_relaxed);

    auto task = std::make_shared<ScheduledTask>();
    task->task_id = id;
    task->name = std::move(name);
    task->next_run_time_ns = execute_at_ns;
    task->interval_ns = 0;
    task->policy = TriggerPolicy::OneShot;
    task->callback = std::move(callback);
    task->is_cancelled = false;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        task_map_[id] = task;
        task_queue_.push(task);
    }

    tasks_scheduled_.fetch_add(1, std::memory_order_relaxed);
    return id;
}

bool TaskScheduler::Cancel(TaskId id) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = task_map_.find(id);
    if (it != task_map_.end()) {
        it->second->is_cancelled = true;
        task_map_.erase(it);
        tasks_cancelled_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

size_t TaskScheduler::Step(int64_t current_time_ns) {
    std::vector<std::shared_ptr<ScheduledTask>> due_tasks;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!task_queue_.empty()) {
            auto top = task_queue_.top();
            if (top->is_cancelled) {
                task_queue_.pop();
                continue;
            }

            if (top->next_run_time_ns > current_time_ns) {
                break; // Earliest task is in the future
            }

            task_queue_.pop();
            due_tasks.push_back(top);
        }
    }

    size_t executed_count = 0;
    std::vector<std::shared_ptr<ScheduledTask>> reschedule_tasks;

    for (auto& task : due_tasks) {
        if (task->is_cancelled) continue;

        try {
            task->callback();
            executed_count++;
            tasks_executed_.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
            // Defensive fault isolation
        }

        if (task->policy == TriggerPolicy::Interval && !task->is_cancelled) {
            if (task->catchup == CatchupPolicy::SkipMissed) {
                // Advance past current time
                while (task->next_run_time_ns <= current_time_ns) {
                    task->next_run_time_ns += task->interval_ns;
                }
            } else {
                task->next_run_time_ns += task->interval_ns;
            }
            reschedule_tasks.push_back(task);
        }
    }

    if (!reschedule_tasks.empty()) {
        std::unique_lock<std::mutex> lock(mutex_);
        for (auto& task : reschedule_tasks) {
            if (!task->is_cancelled && task_map_.find(task->task_id) != task_map_.end()) {
                task_queue_.push(task);
            }
        }
    }

    return executed_count;
}

void TaskScheduler::Clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    task_map_.clear();
    while (!task_queue_.empty()) {
        task_queue_.pop();
    }
}

size_t TaskScheduler::PendingCount() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return task_map_.size();
}

SchedulerMetrics TaskScheduler::GetMetrics() const {
    SchedulerMetrics m;
    m.tasks_scheduled = tasks_scheduled_.load(std::memory_order_relaxed);
    m.tasks_executed = tasks_executed_.load(std::memory_order_relaxed);
    m.tasks_missed = tasks_missed_.load(std::memory_order_relaxed);
    m.tasks_cancelled = tasks_cancelled_.load(std::memory_order_relaxed);

    std::unique_lock<std::mutex> lock(mutex_);
    m.pending_task_count = task_map_.size();
    return m;
}

} // namespace scheduler
} // namespace runtime
} // namespace obsidian
