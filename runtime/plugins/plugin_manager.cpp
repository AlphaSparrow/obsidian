#include "runtime/plugins/plugin_manager.hpp"

namespace obsidian {
namespace runtime {
namespace plugins {

PluginManager::~PluginManager() {
    Clear();
}

storage::Status PluginManager::RegisterPlugin(std::shared_ptr<IPlugin> plugin, PluginManifest manifest) {
    if (!plugin) {
        return storage::Status::InvalidArgument("Cannot register null plugin instance");
    }

    auto val_st = manifest.Validate();
    if (!val_st.ok()) {
        return val_st;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (plugins_.find(manifest.id) != plugins_.end()) {
        return storage::Status::AlreadyExists("Plugin with ID already registered: " + manifest.id);
    }

    PluginEntry entry;
    entry.plugin = std::move(plugin);
    entry.manifest = std::move(manifest);
    entry.state = PluginState::Registered;

    plugins_[entry.manifest.id] = std::move(entry);
    return storage::Status::Ok();
}

storage::Status PluginManager::ActivatePlugin(const std::string& plugin_id, void* host_context) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = plugins_.find(plugin_id);
    if (it == plugins_.end()) {
        return storage::Status::NotFound("Plugin not registered: " + plugin_id);
    }

    if (it->second.state == PluginState::Active) {
        return storage::Status::Ok(); // Already active
    }

    try {
        auto st = it->second.plugin->OnActivate(host_context);
        if (!st.ok()) {
            it->second.state = PluginState::Faulted;
            it->second.faults++;
            fault_count_.fetch_add(1, std::memory_order_relaxed);
            return storage::Status::Internal("Plugin '" + plugin_id + "' activation failed: " + st.message());
        }
        it->second.state = PluginState::Active;
    } catch (const std::exception& e) {
        it->second.state = PluginState::Faulted;
        it->second.faults++;
        fault_count_.fetch_add(1, std::memory_order_relaxed);
        return storage::Status::Internal("Plugin '" + plugin_id + "' threw exception during activation: " + e.what());
    }

    return storage::Status::Ok();
}

storage::Status PluginManager::DeactivatePlugin(const std::string& plugin_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = plugins_.find(plugin_id);
    if (it == plugins_.end()) {
        return storage::Status::NotFound("Plugin not registered: " + plugin_id);
    }

    if (it->second.state != PluginState::Active) {
        return storage::Status::Ok();
    }

    try {
        (void)it->second.plugin->OnDeactivate();
    } catch (...) {
        // Defensive
    }

    it->second.state = PluginState::Deactivated;
    return storage::Status::Ok();
}

void PluginManager::DispatchMarketTick(int64_t timestamp_ns, int32_t symbol_id, double price, double volume) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (auto& [id, entry] : plugins_) {
        if (entry.state == PluginState::Active && (entry.manifest.hooks.empty() || entry.manifest.HasHook("onMarketTick"))) {
            try {
                auto st = entry.plugin->OnMarketTick(timestamp_ns, symbol_id, price, volume);
                if (st.ok()) {
                    entry.ticks_processed++;
                    ticks_dispatched_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    entry.faults++;
                    fault_count_.fetch_add(1, std::memory_order_relaxed);
                }
            } catch (...) {
                entry.faults++;
                fault_count_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

void PluginManager::DispatchSignal(const std::string& strategy_id, int32_t symbol_id, int32_t direction, double score) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (auto& [id, entry] : plugins_) {
        if (entry.state == PluginState::Active && (entry.manifest.hooks.empty() || entry.manifest.HasHook("onSignal"))) {
            try {
                auto st = entry.plugin->OnSignal(strategy_id, symbol_id, direction, score);
                if (st.ok()) {
                    entry.signals_processed++;
                    signals_dispatched_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    entry.faults++;
                    fault_count_.fetch_add(1, std::memory_order_relaxed);
                }
            } catch (...) {
                entry.faults++;
                fault_count_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

void PluginManager::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto& [id, entry] : plugins_) {
        if (entry.state == PluginState::Active) {
            try {
                (void)entry.plugin->OnDeactivate();
            } catch (...) {}
        }
    }
    plugins_.clear();
}

size_t PluginManager::PluginCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return plugins_.size();
}

PluginState PluginManager::GetPluginState(const std::string& plugin_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = plugins_.find(plugin_id);
    if (it != plugins_.end()) {
        return it->second.state;
    }
    return PluginState::Registered;
}

PluginManagerMetrics PluginManager::GetMetrics() const {
    PluginManagerMetrics m;
    m.ticks_dispatched = ticks_dispatched_.load(std::memory_order_relaxed);
    m.signals_dispatched = signals_dispatched_.load(std::memory_order_relaxed);
    m.fault_count = fault_count_.load(std::memory_order_relaxed);

    std::shared_lock<std::shared_mutex> lock(mutex_);
    m.total_registered = plugins_.size();
    for (const auto& [id, entry] : plugins_) {
        if (entry.state == PluginState::Active) m.active_plugins++;
        if (entry.state == PluginState::Faulted) m.faulted_plugins++;
    }

    return m;
}

} // namespace plugins
} // namespace runtime
} // namespace obsidian
