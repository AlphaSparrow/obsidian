#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

namespace obsidian {
namespace storage {
namespace lakehouse {

// Super basic Lakehouse architecture structures (inspired by Delta Lake / Iceberg)

struct PartitionValue {
    std::string name;
    std::string value;
};

struct DataFile {
    std::string path;           // Path to the parquet data file
    std::string format;         // e.g., "parquet"
    int64_t file_size_bytes;
    int64_t record_count;
    std::vector<PartitionValue> partitions;
    
    // Min/Max statistics for query optimization (data skipping)
    std::unordered_map<std::string, std::string> min_values;
    std::unordered_map<std::string, std::string> max_values;
};

struct ManifestEntry {
    enum class Status {
        EXISTING = 0,
        ADDED = 1,
        DELETED = 2
    };
    
    Status status;
    int64_t snapshot_id;
    DataFile data_file;
};

struct ManifestFile {
    std::string path;
    int64_t length;
    int32_t partition_spec_id;
    int64_t added_snapshot_id;
    int32_t added_data_files_count;
    int32_t existing_data_files_count;
    int32_t deleted_data_files_count;
};

struct Snapshot {
    int64_t snapshot_id;
    int64_t parent_snapshot_id;
    int64_t timestamp_ms;
    std::string manifest_list_path; // Path to the list of manifests for this snapshot
    std::string operation;          // e.g., "append", "overwrite", "delete"
};

struct TableMetadata {
    std::string table_name;
    std::string location;           // Base directory for the table in storage
    int32_t format_version;
    int64_t current_snapshot_id;
    std::vector<Snapshot> snapshots;
};

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
