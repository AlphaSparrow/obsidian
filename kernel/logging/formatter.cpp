#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace obsidian {
namespace kernel {
namespace logging {

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5
};

inline const char* log_level_to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default:              return "UNKWN";
    }
}

/// Zero-Allocation Real-Time Fast Log Formatter.
///
/// DDIA Observability & Write-Ahead-Log Architecture:
/// Formats structured log records into a fixed pre-allocated byte buffer,
/// avoiding dynamic memory allocations on hot execution paths.
class LogFormatter {
public:
    /// Formats a log line: [Timestamp] [LEVEL] [Message]\n into `dest_buffer`.
    /// Returns the number of bytes written.
    /// Time Complexity: O(N) where N is message length, strictly bounded by buffer capacity.
    static size_t format(
        char* dest_buffer,
        size_t buffer_capacity,
        LogLevel level,
        const char* message,
        size_t message_len = 0
    ) noexcept {
        if (dest_buffer == nullptr || buffer_capacity < 32 || message == nullptr) {
            return 0;
        }

        if (message_len == 0) {
            message_len = std::strlen(message);
        }

        const auto now = std::chrono::system_clock::now();
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        ).count();

        int written = std::snprintf(
            dest_buffer,
            buffer_capacity,
            "[%lu] [%s] ",
            static_cast<unsigned long>(micros),
            log_level_to_string(level)
        );

        if (written < 0 || static_cast<size_t>(written) >= buffer_capacity) {
            return 0;
        }

        size_t offset = static_cast<size_t>(written);
        const size_t available = buffer_capacity - offset - 2;
        const size_t to_copy = (message_len < available) ? message_len : available;

        if (to_copy > 0) {
            std::memcpy(dest_buffer + offset, message, to_copy);
            offset += to_copy;
        }

        dest_buffer[offset++] = '\n';
        dest_buffer[offset] = '\0';

        return offset;
    }
};

} // namespace logging
} // namespace kernel
} // namespace obsidian
