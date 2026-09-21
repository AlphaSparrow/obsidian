#pragma once

#include "atomic_utils.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace obsidian {
namespace kernel {
namespace concurrency {

/// Chase-Lev Lock-Free Work-Stealing Deque.
///
/// Single-Producer, Multi-Consumer (SPMC):
/// - Worker thread executes push_bottom() and pop_bottom() in LIFO order (optimal cache warmth).
/// - Stealer threads execute steal_top() concurrently in FIFO order (largest granularity tasks stolen).
/// - Eliminates heap allocation per task push using in-place cell storage.
template <typename T>
class WorkStealingDeque {
public:
    static_assert(std::is_nothrow_destructible<T>::value, "T must be nothrow destructible");

    explicit WorkStealingDeque(size_t initial_capacity = 1024)
        : capacity_(next_power_of_two(initial_capacity < 8 ? 8 : initial_capacity)),
          mask_(capacity_ - 1),
          cells_(new Cell[capacity_]) {
        top_.store(0, std::memory_order_relaxed);
        bottom_.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < capacity_; ++i) {
            cells_[i].occupied.store(false, std::memory_order_relaxed);
        }
    }

    ~WorkStealingDeque() {
        T dummy;
        while (pop_bottom(dummy)) {
            // Drain remaining elements
        }
    }

    WorkStealingDeque(const WorkStealingDeque&) = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    /// Pushes an item to the bottom of the deque (called only by the worker thread).
    /// Returns true on success, false if the deque ring buffer is saturated.
    template <typename... Args>
    bool push_bottom(Args&&... args) {
        const int64_t b = bottom_.load(std::memory_order_relaxed);
        const int64_t t = top_.load(std::memory_order_acquire);

        if (b - t >= static_cast<int64_t>(capacity_)) {
            return false; // Buffer saturated
        }

        const size_t idx = static_cast<size_t>(b) & mask_;
        Cell& cell = cells_[idx];
        new (&cell.storage) T(std::forward<Args>(args)...);
        cell.occupied.store(true, std::memory_order_release);

        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }

    /// Pops an item from the bottom of the deque (called only by the worker thread).
    bool pop_bottom(T& val) {
        int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = top_.load(std::memory_order_relaxed);

        if (t <= b) {
            // Non-empty deque
            const size_t idx = static_cast<size_t>(b) & mask_;
            Cell& cell = cells_[idx];

            if (t == b) {
                // Last remaining element: race against potential stealer
                if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    // Stealer won the race
                    bottom_.store(b + 1, std::memory_order_relaxed);
                    return false;
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }

            T* item_ptr = reinterpret_cast<T*>(&cell.storage);
            val = std::move(*item_ptr);
            item_ptr->~T();
            cell.occupied.store(false, std::memory_order_relaxed);
            return true;
        }

        // Deque was empty
        bottom_.store(b + 1, std::memory_order_relaxed);
        return false;
    }

    /// Steals an item from the top of the deque (called by concurrent stealer threads).
    bool steal_top(T& val) {
        int64_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const int64_t b = bottom_.load(std::memory_order_acquire);

        if (t < b) {
            const size_t idx = static_cast<size_t>(t) & mask_;
            Cell& cell = cells_[idx];

            if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return false; // Another stealer won
            }

            // Ensure producer write is visible
            while (!cell.occupied.load(std::memory_order_acquire)) {
                cpu_pause();
            }

            T* item_ptr = reinterpret_cast<T*>(&cell.storage);
            val = std::move(*item_ptr);
            item_ptr->~T();
            cell.occupied.store(false, std::memory_order_relaxed);
            return true;
        }

        return false;
    }

    OBSIDIAN_NODISCARD bool empty() const noexcept {
        const int64_t b = bottom_.load(std::memory_order_relaxed);
        const int64_t t = top_.load(std::memory_order_relaxed);
        return b <= t;
    }

    OBSIDIAN_NODISCARD size_t size() const noexcept {
        const int64_t b = bottom_.load(std::memory_order_relaxed);
        const int64_t t = top_.load(std::memory_order_relaxed);
        return b > t ? static_cast<size_t>(b - t) : 0;
    }

    OBSIDIAN_NODISCARD size_t capacity() const noexcept { return capacity_; }

private:
    struct alignas(alignof(T) > sizeof(void*) ? alignof(T) : sizeof(void*)) Cell {
        std::atomic<bool> occupied{false};
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
    std::unique_ptr<Cell[]> cells_;

    alignas(kCacheLineSize) std::atomic<int64_t> top_{0};
    alignas(kCacheLineSize) std::atomic<int64_t> bottom_{0};
};

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
