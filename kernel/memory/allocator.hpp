#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#if __cplusplus >= 201703L
#define OBSIDIAN_NODISCARD [[nodiscard]]
#else
#define OBSIDIAN_NODISCARD
#endif

namespace obsidian {
namespace kernel {
namespace memory {

/// Default cache-line boundary (64 bytes on x86-64 and most modern architectures).
/// DDIA Principle: False sharing on multi-core architectures degrades throughput;
/// aligning hot variables to cache-line boundaries prevents invalidation storms.
static constexpr size_t kCacheLineSize = 64;

/// Computes the smallest multiple of `alignment` greater than or equal to `size`.
/// Precondition: `alignment` must be a power of two.
OBSIDIAN_NODISCARD inline constexpr size_t align_up(size_t size, size_t alignment) noexcept {
    return (size + (alignment - 1)) & ~(alignment - 1);
}

/// Verifies whether `ptr` or `address` satisfies the given power-of-two alignment.
OBSIDIAN_NODISCARD inline bool is_aligned(const void* ptr, size_t alignment) noexcept {
    return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
}

/// Abstract base interface for all memory allocation strategies in Obsidian.
/// Conforms to DDIA Chapter 3 (Storage and Retrieval) memory predictability standards:
/// All allocations enforce deterministic O(1) bounds without unpredictable GC pauses.
class IAllocator {
public:
    virtual ~IAllocator() = default;

    /// Allocates `bytes` with the specified byte alignment (must be a power of two).
    /// Returns nullptr if the allocation cannot be fulfilled.
    OBSIDIAN_NODISCARD virtual void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t)) noexcept = 0;

    /// Releases previously allocated memory.
    /// For arena/bump allocators, this is a no-op; bulk reset is performed via reset().
    virtual void deallocate(void* ptr, size_t bytes) noexcept = 0;

    /// Reclaims all managed memory in bulk in O(1) time.
    virtual void reset() noexcept = 0;

    /// Returns the total bytes allocated and currently in use.
    OBSIDIAN_NODISCARD virtual size_t allocated_bytes() const noexcept = 0;

    /// Returns the maximum capacity of this allocator.
    OBSIDIAN_NODISCARD virtual size_t capacity_bytes() const noexcept = 0;
};

} // namespace memory
} // namespace kernel
} // namespace obsidian
