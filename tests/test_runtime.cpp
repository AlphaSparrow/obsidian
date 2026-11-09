#include "runtime/event_bus/event.hpp"
#include "runtime/event_bus/event_bus.hpp"
#include "runtime/registry/component_lifecycle.hpp"
#include "runtime/registry/service_registry.hpp"
#include "runtime/dependency_graph/task_node.hpp"
#include "runtime/dependency_graph/dependency_graph.hpp"
#include "runtime/scheduler/task_scheduler.hpp"
#include "runtime/pipeline/pipeline_stage.hpp"
#include "runtime/pipeline/execution_pipeline.hpp"
#include "runtime/plugins/plugin_interface.hpp"
#include "runtime/plugins/plugin_manifest.hpp"
#include "runtime/plugins/plugin_manager.hpp"
#include "runtime/orchestration/runtime_engine.hpp"

#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <atomic>

using namespace obsidian::runtime::event_bus;
using namespace obsidian::runtime::registry;
using namespace obsidian::runtime::dependency_graph;
using namespace obsidian::runtime::scheduler;
using namespace obsidian::runtime::pipeline;
using namespace obsidian::runtime::plugins;
using namespace obsidian::runtime::orchestration;
using obsidian::storage::Status;

void TestEventBusTopicAndTypeSubscription() {
    std::cout << "[TEST] Running TestEventBusTopicAndTypeSubscription...\n";
    EventBus bus;

    std::atomic<int> topic_count{0};
    std::atomic<int> type_count{0};

    // 1. Subscribe by topic
    auto sub1 = bus.Subscribe(42, [&](const Event& evt) {
        assert(evt.topic_id == 42);
        topic_count++;
    });
    assert(sub1 > 0);

    // 2. Subscribe by type
    auto sub2 = bus.SubscribeType(EventType::MarketTick, [&](const Event& evt) {
        assert(evt.type == EventType::MarketTick);
        type_count++;
    });
    assert(sub2 > 0);

    // 3. Publish matching topic 42 and type MarketTick
    Event evt1;
    evt1.topic_id = 42;
    evt1.type = EventType::MarketTick;
    auto st = bus.PublishSync(evt1);
    assert(st.ok());
    assert(topic_count == 1);
    assert(type_count == 1);

    // 4. Publish matching type only (different topic)
    Event evt2;
    evt2.topic_id = 99;
    evt2.type = EventType::MarketTick;
    st = bus.PublishSync(evt2);
    assert(st.ok());
    assert(topic_count == 1);
    assert(type_count == 2);

    // 5. Async publishing and draining
    for (int i = 0; i < 5; ++i) {
        Event async_evt;
        async_evt.topic_id = 42;
        async_evt.type = EventType::SignalGenerated;
        assert(bus.PublishAsync(async_evt).ok());
    }

    auto metrics = bus.GetMetrics();
    assert(metrics.queue_depth == 5);
    size_t processed = bus.ProcessPendingEvents();
    assert(processed == 5);
    assert(topic_count == 6);

    // 6. Unsubscribe
    assert(bus.Unsubscribe(sub1));
    assert(!bus.Unsubscribe(sub1)); // Second unsubscribe should fail

    std::cout << "  -> PASSED\n";
}

class MockComponent : public IComponent {
public:
    explicit MockComponent(std::string name) : name_(std::move(name)) {}

    const std::string& GetComponentName() const override { return name_; }
    LifecycleState GetLifecycleState() const override { return state_; }

    Status Initialize() override {
        state_ = LifecycleState::Configured;
        return Status::Ok();
    }
    Status Start() override {
        state_ = LifecycleState::Running;
        return Status::Ok();
    }
    Status Stop() override {
        state_ = LifecycleState::Stopped;
        return Status::Ok();
    }

private:
    std::string name_;
    LifecycleState state_{LifecycleState::Uninitialized};
};

struct IMarketDataFeed {
    virtual ~IMarketDataFeed() = default;
    virtual std::string GetFeedName() const = 0;
};

struct BinanceFeed : public IMarketDataFeed {
    std::string GetFeedName() const override { return "Binance-USDT"; }
};

