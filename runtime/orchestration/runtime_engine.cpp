#include "runtime/orchestration/runtime_engine.hpp"

namespace obsidian {
namespace runtime {
namespace orchestration {

RuntimeEngine::RuntimeEngine(EngineConfig config)
    : config_(std::move(config)),
      event_bus_(config_.event_bus_capacity),
      pipeline_(config_.pipeline_capacity) {}

RuntimeEngine::~RuntimeEngine() {
    (void)Stop();
}

const std::string& RuntimeEngine::GetComponentName() const {
    return config_.engine_name;
}

registry::LifecycleState RuntimeEngine::GetLifecycleState() const {
    return state_.load(std::memory_order_relaxed);
}

storage::Status RuntimeEngine::Initialize() {
    state_.store(registry::LifecycleState::Starting, std::memory_order_relaxed);

    auto reg_st = registry_.InitializeAll();
    if (!reg_st.ok()) {
        state_.store(registry::LifecycleState::Faulted, std::memory_order_relaxed);
        return reg_st;
    }

    state_.store(registry::LifecycleState::Configured, std::memory_order_relaxed);
    return storage::Status::Ok();
}

storage::Status RuntimeEngine::Start() {
    state_.store(registry::LifecycleState::Starting, std::memory_order_relaxed);

    auto reg_st = registry_.StartAll();
    if (!reg_st.ok()) {
        state_.store(registry::LifecycleState::Faulted, std::memory_order_relaxed);
        return reg_st;
    }

    state_.store(registry::LifecycleState::Running, std::memory_order_relaxed);
    return storage::Status::Ok();
}

storage::Status RuntimeEngine::Pause() {
    state_.store(registry::LifecycleState::Pausing, std::memory_order_relaxed);
    (void)registry_.PauseAll();
    state_.store(registry::LifecycleState::Paused, std::memory_order_relaxed);
    return storage::Status::Ok();
}

storage::Status RuntimeEngine::Resume() {
    state_.store(registry::LifecycleState::Starting, std::memory_order_relaxed);
    (void)registry_.ResumeAll();
    state_.store(registry::LifecycleState::Running, std::memory_order_relaxed);
    return storage::Status::Ok();
}

storage::Status RuntimeEngine::Stop() {
    state_.store(registry::LifecycleState::Stopping, std::memory_order_relaxed);

    plugin_manager_.Clear();
    scheduler_.Clear();
    pipeline_.Clear();
    event_bus_.Reset();
    (void)registry_.StopAll();

    state_.store(registry::LifecycleState::Stopped, std::memory_order_relaxed);
    return storage::Status::Ok();
}

registry::HealthCheckResult RuntimeEngine::CheckHealth() {
    auto current_state = state_.load(std::memory_order_relaxed);
    if (current_state == registry::LifecycleState::Faulted) {
        return registry::HealthCheckResult{registry::HealthStatus::Unhealthy, "Engine in Faulted state", 0, 0};
    }
    if (!registry_.CheckLiveness()) {
        return registry::HealthCheckResult{registry::HealthStatus::Degraded, "Component in registry reported degraded health", 0, 0};
    }
    return registry::HealthCheckResult{registry::HealthStatus::Healthy, "Runtime engine operational", 0, 0};
}

size_t RuntimeEngine::Step(int64_t current_time_ns) {
    if (state_.load(std::memory_order_relaxed) != registry::LifecycleState::Running) {
        return 0;
    }

    size_t work_done = 0;

    // 1. Advance time-driven scheduler
    work_done += scheduler_.Step(current_time_ns);

    // 2. Drain pending event bus queue
    work_done += event_bus_.ProcessPendingEvents();

    // 3. Process next pending batch from streaming pipeline
    while (pipeline_.QueueDepth() > 0) {
        auto batch_or = pipeline_.ProcessNext();
        if (batch_or.ok()) {
            work_done++;
        } else {
            break;
        }
    }

    return work_done;
}

EngineMetrics RuntimeEngine::GetMetrics() const {
    EngineMetrics m;
    m.state = state_.load(std::memory_order_relaxed);
    m.event_metrics = event_bus_.GetMetrics();
    m.scheduler_metrics = scheduler_.GetMetrics();
    m.pipeline_metrics = pipeline_.GetMetrics();
    m.plugin_metrics = plugin_manager_.GetMetrics();
    return m;
}

} // namespace orchestration
} // namespace runtime
} // namespace obsidian
