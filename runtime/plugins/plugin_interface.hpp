#pragma once

#include "storage/common/status.hpp"
#include <string>
#include <cstdint>

namespace obsidian {
namespace runtime {
namespace plugins {

class IPlugin {
public:
    virtual ~IPlugin() = default;

    [[nodiscard]] virtual const std::string& GetPluginId() const = 0;
    [[nodiscard]] virtual const std::string& GetPluginName() const = 0;
    [[nodiscard]] virtual const std::string& GetVersion() const = 0;

    virtual storage::Status OnActivate(void* host_context) = 0;
    virtual storage::Status OnDeactivate() = 0;

    // Optional event and market data hooks
    virtual storage::Status OnMarketTick(int64_t timestamp_ns, int32_t symbol_id, double price, double volume) {
        return storage::Status::Ok();
    }

    virtual storage::Status OnSignal(const std::string& strategy_id, int32_t symbol_id, int32_t direction, double score) {
        return storage::Status::Ok();
    }
};

} // namespace plugins
} // namespace runtime
} // namespace obsidian
