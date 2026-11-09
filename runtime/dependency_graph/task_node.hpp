#pragma once

#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace obsidian {
namespace runtime {
namespace dependency_graph {

using TaskId = std::string;
using TaskFunction = std::function<storage::Status(void* context)>;

struct TaskNode {
    TaskId id;
    std::string name;
    TaskFunction func;
    std::vector<TaskId> dependencies; // Tasks that must complete before this task executes
    int32_t priority{0};
};

} // namespace dependency_graph
} // namespace runtime
} // namespace obsidian