void TestServiceRegistryAndLifecycle() {
    std::cout << "[TEST] Running TestServiceRegistryAndLifecycle...\n";
    ServiceRegistry registry;

    // 1. Service locator
    auto feed = std::make_shared<BinanceFeed>();
    assert(registry.RegisterService<IMarketDataFeed>(feed).ok());
    assert(registry.HasService<IMarketDataFeed>());

    auto feed_or = registry.GetService<IMarketDataFeed>();
    assert(feed_or.ok());
    assert(feed_or.value()->GetFeedName() == "Binance-USDT");

    // 2. Component lifecycle
    auto comp1 = std::make_shared<MockComponent>("OrderGateway");
    auto comp2 = std::make_shared<MockComponent>("RiskController");

    assert(registry.RegisterComponent(comp1).ok());
    assert(registry.RegisterComponent(comp2).ok());
    assert(registry.ComponentCount() == 2);

    assert(registry.InitializeAll().ok());
    assert(comp1->GetLifecycleState() == LifecycleState::Configured);
    assert(comp2->GetLifecycleState() == LifecycleState::Configured);

    assert(registry.StartAll().ok());
    assert(comp1->GetLifecycleState() == LifecycleState::Running);
    assert(comp2->GetLifecycleState() == LifecycleState::Running);

    assert(registry.CheckLiveness());
    assert(registry.CheckReadiness());

    assert(registry.StopAll().ok());
    assert(comp1->GetLifecycleState() == LifecycleState::Stopped);
    assert(comp2->GetLifecycleState() == LifecycleState::Stopped);

    std::cout << "  -> PASSED\n";
}

void TestDependencyGraphDAGAndCycles() {
    std::cout << "[TEST] Running TestDependencyGraphDAGAndCycles...\n";
    DependencyGraph graph;

    std::vector<std::string> executed_order;

    TaskNode nodeA{"A", "TaskA", [&](void*) { executed_order.push_back("A"); return Status::Ok(); }, {}, 10};
    TaskNode nodeB{"B", "TaskB", [&](void*) { executed_order.push_back("B"); return Status::Ok(); }, {"A"}, 5};
    TaskNode nodeC{"C", "TaskC", [&](void*) { executed_order.push_back("C"); return Status::Ok(); }, {"A"}, 8};
    TaskNode nodeD{"D", "TaskD", [&](void*) { executed_order.push_back("D"); return Status::Ok(); }, {"B", "C"}, 1};

    assert(graph.AddTask(nodeA).ok());
    assert(graph.AddTask(nodeB).ok());
    assert(graph.AddTask(nodeC).ok());
    assert(graph.AddTask(nodeD).ok());

    assert(!graph.HasCycle());

    // Check execution waves: Wave 0 = {A}, Wave 1 = {C, B} (C has higher priority), Wave 2 = {D}
    auto waves_or = graph.BuildExecutionWaves();
    assert(waves_or.ok());
    const auto& waves = waves_or.value();
    assert(waves.size() == 3);
    assert(waves[0].size() == 1 && waves[0][0] == "A");
    assert(waves[1].size() == 2);
    assert(waves[2].size() == 1 && waves[2][0] == "D");

    // Execute sequential
    assert(graph.ExecuteSequential().ok());
    assert(executed_order.size() == 4);
    assert(executed_order[0] == "A");
    assert(executed_order[3] == "D");

    // Cycle introduction
    DependencyGraph cyclic_graph;
    cyclic_graph.AddTask({"T1", "T1", nullptr, {"T2"}});
    cyclic_graph.AddTask({"T2", "T2", nullptr, {"T1"}});
    assert(cyclic_graph.HasCycle());
    auto fail_sort = cyclic_graph.TopologicalSort();
    assert(!fail_sort.ok());

    std::cout << "  -> PASSED\n";
}

void TestTaskSchedulerIntervalAndDelayed() {
    std::cout << "[TEST] Running TestTaskSchedulerIntervalAndDelayed...\n";
    TaskScheduler scheduler;

    int interval_ticks = 0;
    int delayed_executed = 0;

    // Schedule interval every 1,000,000 ns (1 ms) starting at 1,000,000 ns
    auto id1 = scheduler.ScheduleInterval("BarAggregator", 1000000, 1000000, [&]() {
        interval_ticks++;
        return Status::Ok();
    });
    assert(id1 > 0);

    // Schedule delayed task at 2,500,000 ns
    auto id2 = scheduler.ScheduleDelayed("SnapshotFlusher", 2500000, [&]() {
        delayed_executed++;
        return Status::Ok();
    });
    assert(id2 > 0);

    // Advance clock to 500,000 ns -> nothing due
    size_t ran = scheduler.Step(500000);
    assert(ran == 0);
    assert(interval_ticks == 0);

    // Advance clock to 1,000,000 ns -> 1st interval tick due
    ran = scheduler.Step(1000000);
    assert(ran == 1);
    assert(interval_ticks == 1);

    // Advance clock to 3,000,000 ns -> interval ticks at 2ms and 3ms, delayed at 2.5ms
    ran = scheduler.Step(3000000);
    assert(ran == 3); // 2 interval + 1 delayed
    assert(delayed_executed == 1);

    // Cancel interval task
    assert(scheduler.Cancel(id1));
    ran = scheduler.Step(10000000);
    assert(ran == 0);

    std::cout << "  -> PASSED\n";
}

class DoublePayloadStage : public IStage {
public:
    const std::string& GetStageName() const override {
        static std::string name = "DoublePayloadStage";
        return name;
    }
    Status Process(const DataBatch& input, DataBatch& output) override {
        output.payload = input.payload;
        for (auto& b : output.payload) {
            b = static_cast<uint8_t>(b * 2);
        }
        return Status::Ok();
    }
};

