#include "allocator.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace obsidian {
namespace kernel {
namespace memory {

extern void* cache_aligned_alloc(size_t size, size_t alignment) noexcept;
extern void cache_aligned_free(void* ptr) noexcept;

/// Single-Producer Single-Consumer (SPSC) Lock-Free & Wait-Free Ring Buffer.
///
/// DDIA Chapter 11 (Stream Processing & Log-Based Brokers):
/// Serves as the fundamental non-blocking inter-thread messaging backbone.
///
/// Algorithmic Guarantees:
///   - Time Complexity: O(1) wait-free push, O(1) wait-free pop.
///   - False Sharing Prevention: Producer head and consumer tail are isolated
///     onto separate 64-byte hardware cache lines.
///   - Index Wrapping: Power-of-two capacity converts modulo arithmetic
///     into single-cycle bitwise AND masking: index & (capacity - 1).
template <typename T>
class SPSCRingBuffer {
public:
    static_assert(std::is_nothrow_destructible<T>::value, "T must be nothrow destructible");

    explicit SPSCRingBuffer(size_t capacity)
        : capacity_(next_power_of_two(capacity < 2 ? 2 : capacity)),
          mask_(capacity_ - 1),
          storage_(static_cast<T*>(cache_aligned_alloc(capacity_ * sizeof(T), alignof(T) > kCacheLineSize ? alignof(T) : kCacheLineSize))) {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~SPSCRingBuffer() {
        T item;
        while (pop(item)) {
            // Drain items
        }
        cache_aligned_free(storage_);
    }

    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

    template <typename... Args>
    bool emplace(Args&&... args) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_acquire);

        if ((current_head - current_tail) >= capacity_) {
            return false;
        }

        const size_t index = current_head & mask_;
        new (&storage_[index]) T(std::forward<Args>(args)...);

        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    bool push(const T& item) {
        return emplace(item);
    }

    bool push(T&& item) {
        return emplace(std::move(item));
    }

    bool pop(T& value) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_acquire);

        if (current_tail >= current_head) {
            return false;
        }

        const size_t index = current_tail & mask_;
        value = std::move(storage_[index]);
        storage_[index].~T();

        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    OBSIDIAN_NODISCARD bool empty() const noexcept {
        return tail_.load(std::memory_order_relaxed) >= head_.load(std::memory_order_relaxed);
    }

    OBSIDIAN_NODISCARD size_t size() const noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_relaxed);
        return (h >= t) ? (h - t) : 0;
    }

    OBSIDIAN_NODISCARD size_t capacity() const noexcept {
        return capacity_;
    }

private:
    static constexpr size_t next_power_of_two(size_t n) noexcept {
        size_t p = 1;
        while (p < n) {
            p <<= 1;
        }
        return p;
    }

    const size_t capacity_;
    const size_t mask_;
    T* const storage_;

    alignas(kCacheLineSize) std::atomic<size_t> head_{0};
    uint8_t pad0_[kCacheLineSize > sizeof(std::atomic<size_t>) ? kCacheLineSize - sizeof(std::atomic<size_t>) : 1];

    alignas(kCacheLineSize) std::atomic<size_t> tail_{0};
    uint8_t pad1_[kCacheLineSize > sizeof(std::atomic<size_t>) ? kCacheLineSize - sizeof(std::atomic<size_t>) : 1];
};

template class SPSCRingBuffer<uint64_t>;

} // namespace memory
} // namespace kernel
} // namespace obsidian
