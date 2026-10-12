#pragma once

#include "lakehouse_structures.hpp"
#include "partition_spec.hpp"
#include "manifest_manager.hpp"
#include "storage/schema/market_data_schema.hpp"
#include "storage/common/file_system.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <optional>

namespace obsidian {
namespace storage {
namespace lakehouse {

class Transaction;
enum class OperationType : uint8_t;

class LakehouseTable : public std::enable_shared_from_this<LakehouseTable> {
public:
    LakehouseTable(
        std::string location,
        TableMetadata metadata,
        schema::Schema schema,
        PartitionSpec partition_spec,
        std::shared_ptr<IFileSystem> fs);

    // Factory to initialize and persist a new Lakehouse table
    static StatusOr<std::shared_ptr<LakehouseTable>> Create(
        std::string location,
        std::string table_name,
        schema::Schema schema,
        PartitionSpec partition_spec = {},
        std::shared_ptr<IFileSystem> fs = LocalFileSystem::Default());

    // Factory to load an existing Lakehouse table from storage
    static StatusOr<std::shared_ptr<LakehouseTable>> Open(
        std::string location,
        std::shared_ptr<IFileSystem> fs = LocalFileSystem::Default());

    [[nodiscard]] const std::string& location() const noexcept { return location_; }
    [[nodiscard]] const std::string& table_name() const noexcept { return metadata_.table_name; }
    [[nodiscard]] const schema::Schema& schema() const noexcept { return schema_; }
    [[nodiscard]] const PartitionSpec& partition_spec() const noexcept { return partition_spec_; }
    [[nodiscard]] int64_t current_snapshot_id() const;

    // Snapshot retrieval
    [[nodiscard]] std::optional<Snapshot> current_snapshot() const;
    [[nodiscard]] std::optional<Snapshot> GetSnapshot(int64_t snapshot_id) const;
    [[nodiscard]] std::optional<Snapshot> GetSnapshotAsOfTimestamp(int64_t timestamp_ms) const;

    // Time-travel and predicate-pushdown file scan
    StatusOr<std::vector<DataFile>> Scan(
        const std::vector<ColumnPredicate>& predicates = {},
        const std::vector<PartitionValue>& partition_filters = {},
        int64_t snapshot_id = -1) const;

    // Create a new transaction on this table
    std::unique_ptr<Transaction> NewTransaction(OperationType op);

    // Low-level atomic commit called by Transaction
    Status CommitTransaction(
        int64_t base_snapshot_id,
        OperationType op,
        const std::vector<DataFile>& added_files,
        const std::vector<DataFile>& deleted_files);

    // Metadata & manifest paths
    [[nodiscard]] std::string delta_log_path() const;
    [[nodiscard]] std::string manifests_dir_path() const;

private:
    Status SaveMetadataUnlocked();
    static StatusOr<TableMetadata> LoadMetadata(
        const std::string& location,
        const std::shared_ptr<IFileSystem>& fs);

    std::string location_;
    TableMetadata metadata_;
    schema::Schema schema_;
    PartitionSpec partition_spec_;
    std::shared_ptr<IFileSystem> fs_;
    std::unique_ptr<ManifestManager> manifest_mgr_;
    mutable std::mutex mutex_;
};

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
