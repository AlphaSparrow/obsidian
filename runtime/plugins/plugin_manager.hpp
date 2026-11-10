#pragma once

#include "runtime/plugins/plugin_interface.hpp"
#include "runtime/plugins/plugin_manifest.hpp"
#include "storage/common/status.hpp"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>

namespace obsidian {
namespace runtime {
namespace plugins {

enum class PluginState : uint8_t {
    Registered = 0,
    Active = 1,
    Deactivated = 2,
    Faulted = 3
};

struct PluginEntry {
    std::shared_ptr<IPlugin> plugin;
    PluginManifest manifest;
    PluginState state{PluginState::Registered};
    uint64_t ticks_processed{0};
    uint64_t signals_processed{0};
    uint64_t faults{0};
};

struct PluginManagerMetrics {
    size_t total_registered{0};
    size_t active_plugins{0};
    size_t faulted_plugins{0};
    uint64_t ticks_dispatched{0};
    uint64_t signals_dispatched{0};
    uint64_t fault_count{0};
};

class PluginManager {
public:
    PluginManager() = default;
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // Registers a plugin instance with its validated manifest
    storage::Status RegisterPlugin(std::shared_ptr<IPlugin> plugin, PluginManifest manifest);

    // Activates a registered plugin
    storage::Status ActivatePlugin(const std::string& plugin_id, void* host_context = nullptr);

    // Deactivates an active plugin
    storage::Status DeactivatePlugin(const std::string& plugin_id);

    // Dispatches market ticks to all active plugins declaring the onMarketTick hook
    void DispatchMarketTick(int64_t timestamp_ns, int32_t symbol_id, double price, double volume);

    // Dispatches signals to all active plugins declaring the onSignal hook
    void DispatchSignal(const std::string& strategy_id, int32_t symbol_id, int32_t direction, double score);

    // Unregisters all plugins and deactivates active ones
    void Clear();

    [[nodiscard]] size_t PluginCount() const;
    [[nodiscard]] PluginState GetPluginState(const std::string& plugin_id) const;
    [[nodiscard]] PluginManagerMetrics GetMetrics() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, PluginEntry> plugins_;

    std::atomic<uint64_t> ticks_dispatched_{0};
    std::atomic<uint64_t> signals_dispatched_{0};
    std::atomic<uint64_t> fault_count_{0};
};

} // namespace plugins
} // namespace runtime
} // namespace obsidian
