#pragma once

#include "storage/common/status.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <optional>

namespace obsidian {
namespace storage {
namespace snapshots {

struct TableSnapshot {
    int64_t snapshot_id{-1};
    int64_t parent_snapshot_id{-1};
    int64_t timestamp_epoch_ms{0};
    std::string manifest_path;
    std::string operation; // "append", "overwrite", "compaction"
    std::vector<std::string> modified_partitions;
    int64_t added_data_files{0};
    int64_t added_records{0};
    int64_t added_bytes{0};
    int32_t pin_count{0};
};

class SnapshotManager {
public:
    SnapshotManager() = default;
    ~SnapshotManager() = default;

    // Registers a new snapshot committed to the table
    Status CommitSnapshot(
        int64_t snapshot_id,
        int64_t parent_snapshot_id,
        int64_t timestamp_epoch_ms,
        const std::string& manifest_path,
        const std::string& operation,
        const std::vector<std::string>& modified_partitions,
        int64_t added_data_files,
        int64_t added_records,
        int64_t added_bytes);

    // Look up snapshot by exact ID
    StatusOr<TableSnapshot> GetSnapshotById(int64_t snapshot_id) const;

    // Point-in-time time-travel: finds the latest valid snapshot at or before specified timestamp
    StatusOr<TableSnapshot> GetSnapshotAsOf(int64_t timestamp_epoch_ms) const;

    // Returns current table HEAD snapshot
    StatusOr<TableSnapshot> GetCurrentSnapshot() const;

    // Validates lineage integrity from HEAD back to root snapshot.
    // Detects cyclic parent references, dangling parent pointers, and timestamp regressions.
    Status ValidateLineage() const;

    // Checks for Optimistic Concurrency Control (OCC) conflicts:
    // Checks if any snapshot committed after base_snapshot_id touched any of target_partitions.
    Status DetectCommitConflicts(
        int64_t base_snapshot_id,
        const std::vector<std::string>& target_partitions) const;

    // Pins a snapshot to protect it against vacuum / physical deletion during long-running analytical query
    Status PinSnapshot(int64_t snapshot_id);

    // Unpins a snapshot when query completes
    Status UnpinSnapshot(int64_t snapshot_id);

    // Checks if a snapshot can be safely expired/garbage collected
    Status CanExpireSnapshot(int64_t snapshot_id) const;

    // Returns all active snapshot IDs
    std::vector<int64_t> ListSnapshotIds() const;

private:
    mutable std::shared_mutex mutex_;
    int64_t current_snapshot_id_{-1};
    std::unordered_map<int64_t, TableSnapshot> snapshots_;
};

} // namespace snapshots
} // namespace storage
} // namespace obsidian
