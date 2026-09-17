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

namespace obsidian {
namespace kernel {
namespace concurrency {

static constexpr size_t kCacheLineSize = 64;

// Architecture-specific CPU pause instruction for spin loops
inline void cpu_pause() noexcept {
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

// Relinquish remaining CPU timeslice
inline void thread_yield() noexcept {
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

// Read CPU timestamp counter for benchmarking
inline uint64_t read_tsc() noexcept {
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

// Adaptive exponential backoff for spin loops
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

// Lightweight spinlock
class SpinLock {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            cpu_pause();
        }
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// RAII lock guard for SpinLock
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

// Cache-padded wrapper to isolate variables onto their own cache line
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
