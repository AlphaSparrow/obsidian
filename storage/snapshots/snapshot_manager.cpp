#include "storage/snapshots/snapshot_manager.hpp"
#include <algorithm>
#include <mutex>

namespace obsidian {
namespace storage {
namespace snapshots {

Status SnapshotManager::CommitSnapshot(
    int64_t snapshot_id,
    int64_t parent_snapshot_id,
    int64_t timestamp_epoch_ms,
    const std::string& manifest_path,
    const std::string& operation,
    const std::vector<std::string>& modified_partitions,
    int64_t added_data_files,
    int64_t added_records,
    int64_t added_bytes) {
    if (snapshot_id <= 0) {
        return Status::InvalidArgument("Snapshot ID must be positive: " + std::to_string(snapshot_id));
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (snapshots_.find(snapshot_id) != snapshots_.end()) {
        return Status::AlreadyExists("Snapshot ID already exists in catalog: " + std::to_string(snapshot_id));
    }

    // If parent is specified, verify parent exists
    if (parent_snapshot_id > 0) {
        auto parent_it = snapshots_.find(parent_snapshot_id);
        if (parent_it == snapshots_.end()) {
            return Status::Corruption("Dangling parent snapshot reference: parent " + 
                                      std::to_string(parent_snapshot_id) + " not found");
        }
        if (timestamp_epoch_ms < parent_it->second.timestamp_epoch_ms) {
            return Status::Corruption("Snapshot commit timestamp (" + std::to_string(timestamp_epoch_ms) + 
                                      ") is earlier than parent timestamp (" + 
                                      std::to_string(parent_it->second.timestamp_epoch_ms) + ")");
        }
    }

    TableSnapshot snap;
    snap.snapshot_id = snapshot_id;
    snap.parent_snapshot_id = parent_snapshot_id;
    snap.timestamp_epoch_ms = timestamp_epoch_ms;
    snap.manifest_path = manifest_path;
    snap.operation = operation;
    snap.modified_partitions = modified_partitions;
    snap.added_data_files = added_data_files;
    snap.added_records = added_records;
    snap.added_bytes = added_bytes;
    snap.pin_count = 0;

    snapshots_[snapshot_id] = std::move(snap);
    current_snapshot_id_ = snapshot_id;

    return Status::Ok();
}

StatusOr<TableSnapshot> SnapshotManager::GetSnapshotById(int64_t snapshot_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        return Status::NotFound("Snapshot not found: " + std::to_string(snapshot_id));
    }
    return it->second;
}

StatusOr<TableSnapshot> SnapshotManager::GetSnapshotAsOf(int64_t timestamp_epoch_ms) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (snapshots_.empty()) {
        return Status::NotFound("No snapshots available in table catalog");
    }

    const TableSnapshot* best_match = nullptr;
    for (const auto& [id, snap] : snapshots_) {
        if (snap.timestamp_epoch_ms <= timestamp_epoch_ms) {
            if (!best_match || snap.timestamp_epoch_ms > best_match->timestamp_epoch_ms) {
                best_match = &snap;
            }
        }
    }

    if (!best_match) {
        return Status::NotFound("No snapshot exists as of timestamp " + std::to_string(timestamp_epoch_ms));
    }

    return *best_match;
}

StatusOr<TableSnapshot> SnapshotManager::GetCurrentSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (current_snapshot_id_ <= 0) {
        return Status::NotFound("Table has no committed snapshots");
    }
    auto it = snapshots_.find(current_snapshot_id_);
    if (it == snapshots_.end()) {
        return Status::Corruption("Head snapshot ID pointer is corrupted: " + std::to_string(current_snapshot_id_));
    }
    return it->second;
}

Status SnapshotManager::ValidateLineage() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (snapshots_.empty()) {
        return Status::Ok();
    }

    if (current_snapshot_id_ <= 0) {
        return Status::Corruption("Lineage validation error: table has snapshots but head ID is unset");
    }

    std::unordered_set<int64_t> visited;
    int64_t curr_id = current_snapshot_id_;

