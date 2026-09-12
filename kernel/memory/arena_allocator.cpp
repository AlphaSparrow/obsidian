#include "allocator.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>

namespace obsidian {
namespace kernel {
namespace memory {

extern void* cache_aligned_alloc(size_t size, size_t alignment) noexcept;
extern void cache_aligned_free(void* ptr) noexcept;

/// High-performance Linear Bump-Pointer Arena Allocator.
///
/// DDIA Chapter 3 (Storage & Retrieval - In-Memory Memtables):
/// Eliminates per-object malloc/free overhead and internal fragmentation.
/// Time Complexity:
///   - allocate(): O(1) deterministic (single addition & bitwise mask)
///   - deallocate(): O(1) no-op
///   - reset(): O(1) deterministic (resets cursor to 0)
/// Cache Locality:
///   Sequential allocations reside contiguously in memory, maximizing L1/L2
///   cache line spatial locality and CPU hardware prefetcher efficiency.
class ArenaAllocator final : public IAllocator {
public:
    explicit ArenaAllocator(size_t capacity, size_t alignment = kCacheLineSize) noexcept
        : capacity_(capacity), alignment_(alignment), owns_memory_(true) {
        buffer_ = static_cast<uint8_t*>(cache_aligned_alloc(capacity, alignment));
        offset_ = 0;
    }

    /// Construct an arena borrowing an existing pre-allocated memory region.
    ArenaAllocator(void* memory, size_t capacity, size_t alignment = kCacheLineSize) noexcept
        : buffer_(static_cast<uint8_t*>(memory)),
          capacity_(capacity),
          alignment_(alignment),
          offset_(0),
          owns_memory_(false) {}

    ~ArenaAllocator() noexcept override {
        if (owns_memory_ && buffer_) {
            cache_aligned_free(buffer_);
            buffer_ = nullptr;
        }
    }

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    ArenaAllocator(ArenaAllocator&& other) noexcept
        : buffer_(other.buffer_),
          capacity_(other.capacity_),
          alignment_(other.alignment_),
          offset_(other.offset_),
          owns_memory_(other.owns_memory_) {
        other.buffer_ = nullptr;
        other.capacity_ = 0;
        other.offset_ = 0;
        other.owns_memory_ = false;
    }

    ArenaAllocator& operator=(ArenaAllocator&& other) noexcept {
        if (this != &other) {
            if (owns_memory_ && buffer_) {
                cache_aligned_free(buffer_);
            }
            buffer_ = other.buffer_;
            capacity_ = other.capacity_;
            alignment_ = other.alignment_;
            offset_ = other.offset_;
            owns_memory_ = other.owns_memory_;

            other.buffer_ = nullptr;
            other.capacity_ = 0;
            other.offset_ = 0;
            other.owns_memory_ = false;
        }
        return *this;
    }

    OBSIDIAN_NODISCARD void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t)) noexcept override {
        if (bytes == 0 || buffer_ == nullptr) {
            return nullptr;
        }

        const size_t effective_align = (alignment > alignment_) ? alignment : alignment_;
        const size_t current_addr = reinterpret_cast<size_t>(buffer_ + offset_);
        const size_t aligned_addr = align_up(current_addr, effective_align);
        const size_t new_offset = (aligned_addr - reinterpret_cast<size_t>(buffer_)) + bytes;

        if (new_offset > capacity_) {
            return nullptr;
        }

        offset_ = new_offset;
        return reinterpret_cast<void*>(aligned_addr);
    }

    void deallocate(void* /*ptr*/, size_t /*bytes*/) noexcept override {
        // Linear bump allocators do not support individual deallocations.
        // Memory is reclaimed in bulk via reset().
    }

    void reset() noexcept override {
        offset_ = 0;
    }

    /// Creates an allocation snapshot point for localized rollback.
    OBSIDIAN_NODISCARD size_t save_point() const noexcept {
        return offset_;
    }

    /// Rolls back the allocation cursor to a previously saved point in O(1).
    void rollback_to(size_t point) noexcept {
        if (point <= capacity_) {
            offset_ = point;
        }
    }

    OBSIDIAN_NODISCARD size_t allocated_bytes() const noexcept override {
        return offset_;
    }

    OBSIDIAN_NODISCARD size_t capacity_bytes() const noexcept override {
        return capacity_;
    }

    OBSIDIAN_NODISCARD size_t remaining_bytes() const noexcept {
        return (capacity_ > offset_) ? (capacity_ - offset_) : 0;
    }

private:
    uint8_t* buffer_{nullptr};
    size_t capacity_{0};
    size_t alignment_{kCacheLineSize};
    size_t offset_{0};
    bool owns_memory_{false};
};

} // namespace memory
} // namespace kernel
} // namespace obsidian
