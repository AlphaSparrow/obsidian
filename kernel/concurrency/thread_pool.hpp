#pragma once

#include "atomic_utils.hpp"
#include "lockfree_queue.hpp"
#include "work_stealing.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace obsidian {
namespace kernel {
namespace concurrency {

/// Low-overhead platform thread abstraction with hardware affinity support.
class Thread {
public:
    template <typename F>
    explicit Thread(F&& f) {
        auto* func = new std::function<void()>(std::forward<F>(f));
#if defined(_WIN32)
        handle_ = CreateThread(nullptr, 0, &Thread::run_win32, func, 0, nullptr);
#else
        pthread_create(&handle_, nullptr, &Thread::run_posix, func);
        has_handle_ = true;
#endif
    }

    ~Thread() {
        join();
    }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(Thread&& other) noexcept
#if defined(_WIN32)
        : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
#else
        : handle_(other.handle_), has_handle_(other.has_handle_) {
        other.has_handle_ = false;
    }
#endif

    void join() noexcept {
#if defined(_WIN32)
        if (handle_) {
            WaitForSingleObject(handle_, INFINITE);
            CloseHandle(handle_);
            handle_ = nullptr;
        }
#else
        if (has_handle_) {
            pthread_join(handle_, nullptr);
            has_handle_ = false;
        }
#endif
    }

    OBSIDIAN_NODISCARD bool joinable() const noexcept {
#if defined(_WIN32)
        return handle_ != nullptr;
#else
        return has_handle_;
#endif
    }

    bool set_affinity(size_t core_id) noexcept {
#if defined(_WIN32)
        if (!handle_) return false;
        DWORD_PTR mask = static_cast<DWORD_PTR>(1) << (core_id % (sizeof(DWORD_PTR) * 8));
        return SetThreadAffinityMask(handle_, mask) != 0;
#elif defined(__linux__)
        if (!has_handle_) return false;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        return pthread_setaffinity_np(handle_, sizeof(cpu_set_t), &cpuset) == 0;
#else
        (void)core_id;
        return false;
#endif
    }

    static size_t hardware_concurrency() noexcept {
#if defined(_WIN32)
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        return sysinfo.dwNumberOfProcessors > 0 ? sysinfo.dwNumberOfProcessors : 4;
#else
        return 4;
#endif
    }

private:
#if defined(_WIN32)
    static DWORD WINAPI run_win32(LPVOID param) {
        auto* func = static_cast<std::function<void()>*>(param);
        (*func)();
        delete func;
        return 0;
    }
    HANDLE handle_{nullptr};
#else
    static void* run_posix(void* param) {
        auto* func = static_cast<std::function<void()>*>(param);
        (*func)();
        delete func;
        return nullptr;
    }
    pthread_t handle_{};
    bool has_handle_{false};
#endif
};

/// High-performance Work-Stealing Thread Pool.
class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t thread_count = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Submits a fire-and-forget callable task.
    template <typename F>
    void submit(F&& f) {
        Task task(std::forward<F>(f));
        submit_internal(std::move(task));
    }

    void shutdown();

    OBSIDIAN_NODISCARD size_t thread_count() const noexcept { return thread_count_; }
    OBSIDIAN_NODISCARD bool is_running() const noexcept { return !stopping_.load(std::memory_order_relaxed); }

private:
    void worker_loop(size_t worker_id);
    void submit_internal(Task task);

    const size_t thread_count_;
    std::atomic<bool> stopping_{false};

    // Global injection queue for externally submitted tasks
    LockFreeMPMCQueue<Task> global_queue_;

    // Per-worker work-stealing deques
    std::vector<std::unique_ptr<WorkStealingDeque<Task>>> local_queues_;
    std::vector<Thread> workers_;

#if defined(_WIN32)
    HANDLE wake_event_{nullptr};
#endif
    std::atomic<uint32_t> idle_workers_{0};
};

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
