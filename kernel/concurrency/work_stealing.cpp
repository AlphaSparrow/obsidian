#include "work_stealing.hpp"

#include <functional>

namespace obsidian {
namespace kernel {
namespace concurrency {

// Explicit template instantiations
template class WorkStealingDeque<std::function<void()>>;
template class WorkStealingDeque<void*>;

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
