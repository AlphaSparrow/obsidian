#pragma once

#include "runtime/dependency_graph/task_node.hpp"
#include "storage/common/status.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>

namespace obsidian {
namespace runtime {
namespace dependency_graph {

class DependencyGraph {
public:
    DependencyGraph() = default;
    ~DependencyGraph() = default;

    DependencyGraph(const DependencyGraph&) = delete;
    DependencyGraph& operator=(const DependencyGraph&) = delete;

    // Adds a task node to the graph
    storage::Status AddTask(TaskNode task);

    // Declares that 'downstream' task depends on 'upstream' task
    storage::Status AddDependency(const TaskId& upstream, const TaskId& downstream);

    // Checks if the graph contains any cycle (circular dependency)
    [[nodiscard]] bool HasCycle() const;

    // Produces a valid linear topological ordering of task IDs
    storage::StatusOr<std::vector<TaskId>> TopologicalSort() const;

    // Partitions tasks into parallel execution waves (tasks in wave K can execute in parallel)
    storage::StatusOr<std::vector<std::vector<TaskId>>> BuildExecutionWaves() const;

    // Executes all tasks in topological order sequentially
    storage::Status ExecuteSequential(void* context = nullptr);

    // Clears the graph
    void Clear();

    [[nodiscard]] size_t TaskCount() const;
    [[nodiscard]] bool HasTask(const TaskId& id) const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<TaskId, TaskNode> tasks_;
    std::unordered_map<TaskId, std::vector<TaskId>> dependents_; // upstream -> list of downstream tasks
};

} // namespace dependency_graph
} // namespace runtime
} // namespace obsidian
