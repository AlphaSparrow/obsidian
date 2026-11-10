#pragma once

#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <algorithm>

namespace obsidian {
namespace runtime {
namespace plugins {

struct PluginManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string api_version{"^2.0"};
    std::string description;
    std::string author;
    std::vector<std::string> hooks;
    std::vector<std::string> permissions;

    [[nodiscard]] storage::Status Validate() const {
        if (id.empty()) {
            return storage::Status::InvalidArgument("Plugin manifest validation failed: 'id' cannot be empty");
        }
        if (name.empty()) {
            return storage::Status::InvalidArgument("Plugin manifest validation failed: 'name' cannot be empty");
        }
        if (version.empty()) {
            return storage::Status::InvalidArgument("Plugin manifest validation failed: 'version' cannot be empty");
        }
        return storage::Status::Ok();
    }

    [[nodiscard]] bool HasHook(const std::string& hook_name) const {
        return std::find(hooks.begin(), hooks.end(), hook_name) != hooks.end();
    }

    [[nodiscard]] bool HasPermission(const std::string& perm) const {
        return std::find(permissions.begin(), permissions.end(), perm) != permissions.end();
    }
};

} // namespace plugins
} // namespace runtime
} // namespace obsidian
