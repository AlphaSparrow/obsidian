#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

namespace obsidian {
namespace kernel {
namespace logging {

struct LogRecord {
    uint64_t timestamp_us{0};
    uint8_t level{0};
    uint16_t length{0};
    char message[256]{};
};

/// High-Performance Asynchronous Logger.
///
/// DDIA Principles (Write-Ahead-Log & Tail Latency Preservation):
/// Real-time trading engines cannot afford blocking disk I/O or console synchronization.
/// Producers write fixed-size log records into memory buffers in O(1) time;
/// a dedicated background writer thread batches and drains records sequentially.
class AsyncLogger {
public:
    explicit AsyncLogger(size_t buffer_slots = 4096)
        : capacity_(buffer_slots),
          buffer_(buffer_slots),
          running_(false),
          head_(0),
          tail_(0)
#if defined(_WIN32)
        , thread_handle_(nullptr)
#else
        , has_thread_(false)
#endif
    {
        start();
    }

    ~AsyncLogger() {
        stop();
    }

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    void start() {
        if (!running_.exchange(true, std::memory_order_acq_rel)) {
#if defined(_WIN32)
            thread_handle_ = CreateThread(nullptr, 0, &AsyncLogger::worker_entry_win32, this, 0, nullptr);
#else
            if (pthread_create(&thread_handle_, nullptr, &AsyncLogger::worker_entry_posix, this) == 0) {
                has_thread_ = true;
            }
#endif
        }
    }

    void stop() {
        if (running_.exchange(false, std::memory_order_acq_rel)) {
#if defined(_WIN32)
            if (thread_handle_) {
                WaitForSingleObject(thread_handle_, INFINITE);
                CloseHandle(thread_handle_);
                thread_handle_ = nullptr;
            }
#else
            if (has_thread_) {
                pthread_join(thread_handle_, nullptr);
                has_thread_ = false;
            }
#endif
            flush_pending();
        }
    }

    /// Emits a log record asynchronously.
    /// Time Complexity: O(1) non-blocking buffer write.
    void log(uint8_t level, const char* msg, size_t msg_len = 0) {
        if (!running_.load(std::memory_order_relaxed) || msg == nullptr) {
            return;
        }

        if (msg_len == 0) {
            msg_len = std::strlen(msg);
        }

        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_acquire);

        if ((h - t) >= capacity_) {
            // Buffer full: drop record under backpressure to protect p99 SLA
            return;
        }

        const size_t idx = h % capacity_;
        LogRecord& rec = buffer_[idx];

        const auto now = std::chrono::system_clock::now();
        rec.timestamp_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        ).count());
        rec.level = level;

        const size_t copy_len = (msg_len < sizeof(rec.message) - 1) ? msg_len : sizeof(rec.message) - 1;
        std::memcpy(rec.message, msg, copy_len);
        rec.message[copy_len] = '\0';
        rec.length = static_cast<uint16_t>(copy_len);

        head_.store(h + 1, std::memory_order_release);
    }

private:
#if defined(_WIN32)
    static DWORD WINAPI worker_entry_win32(LPVOID param) {
        auto* self = static_cast<AsyncLogger*>(param);
        self->background_worker();
        return 0;
    }
#else
    static void* worker_entry_posix(void* param) {
        auto* self = static_cast<AsyncLogger*>(param);
        self->background_worker();
        return nullptr;
    }
#endif

    void background_worker() {
        while (running_.load(std::memory_order_relaxed)) {
            flush_pending();
#if defined(_WIN32)
            Sleep(10);
#else
            struct timespec ts{0, 10000000};
            nanosleep(&ts, nullptr);
#endif
        }
    }

    void flush_pending() {
        size_t t = tail_.load(std::memory_order_relaxed);
        const size_t h = head_.load(std::memory_order_acquire);

        while (t < h) {
            const size_t idx = t % capacity_;
            const LogRecord& rec = buffer_[idx];

            std::fwrite(rec.message, 1, rec.length, stdout);
            std::fputc('\n', stdout);

            ++t;
        }

        tail_.store(t, std::memory_order_release);
        std::fflush(stdout);
    }

    const size_t capacity_;
    std::vector<LogRecord> buffer_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};

#if defined(_WIN32)
    HANDLE thread_handle_{nullptr};
#else
    pthread_t thread_handle_{};
    bool has_thread_{false};
#endif
};

} // namespace logging
} // namespace kernel
} // namespace obsidian
