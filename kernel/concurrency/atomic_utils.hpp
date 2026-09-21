#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sched.h>
#endif

#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

#if __cplusplus >= 201703L
#define OBSIDIAN_NODISCARD [[nodiscard]]
#else
#define OBSIDIAN_NODISCARD
#endif

#if defined(_MSC_VER)
#define OBSIDIAN_FORCE_INLINE __forceinline
#define OBSIDIAN_COMPILER_BARRIER() _ReadWriteBarrier()
#else
#define OBSIDIAN_FORCE_INLINE inline __attribute__((always_inline))
#define OBSIDIAN_COMPILER_BARRIER() asm volatile("" ::: "memory")
#endif

namespace obsidian {
namespace kernel {
namespace concurrency {

static constexpr size_t kCacheLineSize = 64;

/// Architecture-specific CPU pause instruction for low-latency spin loops
OBSIDIAN_FORCE_INLINE void cpu_pause() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_pause();
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield" ::: "memory");
#elif defined(_WIN32)
    YieldProcessor();
#else
    sched_yield();
#endif
}

/// Relinquish remaining CPU timeslice
OBSIDIAN_FORCE_INLINE void thread_yield() noexcept {
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

/// Read CPU timestamp counter for high-precision benchmarking
OBSIDIAN_FORCE_INLINE uint64_t read_tsc() noexcept {
#if defined(_MSC_VER)
    return __rdtsc();
#elif defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#elif defined(__aarch64__)
    uint64_t val;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    return 0;
#endif
}

/// Adaptive exponential backoff for spin loops (pause -> multi-pause -> yield)
class SpinWait {
public:
    SpinWait() = default;

    void spin() noexcept {
        if (count_ < 16) {
            cpu_pause();
        } else if (count_ < 32) {
            for (uint32_t i = 0; i < (count_ - 15); ++i) {
                cpu_pause();
            }
        } else {
            thread_yield();
        }
        ++count_;
    }

    void reset() noexcept {
        count_ = 0;
    }

    OBSIDIAN_NODISCARD uint32_t count() const noexcept {
        return count_;
    }

private:
    uint32_t count_{0};
};

/// Lightweight Test-and-Test-and-Set (TTAS) SpinLock
class SpinLock {
public:
    SpinLock() = default;

    void lock() noexcept {
        for (;;) {
            if (!flag_.test_and_set(std::memory_order_acquire)) {
                return;
            }
            while (locked_.load(std::memory_order_relaxed)) {
                cpu_pause();
            }
        }
    }

    OBSIDIAN_NODISCARD bool try_lock() noexcept {
        if (!flag_.test_and_set(std::memory_order_acquire)) {
            locked_.store(true, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    void unlock() noexcept {
        locked_.store(false, std::memory_order_relaxed);
        flag_.clear(std::memory_order_release);
    }

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> locked_{false};
};

/// RAII lock guard for SpinLock
class SpinLockGuard {
public:
    explicit SpinLockGuard(SpinLock& lock) noexcept : lock_(lock) {
        lock_.lock();
    }

    ~SpinLockGuard() noexcept {
        lock_.unlock();
    }

    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;

private:
    SpinLock& lock_;
};

/// Fair FIFO Ticket Lock preventing thread starvation
class TicketLock {
public:
    TicketLock() : ticket_(0), serving_(0) {}

    void lock() noexcept {
        const uint32_t my_ticket = ticket_.fetch_add(1, std::memory_order_relaxed);
        while (serving_.load(std::memory_order_acquire) != my_ticket) {
            cpu_pause();
        }
    }

    OBSIDIAN_NODISCARD bool try_lock() noexcept {
        uint32_t s = serving_.load(std::memory_order_acquire);
        uint32_t t = ticket_.load(std::memory_order_relaxed);
        if (s != t) return false;
        return ticket_.compare_exchange_strong(t, t + 1, std::memory_order_acquire, std::memory_order_relaxed);
    }

    void unlock() noexcept {
        serving_.fetch_add(1, std::memory_order_release);
    }

private:
    alignas(kCacheLineSize) std::atomic<uint32_t> ticket_;
    alignas(kCacheLineSize) std::atomic<uint32_t> serving_;
};

/// Sequential Lock (SeqLock) for single-writer, optimistic wait-free multi-reader access.
class SeqLock {
public:
    SeqLock() : sequence_(0) {}

    OBSIDIAN_NODISCARD uint32_t read_begin() const noexcept {
        for (;;) {
            uint32_t seq = sequence_.load(std::memory_order_acquire);
            if ((seq & 1) == 0) {
                return seq;
            }
            cpu_pause();
        }
    }

    OBSIDIAN_NODISCARD bool read_retry(uint32_t start_seq) const noexcept {
        std::atomic_thread_fence(std::memory_order_acquire);
        return sequence_.load(std::memory_order_relaxed) != start_seq;
    }

    void write_lock() noexcept {
        spin_.lock();
        sequence_.fetch_add(1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
    }

    void write_unlock() noexcept {
        sequence_.fetch_add(1, std::memory_order_release);
        spin_.unlock();
    }

private:
    alignas(kCacheLineSize) std::atomic<uint32_t> sequence_;
    SpinLock spin_;
};

/// Cache-padded wrapper to isolate variables onto their own cache line and eliminate false sharing
template <typename T>
struct alignas(kCacheLineSize) CachePadded {
    T value;

    template <typename... Args>
    explicit CachePadded(Args&&... args) : value(std::forward<Args>(args)...) {}

    CachePadded() = default;
    ~CachePadded() = default;

    T& operator*() noexcept { return value; }
    const T& operator*() const noexcept { return value; }

    T* operator->() noexcept { return &value; }
    const T* operator->() const noexcept { return &value; }
};

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
