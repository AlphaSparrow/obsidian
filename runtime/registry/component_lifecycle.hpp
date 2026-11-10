#pragma once

#include "storage/common/status.hpp"
#include <string>
#include <cstdint>

namespace obsidian {
namespace runtime {
namespace registry {

enum class LifecycleState : uint8_t {
    Uninitialized = 0,
    Configured = 1,
    Starting = 2,
    Running = 3,
    Pausing = 4,
    Paused = 5,
    Stopping = 6,
    Stopped = 7,
    Faulted = 8
};

inline const char* LifecycleStateToString(LifecycleState state) {
    switch (state) {
        case LifecycleState::Uninitialized: return "Uninitialized";
        case LifecycleState::Configured: return "Configured";
        case LifecycleState::Starting: return "Starting";
        case LifecycleState::Running: return "Running";
        case LifecycleState::Pausing: return "Pausing";
        case LifecycleState::Paused: return "Paused";
        case LifecycleState::Stopping: return "Stopping";
        case LifecycleState::Stopped: return "Stopped";
        case LifecycleState::Faulted: return "Faulted";
        default: return "Unknown";
    }
}

enum class HealthStatus : uint8_t {
    Healthy = 0,
    Degraded = 1,
    Unhealthy = 2
};

inline const char* HealthStatusToString(HealthStatus status) {
    switch (status) {
        case HealthStatus::Healthy: return "Healthy";
        case HealthStatus::Degraded: return "Degraded";
        case HealthStatus::Unhealthy: return "Unhealthy";
        default: return "Unknown";
    }
}

struct HealthCheckResult {
    HealthStatus status{HealthStatus::Healthy};
    std::string message{"OK"};
    int64_t timestamp_ns{0};
    int64_t latency_us{0};
};

class IComponent {
public:
    virtual ~IComponent() = default;

    [[nodiscard]] virtual const std::string& GetComponentName() const = 0;
    [[nodiscard]] virtual LifecycleState GetLifecycleState() const = 0;

    virtual storage::Status Initialize() = 0;
    virtual storage::Status Start() = 0;
    virtual storage::Status Pause() { return storage::Status::Ok(); }
    virtual storage::Status Resume() { return storage::Status::Ok(); }
    virtual storage::Status Stop() = 0;

    virtual HealthCheckResult CheckHealth() {
        return HealthCheckResult{HealthStatus::Healthy, "Component operational", 0, 0};
    }
};

} // namespace registry
} // namespace runtime
} // namespace obsidian
