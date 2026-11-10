#pragma once

#include "runtime/registry/component_lifecycle.hpp"
#include "storage/common/status.hpp"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <shared_mutex>
#include <mutex>

namespace obsidian {
namespace runtime {
namespace registry {

class ServiceRegistry {
public:
    ServiceRegistry() = default;
    ~ServiceRegistry() = default;

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    // Type-safe service registration
    template <typename Interface>
    storage::Status RegisterService(std::shared_ptr<Interface> service) {
        if (!service) {
            return storage::Status::InvalidArgument("Cannot register null service instance");
        }
        std::unique_lock<std::shared_mutex> lock(services_mutex_);
        std::type_index key(typeid(Interface));
        if (services_.find(key) != services_.end()) {
            return storage::Status::AlreadyExists("Service type already registered in registry");
        }
        services_[key] = service;
        return storage::Status::Ok();
    }

    // Type-safe service retrieval
    template <typename Interface>
    storage::StatusOr<std::shared_ptr<Interface>> GetService() const {
        std::shared_lock<std::shared_mutex> lock(services_mutex_);
        std::type_index key(typeid(Interface));
        auto it = services_.find(key);
        if (it == services_.end()) {
            return storage::Status::NotFound("Requested service interface not found in registry");
        }
        return std::static_pointer_cast<Interface>(it->second);
    }

    // Check if service is registered
    template <typename Interface>
    bool HasService() const {
        std::shared_lock<std::shared_mutex> lock(services_mutex_);
        return services_.find(std::type_index(typeid(Interface))) != services_.end();
    }

    // Component registration for lifecycle management
    storage::Status RegisterComponent(std::shared_ptr<IComponent> component);

    // Retrieve registered component by name
    std::shared_ptr<IComponent> GetComponent(const std::string& name) const;

    // Lifecycle transitions
    storage::Status InitializeAll();
    storage::Status StartAll();
    storage::Status PauseAll();
    storage::Status ResumeAll();
    storage::Status StopAll();

    // Health probes (matching docs/architecture/05_runtime_design.md)
    [[nodiscard]] bool CheckLiveness() const;
    [[nodiscard]] bool CheckReadiness() const;
    [[nodiscard]] std::vector<std::pair<std::string, HealthCheckResult>> CheckAllHealth() const;

    // Clear registry
    void Clear();

    [[nodiscard]] size_t ComponentCount() const;
    [[nodiscard]] size_t ServiceCount() const;

private:
    mutable std::shared_mutex services_mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;

    mutable std::shared_mutex components_mutex_;
    std::vector<std::shared_ptr<IComponent>> components_;
    std::unordered_map<std::string, std::shared_ptr<IComponent>> component_map_;
};

} // namespace registry
} // namespace runtime
} // namespace obsidian
