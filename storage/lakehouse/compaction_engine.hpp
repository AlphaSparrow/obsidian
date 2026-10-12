#pragma once

#include "lakehouse_structures.hpp"
#include "lakehouse_table.hpp"
#include "transaction.hpp"
#include "storage/common/file_system.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace obsidian {
namespace storage {
namespace lakehouse {

struct CompactionOptions {
    int64_t target_file_size_bytes{128 * 1024 * 1024}; // 128 MB default target
    int32_t min_files_to_compact{2};
    int32_t max_files_per_group{50};
};

struct FileGroup {
    std::vector<PartitionValue> partitions;
    std::vector<DataFile> files;
    int64_t total_size_bytes{0};
};

struct CompactionPlan {
    std::vector<FileGroup> groups;
};

struct VacuumOptions {
    int64_t retention_threshold_ms{7 * 24 * 60 * 60 * 1000LL}; // 7 days default
    bool dry_run{false};
};

struct VacuumResult {
    int32_t deleted_files_count{0};
    int64_t freed_bytes{0};
    int32_t expired_snapshots_count{0};
    std::vector<std::string> deleted_file_paths;
};

class CompactionEngine {
public:
    // Identifies candidate small files within identical partition specs that need merging
    static StatusOr<CompactionPlan> Plan(
        const LakehouseTable& table,
        const CompactionOptions& options = {});

    // Executes compaction plan by committing a Compaction transaction
    static Status Execute(
        const std::shared_ptr<LakehouseTable>& table,
        const CompactionPlan& plan,
        const std::function<StatusOr<DataFile>(const FileGroup&)>& file_merger);
};

class VacuumEngine {
public:
    explicit VacuumEngine(std::shared_ptr<IFileSystem> fs = LocalFileSystem::Default())
        : fs_(std::move(fs)) {}

    // Purges unreferenced data files past retention period and cleans expired snapshot manifests
    StatusOr<VacuumResult> Vacuum(
        const LakehouseTable& table,
        const VacuumOptions& options = {});

private:
    std::shared_ptr<IFileSystem> fs_;
};

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
