#pragma once

#include "atomic_utils.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace obsidian {
namespace kernel {
namespace concurrency {

/// Sequence counter padded to 64 bytes to eliminate false-sharing invalidations.
struct alignas(kCacheLineSize) Sequence {
    std::atomic<int64_t> value{-1};

    Sequence() = default;
    explicit Sequence(int64_t initial) : value(initial) {}

    int64_t get() const noexcept {
        return value.load(std::memory_order_acquire);
    }

    void set(int64_t v) noexcept {
        value.store(v, std::memory_order_release);
    }

    int64_t increment_and_get() noexcept {
        return value.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    bool compare_and_swap(int64_t& expected, int64_t desired) noexcept {
        return value.compare_exchange_weak(expected, desired, std::memory_order_acq_rel, std::memory_order_relaxed);
    }
};

/// High-performance LMAX-style Lock-Free RingBuffer / Disruptor.
///
/// Features:
/// - Zero heap allocation during steady-state processing.
/// - Pre-allocated contiguous event slots for maximum L1/L2 cache prefetching.
/// - Single-Writer / Multi-Consumer or Multi-Writer sequence claim mechanisms.
template <typename Event, size_t RingSize>
class Disruptor {
    static_assert((RingSize & (RingSize - 1)) == 0, "RingSize must be a power of 2");

public:
    Disruptor() : mask_(RingSize - 1) {
        cursor_.set(-1);
    }

    /// Claim next sequence number for writing (Multi-Producer safe).
    int64_t claim_next() noexcept {
        int64_t current = cursor_.get();
        for (;;) {
            int64_t next = current + 1;
            if (cursor_.compare_and_swap(current, next)) {
                return next;
            }
            cpu_pause();
        }
    }

    /// Returns a mutable reference to the pre-allocated event at the given sequence.
    Event& get(int64_t sequence) noexcept {
        return ring_[static_cast<size_t>(sequence) & mask_].event;
    }

    /// Publishes the claimed sequence, making it visible to consumers.
    void publish(int64_t sequence) noexcept {
        ring_[static_cast<size_t>(sequence) & mask_].published.store(sequence, std::memory_order_release);
    }

    /// Checks if a sequence is fully published and ready for consumption.
    bool is_published(int64_t sequence) const noexcept {
        return ring_[static_cast<size_t>(sequence) & mask_].published.load(std::memory_order_acquire) == sequence;
    }

    /// Wait strategy for consumer to poll sequence availability.
    void wait_until_published(int64_t sequence) const noexcept {
        SpinWait wait;
        while (!is_published(sequence)) {
            wait.spin();
        }
    }

    OBSIDIAN_NODISCARD static constexpr size_t capacity() noexcept {
        return RingSize;
    }

private:
    struct alignas(kCacheLineSize) Slot {
        std::atomic<int64_t> published{-1};
        Event event{};
    };

    const size_t mask_;
    Sequence cursor_;
    alignas(kCacheLineSize) Slot ring_[RingSize];
};

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