    while (curr_id > 0) {
        if (visited.find(curr_id) != visited.end()) {
            return Status::Corruption("Lineage loop detected: cyclic reference found at snapshot " + std::to_string(curr_id));
        }
        visited.insert(curr_id);

        auto it = snapshots_.find(curr_id);
        if (it == snapshots_.end()) {
            return Status::Corruption("Lineage gap detected: missing intermediate snapshot " + std::to_string(curr_id));
        }

        int64_t parent_id = it->second.parent_snapshot_id;
        if (parent_id > 0) {
            auto parent_it = snapshots_.find(parent_id);
            if (parent_it == snapshots_.end()) {
                return Status::Corruption("Lineage broken: snapshot " + std::to_string(curr_id) + 
                                          " references missing parent " + std::to_string(parent_id));
            }
            if (it->second.timestamp_epoch_ms < parent_it->second.timestamp_epoch_ms) {
                return Status::Corruption("Lineage temporal regression: snapshot " + std::to_string(curr_id) + 
                                          " is older than parent " + std::to_string(parent_id));
            }
        }
        curr_id = parent_id;
    }

    return Status::Ok();
}

Status SnapshotManager::DetectCommitConflicts(
    int64_t base_snapshot_id,
    const std::vector<std::string>& target_partitions) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (base_snapshot_id == current_snapshot_id_) {
        // No new commits since base snapshot
        return Status::Ok();
    }

    std::unordered_set<std::string> conflict_partitions(target_partitions.begin(), target_partitions.end());
    if (conflict_partitions.empty()) {
        return Status::Ok();
    }

    // Trace snapshots from current_snapshot_id back to base_snapshot_id
    int64_t curr = current_snapshot_id_;
    while (curr > 0 && curr != base_snapshot_id) {
        auto it = snapshots_.find(curr);
        if (it == snapshots_.end()) {
            return Status::Corruption("Broken snapshot lineage while checking conflicts: missing ID " + std::to_string(curr));
        }

        for (const auto& part : it->second.modified_partitions) {
            if (conflict_partitions.find(part) != conflict_partitions.end()) {
                return Status::ConcurrentModification(
                    "Optimistic concurrency conflict detected: partition '" + part + 
                    "' was modified by concurrent snapshot " + std::to_string(curr) + 
                    " (" + it->second.operation + ") after base snapshot " + std::to_string(base_snapshot_id));
            }
        }

        curr = it->second.parent_snapshot_id;
    }

    return Status::Ok();
}

Status SnapshotManager::PinSnapshot(int64_t snapshot_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        return Status::NotFound("Cannot pin non-existent snapshot: " + std::to_string(snapshot_id));
    }
    it->second.pin_count++;
    return Status::Ok();
}

Status SnapshotManager::UnpinSnapshot(int64_t snapshot_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        return Status::NotFound("Cannot unpin non-existent snapshot: " + std::to_string(snapshot_id));
    }
    if (it->second.pin_count <= 0) {
        return Status::InvalidArgument("Snapshot " + std::to_string(snapshot_id) + " pin count is already zero");
    }
    it->second.pin_count--;
    return Status::Ok();
}

Status SnapshotManager::CanExpireSnapshot(int64_t snapshot_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        return Status::NotFound("Snapshot not found: " + std::to_string(snapshot_id));
    }
    if (snapshot_id == current_snapshot_id_) {
        return Status::InvalidArgument("Cannot expire current HEAD snapshot " + std::to_string(snapshot_id));
    }
    if (it->second.pin_count > 0) {
        return Status::InvalidArgument("Cannot expire snapshot " + std::to_string(snapshot_id) + 
                                       ": currently pinned by " + std::to_string(it->second.pin_count) + 
                                       " active analytical queries");
    }
    return Status::Ok();
}

std::vector<int64_t> SnapshotManager::ListSnapshotIds() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<int64_t> ids;
    ids.reserve(snapshots_.size());
    for (const auto& [id, _] : snapshots_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace snapshots
} // namespace storage
} // namespace obsidian
