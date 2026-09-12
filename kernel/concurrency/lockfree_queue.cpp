#include "atomic_utils.hpp"

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
}

namespace concurrency {

/// Bounded Multi-Producer Multi-Consumer (MPMC) Lock-Free Queue.
///
/// DDIA Chapter 1 (Tail Latency Amplification) & Chapter 11 (Stream Processing):
/// High-contention message queue eliminating thread synchronization bottlenecks
/// and mutual exclusion blocking (mutex locks).
///
/// Algorithmic Basis (Vyukov MPMC Algorithm):
///   - Ring buffer of Cell structs, each with an atomic sequence ticket.
///   - Enqueue ticket check: cell.sequence == pos
///   - Dequeue ticket check: cell.sequence == pos + 1
///   - Time Complexity: O(1) wait-free progression under low-to-medium contention,
///     bounded O(1) CAS retries under high contention.
template <typename T>
class LockFreeMPMCQueue {
public:
    static_assert(std::is_nothrow_destructible<T>::value, "T must be nothrow destructible");

    explicit LockFreeMPMCQueue(size_t capacity)
        : capacity_(next_power_of_two(capacity < 2 ? 2 : capacity)),
          mask_(capacity_ - 1),
          buffer_(static_cast<Cell*>(memory::cache_aligned_alloc(capacity_ * sizeof(Cell), alignof(Cell) > kCacheLineSize ? alignof(Cell) : kCacheLineSize))) {
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }

    ~LockFreeMPMCQueue() {
        T dummy;
        while (dequeue(dummy)) {
            // Drain remaining elements
        }
        memory::cache_aligned_free(buffer_);
    }

    LockFreeMPMCQueue(const LockFreeMPMCQueue&) = delete;
    LockFreeMPMCQueue& operator=(const LockFreeMPMCQueue&) = delete;

    template <typename... Args>
    bool enqueue(Args&&... args) {
        Cell* cell = nullptr;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (dif == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                return false;
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        new (&cell->storage) T(std::forward<Args>(args)...);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool dequeue(T& value) {
        Cell* cell = nullptr;
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (dif == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                return false;
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }

        T* item_ptr = reinterpret_cast<T*>(&cell->storage);
        value = std::move(*item_ptr);
        item_ptr->~T();

        cell->sequence.store(pos + mask_ + 1, std::memory_order_release);
        return true;
    }

    OBSIDIAN_NODISCARD size_t capacity() const noexcept { return capacity_; }

private:
    struct Cell {
        std::atomic<size_t> sequence;
        alignas(alignof(T)) uint8_t storage[sizeof(T)];
    };

    static constexpr size_t next_power_of_two(size_t n) noexcept {
        size_t p = 1;
        while (p < n) {
            p <<= 1;
        }
        return p;
    }

    const size_t capacity_;
    const size_t mask_;
    Cell* const buffer_;

    alignas(kCacheLineSize) std::atomic<size_t> enqueue_pos_{0};
    alignas(kCacheLineSize) std::atomic<size_t> dequeue_pos_{0};
};

template class LockFreeMPMCQueue<uint64_t>;

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
