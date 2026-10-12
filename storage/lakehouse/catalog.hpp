#pragma once

#include "lakehouse_table.hpp"
#include "partition_spec.hpp"
#include "storage/schema/market_data_schema.hpp"
#include "storage/common/file_system.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace obsidian {
namespace storage {
namespace lakehouse {

class LakehouseCatalog {
public:
    explicit LakehouseCatalog(
        std::string warehouse_root,
        std::shared_ptr<IFileSystem> fs = LocalFileSystem::Default());

    [[nodiscard]] const std::string& warehouse_root() const noexcept {
        return warehouse_root_;
    }

    // Namespace operations
    Status CreateNamespace(const std::string& ns);
    StatusOr<std::vector<std::string>> ListNamespaces();

    // Table lifecycle operations
    StatusOr<std::shared_ptr<LakehouseTable>> CreateTable(
        const std::string& ns,
        const std::string& table_name,
        schema::Schema schema,
        PartitionSpec spec = {});

    StatusOr<std::shared_ptr<LakehouseTable>> LoadTable(
        const std::string& ns,
        const std::string& table_name);

    Status DropTable(
        const std::string& ns,
        const std::string& table_name,
        bool purge_data = false);

    StatusOr<std::vector<std::string>> ListTables(const std::string& ns);

    StatusOr<bool> TableExists(const std::string& ns, const std::string& table_name);

private:
    std::string TableLocation(const std::string& ns, const std::string& table_name) const;

    std::string warehouse_root_;
    std::shared_ptr<IFileSystem> fs_;
    std::unordered_map<std::string, std::shared_ptr<LakehouseTable>> table_cache_;
    std::mutex mutex_;
};

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
