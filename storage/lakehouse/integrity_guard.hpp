#pragma once

#include "lakehouse_table.hpp"
#include "lakehouse_structures.hpp"
#include "storage/common/file_system.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <memory>

namespace obsidian {
namespace storage {
namespace lakehouse {

struct IntegrityReport {
    bool is_healthy{true};
    int32_t active_data_files{0};
    int32_t missing_data_files{0};
    int32_t orphan_data_files{0};
    int64_t orphan_leaked_bytes{0};
    int32_t boundary_anomalies_count{0};
    std::vector<std::string> detected_issues;
    std::vector<std::string> orphan_file_paths;
};

class LakehouseIntegrityGuard {
public:
    explicit LakehouseIntegrityGuard(std::shared_ptr<IFileSystem> fs = LocalFileSystem::Default());

    // Performs an exhaustive audit of table snapshots, manifest consistency, data file presence,
    // min/max statistics sanity, and orphan staging file leakage.
    StatusOr<IntegrityReport> Audit(
        const LakehouseTable& table,
        int64_t snapshot_id = -1);

    // Purges identified orphan staging files from aborted/crashed transactions
    Status CleanOrphanFiles(
        const std::vector<std::string>& orphan_paths);

private:
    std::shared_ptr<IFileSystem> fs_;

    void CheckBoundaryAnomalies(
        const DataFile& file,
        IntegrityReport& report);

    void FindOrphanFiles(
        const std::string& dir_path,
        const std::unordered_set<std::string>& active_paths,
        IntegrityReport& report);
};

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
