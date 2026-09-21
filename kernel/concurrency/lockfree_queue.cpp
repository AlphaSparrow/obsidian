#include "lockfree_queue.hpp"

namespace obsidian {
namespace kernel {
namespace concurrency {

// Explicit template instantiations for core kernel primitive types
template class LockFreeMPMCQueue<uint64_t>;
template class LockFreeMPMCQueue<void*>;

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
