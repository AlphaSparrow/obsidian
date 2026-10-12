#pragma once

#include "lakehouse_structures.hpp"
#include "partition_spec.hpp"
#include "storage/common/file_system.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <memory>

namespace obsidian {
namespace storage {
namespace lakehouse {

class ManifestManager {
public:
    explicit ManifestManager(std::shared_ptr<IFileSystem> fs = LocalFileSystem::Default())
        : fs_(std::move(fs)) {}

    // Writes an immutable manifest file to disk atomically
    StatusOr<ManifestFile> WriteManifest(
        const std::string& manifest_path,
        int64_t snapshot_id,
        int32_t partition_spec_id,
        const std::vector<ManifestEntry>& entries);

    // Reads and deserializes all manifest entries from a manifest file
    StatusOr<std::vector<ManifestEntry>> ReadManifest(const std::string& manifest_path);

    // Filters active data files using partition predicates and columnar min/max statistics
    [[nodiscard]] std::vector<DataFile> FilterFiles(
        const std::vector<ManifestEntry>& entries,
        const std::vector<ColumnPredicate>& column_predicates = {},
        const std::vector<PartitionValue>& partition_filters = {}) const;

private:
    std::shared_ptr<IFileSystem> fs_;
};

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
