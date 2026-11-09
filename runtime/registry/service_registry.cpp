#include "runtime/registry/service_registry.hpp"

namespace obsidian {
namespace runtime {
namespace registry {

storage::Status ServiceRegistry::RegisterComponent(std::shared_ptr<IComponent> component) {
    if (!component) {
        return storage::Status::InvalidArgument("Cannot register null component");
    }

    std::unique_lock<std::shared_mutex> lock(components_mutex_);
    const std::string& name = component->GetComponentName();
    if (component_map_.find(name) != component_map_.end()) {
        return storage::Status::AlreadyExists("Component already registered: " + name);
    }

    components_.push_back(component);
    component_map_[name] = component;
    return storage::Status::Ok();
}

std::shared_ptr<IComponent> ServiceRegistry::GetComponent(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    auto it = component_map_.find(name);
    if (it != component_map_.end()) {
        return it->second;
    }
    return nullptr;
}

storage::Status ServiceRegistry::InitializeAll() {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    for (auto& comp : components_) {
        auto st = comp->Initialize();
        if (!st.ok()) {
            return storage::Status::Internal("Failed to initialize component '" + comp->GetComponentName() + "': " + st.message());
        }
    }
    return storage::Status::Ok();
}

storage::Status ServiceRegistry::StartAll() {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    for (auto& comp : components_) {
        auto st = comp->Start();
        if (!st.ok()) {
            return storage::Status::Internal("Failed to start component '" + comp->GetComponentName() + "': " + st.message());
        }
    }
    return storage::Status::Ok();
}

storage::Status ServiceRegistry::PauseAll() {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    for (auto& comp : components_) {
        auto st = comp->Pause();
        if (!st.ok()) {
            return st;
        }
    }
    return storage::Status::Ok();
}

storage::Status ServiceRegistry::ResumeAll() {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    for (auto& comp : components_) {
        auto st = comp->Resume();
        if (!st.ok()) {
            return st;
        }
    }
    return storage::Status::Ok();
}

storage::Status ServiceRegistry::StopAll() {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    // Stop in reverse order of initialization
    for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
        auto st = (*it)->Stop();
        if (!st.ok()) {
            // Log or record error but continue stopping other components
        }
    }
    return storage::Status::Ok();
}

bool ServiceRegistry::CheckLiveness() const {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    for (const auto& comp : components_) {
        if (comp->GetLifecycleState() == LifecycleState::Faulted) {
            return false;
        }
        auto health = comp->CheckHealth();
        if (health.status == HealthStatus::Unhealthy) {
            return false;
        }
    }
    return true;
}

bool ServiceRegistry::CheckReadiness() const {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    for (const auto& comp : components_) {
        if (comp->GetLifecycleState() != LifecycleState::Running) {
            return false;
        }
        auto health = comp->CheckHealth();
        if (health.status != HealthStatus::Healthy) {
            return false;
        }
    }
    return true;
}

std::vector<std::pair<std::string, HealthCheckResult>> ServiceRegistry::CheckAllHealth() const {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    std::vector<std::pair<std::string, HealthCheckResult>> results;
    results.reserve(components_.size());
    for (const auto& comp : components_) {
        results.emplace_back(comp->GetComponentName(), comp->CheckHealth());
    }
    return results;
}

void ServiceRegistry::Clear() {
    {
        std::unique_lock<std::shared_mutex> lock(services_mutex_);
        services_.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(components_mutex_);
        components_.clear();
        component_map_.clear();
    }
}

size_t ServiceRegistry::ComponentCount() const {
    std::shared_lock<std::shared_mutex> lock(components_mutex_);
    return components_.size();
}

size_t ServiceRegistry::ServiceCount() const {
    std::shared_lock<std::shared_mutex> lock(services_mutex_);
    return services_.size();
}

} // namespace registry
} // namespace runtime
} // namespace obsidian