void TestExecutionPipelineAndBackpressure() {
    std::cout << "[TEST] Running TestExecutionPipelineAndBackpressure...\n";
    ExecutionPipeline pipeline(2, BackpressurePolicy::RejectNew);

    pipeline.AddStage(std::make_shared<DoublePayloadStage>());

    DataBatch b1;
    b1.batch_id = 1;
    b1.payload = {1, 2, 3};

    DataBatch b2;
    b2.batch_id = 2;
    b2.payload = {10, 20};

    DataBatch b3;
    b3.batch_id = 3;

    assert(pipeline.Push(b1).ok());
    assert(pipeline.Push(b2).ok());

    // Queue is full (capacity 2), RejectNew policy should reject b3
    auto reject_st = pipeline.Push(b3);
    assert(!reject_st.ok());

    auto out1_or = pipeline.ProcessNext();
    assert(out1_or.ok());
    assert(out1_or.value().payload == std::vector<uint8_t>({2, 4, 6}));

    // Now b3 can be pushed
    assert(pipeline.Push(b3).ok());

    std::cout << "  -> PASSED\n";
}

class MockStrategyPlugin : public IPlugin {
public:
    const std::string& GetPluginId() const override {
        static std::string id = "com.obsidian.alpha-momentum";
        return id;
    }
    const std::string& GetPluginName() const override {
        static std::string name = "Alpha Momentum";
        return name;
    }
    const std::string& GetVersion() const override {
        static std::string ver = "1.0.0";
        return ver;
    }

    Status OnActivate(void*) override { is_active_ = true; return Status::Ok(); }
    Status OnDeactivate() override { is_active_ = false; return Status::Ok(); }

    Status OnMarketTick(int64_t, int32_t, double, double) override {
        ticks_received_++;
        return Status::Ok();
    }

    bool is_active_{false};
    int ticks_received_{0};
};

void TestPluginManagerAndHooks() {
    std::cout << "[TEST] Running TestPluginManagerAndHooks...\n";
    PluginManager mgr;

    auto plugin = std::make_shared<MockStrategyPlugin>();
    PluginManifest manifest;
    manifest.id = "com.obsidian.alpha-momentum";
    manifest.name = "Alpha Momentum";
    manifest.version = "1.0.0";
    manifest.hooks = {"onMarketTick", "onSignal"};

    assert(mgr.RegisterPlugin(plugin, manifest).ok());
    assert(mgr.PluginCount() == 1);
    assert(mgr.GetPluginState("com.obsidian.alpha-momentum") == PluginState::Registered);

    assert(mgr.ActivatePlugin("com.obsidian.alpha-momentum").ok());
    assert(plugin->is_active_);
    assert(mgr.GetPluginState("com.obsidian.alpha-momentum") == PluginState::Active);

    // Dispatch market ticks
    mgr.DispatchMarketTick(1000, 1, 65000.0, 1.5);
    mgr.DispatchMarketTick(2000, 1, 65050.0, 2.0);
    assert(plugin->ticks_received_ == 2);

    assert(mgr.DeactivatePlugin("com.obsidian.alpha-momentum").ok());
    assert(!plugin->is_active_);

    std::cout << "  -> PASSED\n";
}

void TestRuntimeEngineOrchestration() {
    std::cout << "[TEST] Running TestRuntimeEngineOrchestration...\n";
    EngineConfig cfg;
    cfg.engine_name = "ObsidianMasterEngine";

    RuntimeEngine engine(cfg);
    assert(engine.GetComponentName() == "ObsidianMasterEngine");
    assert(engine.GetLifecycleState() == LifecycleState::Uninitialized);

    assert(engine.Initialize().ok());
    assert(engine.GetLifecycleState() == LifecycleState::Configured);

    assert(engine.Start().ok());
    assert(engine.GetLifecycleState() == LifecycleState::Running);

    auto health = engine.CheckHealth();
    assert(health.status == HealthStatus::Healthy);

    // Step the engine
    size_t work = engine.Step(1000000);
    assert(work == 0); // Nothing scheduled yet

    assert(engine.Stop().ok());
    assert(engine.GetLifecycleState() == LifecycleState::Stopped);

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "=====================================================\n";
    std::cout << "   Obsidian Runtime & Orchestration Verification Suite\n";
    std::cout << "=====================================================\n";

    try {
        TestEventBusTopicAndTypeSubscription();
        TestServiceRegistryAndLifecycle();
        TestDependencyGraphDAGAndCycles();
        TestTaskSchedulerIntervalAndDelayed();
        TestExecutionPipelineAndBackpressure();
        TestPluginManagerAndHooks();
        TestRuntimeEngineOrchestration();

        std::cout << "\n>>> ALL OBSIDIAN RUNTIME TESTS PASSED! <<<\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR during test execution: " << e.what() << "\n";
        return 1;
    }
}
