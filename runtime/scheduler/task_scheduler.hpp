#pragma once

#include "storage/common/status.hpp"

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace obsidian {
namespace runtime {
namespace scheduler {

using TaskId = uint64_t;
using ScheduledCallback = std::function<storage::Status()>;

enum class TriggerPolicy : uint8_t {
    OneShot = 0,
    Interval = 1,
    MarketTick = 2
};

enum class CatchupPolicy : uint8_t {
    SkipMissed = 0,
    ExecuteAll = 1
};

struct ScheduledTask {
    TaskId task_id{0};
    std::string name;
    int64_t next_run_time_ns{0};
    int64_t interval_ns{0};
    TriggerPolicy policy{TriggerPolicy::OneShot};
    CatchupPolicy catchup{CatchupPolicy::SkipMissed};
    ScheduledCallback callback;
    bool is_cancelled{false};
};

struct SchedulerMetrics {
    uint64_t tasks_scheduled{0};
    uint64_t tasks_executed{0};
    uint64_t tasks_missed{0};
    uint64_t tasks_cancelled{0};
    size_t pending_task_count{0};
};

class TaskScheduler {
public:
    TaskScheduler() = default;
    ~TaskScheduler() = default;

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    // Schedules a periodic interval task
    TaskId ScheduleInterval(
        std::string name,
        int64_t start_time_ns,
        int64_t interval_ns,
        ScheduledCallback callback,
        CatchupPolicy catchup = CatchupPolicy::SkipMissed);

    // Schedules a one-shot delayed task
    TaskId ScheduleDelayed(
        std::string name,
        int64_t execute_at_ns,
        ScheduledCallback callback);

    // Cancels a task by ID
    bool Cancel(TaskId id);

    // Advances scheduler clock to current_time_ns and executes all due tasks
    // Returns count of tasks executed
    size_t Step(int64_t current_time_ns);

    // Clears all scheduled tasks
    void Clear();

    [[nodiscard]] size_t PendingCount() const;
    [[nodiscard]] SchedulerMetrics GetMetrics() const;

private:
    struct TaskComparator {
        bool operator()(const std::shared_ptr<ScheduledTask>& a, const std::shared_ptr<ScheduledTask>& b) const {
            return a->next_run_time_ns > b->next_run_time_ns; // Min-heap by run time
        }
    };

    mutable std::mutex mutex_;
    std::atomic<TaskId> next_task_id_{1};

    std::priority_queue<std::shared_ptr<ScheduledTask>, 
                        std::vector<std::shared_ptr<ScheduledTask>>, 
                        TaskComparator> task_queue_;
    std::unordered_map<TaskId, std::shared_ptr<ScheduledTask>> task_map_;

    // Telemetry
    std::atomic<uint64_t> tasks_scheduled_{0};
    std::atomic<uint64_t> tasks_executed_{0};
    std::atomic<uint64_t> tasks_missed_{0};
    std::atomic<uint64_t> tasks_cancelled_{0};
};

} // namespace scheduler
} // namespace runtime
} // namespace obsidian
