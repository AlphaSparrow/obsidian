#pragma once

#include "runtime/event_bus/event_bus.hpp"
#include "runtime/registry/service_registry.hpp"
#include "runtime/scheduler/task_scheduler.hpp"
#include "runtime/dependency_graph/dependency_graph.hpp"
#include "runtime/pipeline/pipeline_stage.hpp"
#include "runtime/pipeline/execution_pipeline.hpp"
#include "runtime/plugins/plugin_manager.hpp"
#include "storage/common/status.hpp"

#include <memory>
#include <string>
#include <atomic>

namespace obsidian {
namespace runtime {
namespace orchestration {

struct EngineConfig {
    std::string engine_name{"ObsidianRuntimeEngine"};
    size_t event_bus_capacity{65536};
    size_t pipeline_capacity{16384};
};

struct EngineMetrics {
    registry::LifecycleState state{registry::LifecycleState::Uninitialized};
    event_bus::EventBusMetrics event_metrics;
    scheduler::SchedulerMetrics scheduler_metrics;
    pipeline::PipelineMetrics pipeline_metrics;
    plugins::PluginManagerMetrics plugin_metrics;
};

class RuntimeEngine : public registry::IComponent {
public:
    explicit RuntimeEngine(EngineConfig config = {});
    ~RuntimeEngine() override;

    RuntimeEngine(const RuntimeEngine&) = delete;
    RuntimeEngine& operator=(const RuntimeEngine&) = delete;

    // IComponent interface implementation
    [[nodiscard]] const std::string& GetComponentName() const override;
    [[nodiscard]] registry::LifecycleState GetLifecycleState() const override;
    storage::Status Initialize() override;
    storage::Status Start() override;
    storage::Status Pause() override;
    storage::Status Resume() override;
    storage::Status Stop() override;
    registry::HealthCheckResult CheckHealth() override;

    // Orchestration step: advances scheduler, drains event bus, and processes pending pipeline batches
    size_t Step(int64_t current_time_ns);

    // Subsystem accessors
    [[nodiscard]] event_bus::EventBus& GetEventBus() noexcept { return event_bus_; }
    [[nodiscard]] registry::ServiceRegistry& GetRegistry() noexcept { return registry_; }
    [[nodiscard]] scheduler::TaskScheduler& GetScheduler() noexcept { return scheduler_; }
    [[nodiscard]] dependency_graph::DependencyGraph& GetDependencyGraph() noexcept { return graph_; }
    [[nodiscard]] pipeline::ExecutionPipeline& GetPipeline() noexcept { return pipeline_; }
    [[nodiscard]] plugins::PluginManager& GetPluginManager() noexcept { return plugin_manager_; }

    [[nodiscard]] EngineMetrics GetMetrics() const;

private:
    EngineConfig config_;
    std::atomic<registry::LifecycleState> state_{registry::LifecycleState::Uninitialized};

    event_bus::EventBus event_bus_;
    registry::ServiceRegistry registry_;
    scheduler::TaskScheduler scheduler_;
    dependency_graph::DependencyGraph graph_;
    pipeline::ExecutionPipeline pipeline_;
    plugins::PluginManager plugin_manager_;
};

} // namespace orchestration
} // namespace runtime
} // namespace obsidian
