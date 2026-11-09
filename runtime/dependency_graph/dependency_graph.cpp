#include "runtime/dependency_graph/dependency_graph.hpp"
#include <queue>
#include <algorithm>

namespace obsidian {
namespace runtime {
namespace dependency_graph {

storage::Status DependencyGraph::AddTask(TaskNode task) {
    if (task.id.empty()) {
        return storage::Status::InvalidArgument("Task ID cannot be empty");
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (tasks_.find(task.id) != tasks_.end()) {
        return storage::Status::AlreadyExists("Task with ID already exists: " + task.id);
    }

    for (const auto& dep : task.dependencies) {
        dependents_[dep].push_back(task.id);
    }

    tasks_[task.id] = std::move(task);
    return storage::Status::Ok();
}

storage::Status DependencyGraph::AddDependency(const TaskId& upstream, const TaskId& downstream) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (tasks_.find(upstream) == tasks_.end()) {
        return storage::Status::NotFound("Upstream task not found: " + upstream);
    }
    auto it = tasks_.find(downstream);
    if (it == tasks_.end()) {
        return storage::Status::NotFound("Downstream task not found: " + downstream);
    }

    // Check if dependency already exists
    auto& deps = it->second.dependencies;
    if (std::find(deps.begin(), deps.end(), upstream) == deps.end()) {
        deps.push_back(upstream);
        dependents_[upstream].push_back(downstream);
    }

    return storage::Status::Ok();
}

bool DependencyGraph::HasCycle() const {
    auto waves_or = BuildExecutionWaves();
    return !waves_or.ok();
}

storage::StatusOr<std::vector<std::vector<TaskId>>> DependencyGraph::BuildExecutionWaves() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (tasks_.empty()) {
        return std::vector<std::vector<TaskId>>{};
    }

    std::unordered_map<TaskId, size_t> in_degree;
    for (const auto& [id, node] : tasks_) {
        in_degree[id] = node.dependencies.size();
    }

    std::vector<TaskId> current_wave;
    for (const auto& [id, deg] : in_degree) {
        if (deg == 0) {
            current_wave.push_back(id);
        }
    }

    // Sort current wave by priority descending
    auto sort_wave = [this](std::vector<TaskId>& wave) {
        std::sort(wave.begin(), wave.end(), [this](const TaskId& a, const TaskId& b) {
            return tasks_.at(a).priority > tasks_.at(b).priority;
        });
    };
    sort_wave(current_wave);

    std::vector<std::vector<TaskId>> waves;
    size_t processed_count = 0;

    while (!current_wave.empty()) {
        waves.push_back(current_wave);
        processed_count += current_wave.size();

        std::vector<TaskId> next_wave;
        for (const auto& u : current_wave) {
            auto dep_it = dependents_.find(u);
            if (dep_it != dependents_.end()) {
                for (const auto& v : dep_it->second) {
                    if (in_degree[v] > 0) {
                        in_degree[v]--;
                        if (in_degree[v] == 0) {
                            next_wave.push_back(v);
                        }
                    }
                }
            }
        }

        sort_wave(next_wave);
        current_wave = std::move(next_wave);
    }

    if (processed_count != tasks_.size()) {
        return storage::Status::InvalidArgument("Circular dependency (cycle) detected in task graph");
    }

    return waves;
}

storage::StatusOr<std::vector<TaskId>> DependencyGraph::TopologicalSort() const {
    auto waves_or = BuildExecutionWaves();
    if (!waves_or.ok()) {
        return waves_or.status();
    }

    std::vector<TaskId> ordered;
    ordered.reserve(tasks_.size());
    for (const auto& wave : waves_or.value()) {
        for (const auto& id : wave) {
            ordered.push_back(id);
        }
    }

    return ordered;
}

storage::Status DependencyGraph::ExecuteSequential(void* context) {
    auto sorted_or = TopologicalSort();
    if (!sorted_or.ok()) {
        return sorted_or.status();
    }

    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& id : sorted_or.value()) {
        auto it = tasks_.find(id);
        if (it != tasks_.end() && it->second.func) {
            auto st = it->second.func(context);
            if (!st.ok()) {
                return storage::Status::Internal("Task execution failed for '" + id + "': " + st.message());
            }
        }
    }

    return storage::Status::Ok();
}

void DependencyGraph::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    tasks_.clear();
    dependents_.clear();
}

size_t DependencyGraph::TaskCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tasks_.size();
}

bool DependencyGraph::HasTask(const TaskId& id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tasks_.find(id) != tasks_.end();
}

} // namespace dependency_graph
} // namespace runtime
} // namespace obsidian
