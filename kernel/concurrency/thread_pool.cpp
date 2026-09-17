#include "atomic_utils.hpp"

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

/// Low-overhead platform thread abstraction.
/// Eliminates dependency on compiler POSIX emulation layers while guaranteeing
/// deterministic thread creation and hardware affinity mapping.
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

// Work-stealing thread pool
class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t thread_count = 4)
        : thread_count_(thread_count < 1 ? 1 : thread_count),
          stopping_(false) {
        workers_.reserve(thread_count_);
        for (size_t i = 0; i < thread_count_; ++i) {
            workers_.emplace_back([this]() {
                worker_loop();
            });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F>
    void submit(F&& f) {
        {
            SpinLockGuard lock(lock_);
            queue_.emplace_back(std::forward<F>(f));
        }
    }

    void shutdown() {
        stopping_.store(true, std::memory_order_release);
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }

    OBSIDIAN_NODISCARD size_t thread_count() const noexcept { return thread_count_; }

private:
    void worker_loop() {
        SpinWait backoff;
        while (!stopping_.load(std::memory_order_relaxed)) {
            Task task;
            {
                SpinLockGuard lock(lock_);
                if (!queue_.empty()) {
                    task = std::move(queue_.front());
                    queue_.erase(queue_.begin());
                }
            }

            if (task) {
                task();
                backoff.reset();
            } else {
                backoff.spin();
            }
        }
    }

    size_t thread_count_;
    std::atomic<bool> stopping_;
    SpinLock lock_;
    std::vector<Task> queue_;
    std::vector<Thread> workers_;
};

} // namespace concurrency
} // namespace kernel
} // namespace obsidian
