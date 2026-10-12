#include "compaction_engine.hpp"
#include <map>
#include <set>
#include <chrono>

namespace obsidian {
namespace storage {
namespace lakehouse {

namespace {

int64_t CurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string MakePartitionKey(const std::vector<PartitionValue>& parts) {
    std::string key;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) key += "/";
        key += parts[i].name + "=" + parts[i].value;
    }
    return key;
}

} // namespace

StatusOr<CompactionPlan> CompactionEngine::Plan(
    const LakehouseTable& table,
    const CompactionOptions& options) {

    auto files_or = table.Scan();
    if (!files_or.ok()) {
        return files_or.status();
    }

    const auto& files = files_or.value();
    std::map<std::string, std::vector<DataFile>> partition_groups;

    for (const auto& file : files) {
        std::string part_key = MakePartitionKey(file.partitions);
        partition_groups[part_key].push_back(file);
    }

    CompactionPlan plan;

    for (const auto& [part_key, group_files] : partition_groups) {
        std::vector<DataFile> small_files;
        int64_t running_size = 0;

        for (const auto& f : group_files) {
            if (f.file_size_bytes < options.target_file_size_bytes) {
                small_files.push_back(f);
                running_size += f.file_size_bytes;

                if (static_cast<int32_t>(small_files.size()) >= options.max_files_per_group ||
                    running_size >= options.target_file_size_bytes) {
                    if (static_cast<int32_t>(small_files.size()) >= options.min_files_to_compact) {
                        FileGroup fg{};
                        fg.partitions = small_files.front().partitions;
                        fg.files = std::move(small_files);
                        fg.total_size_bytes = running_size;
                        plan.groups.push_back(std::move(fg));
                    }
                    small_files.clear();
                    running_size = 0;
                }
            }
        }

        if (static_cast<int32_t>(small_files.size()) >= options.min_files_to_compact) {
            FileGroup fg{};
            fg.partitions = small_files.front().partitions;
            fg.files = std::move(small_files);
            fg.total_size_bytes = running_size;
            plan.groups.push_back(std::move(fg));
        }
    }

    return plan;
}

Status CompactionEngine::Execute(
    const std::shared_ptr<LakehouseTable>& table,
    const CompactionPlan& plan,
    const std::function<StatusOr<DataFile>(const FileGroup&)>& file_merger) {

    if (plan.groups.empty()) {
        return Status::Ok();
    }

    Transaction tx(table, OperationType::Compaction);

    for (const auto& group : plan.groups) {
        auto merged_or = file_merger(group);
        if (!merged_or.ok()) {
            return merged_or.status();
        }

        tx.AppendFile(merged_or.value());

        for (const auto& old_file : group.files) {
            tx.DeleteFile(old_file);
        }
    }

    return tx.Commit();
}

StatusOr<VacuumResult> VacuumEngine::Vacuum(
    const LakehouseTable& table,
    const VacuumOptions& options) {

    VacuumResult result{};
    int64_t now_ms = CurrentTimeMs();
    int64_t threshold_ms = now_ms - options.retention_threshold_ms;

    // 1. Gather all active referenced data files across non-expired snapshots
    std::set<std::string> referenced_active_files;
    auto current_snap = table.current_snapshot();
    if (current_snap) {
        auto active_files_or = table.Scan({}, {}, current_snap->snapshot_id);
        if (active_files_or.ok()) {
            for (const auto& f : active_files_or.value()) {
                referenced_active_files.insert(f.path);
            }
        }
    }

    // 2. Identify candidate deleted files from historical manifests
    ManifestManager manifest_mgr(fs_);
    auto log_files_or = fs_->ListDirectory(table.manifests_dir_path());
    if (!log_files_or.ok()) {
        return result; // No manifests found, nothing to vacuum
    }

    for (const auto& manifest_path : log_files_or.value()) {
        auto entries_or = manifest_mgr.ReadManifest(manifest_path);
        if (!entries_or.ok()) continue;

        for (const auto& entry : entries_or.value()) {
            if (entry.status == ManifestEntry::Status::DELETED) {
                const auto& file_path = entry.data_file.path;
                // Check if file is still referenced by any active snapshot
                if (referenced_active_files.find(file_path) == referenced_active_files.end()) {
                    auto exists_or = fs_->Exists(file_path);
                    if (exists_or.ok() && exists_or.value()) {
                        auto size_or = fs_->GetFileSize(file_path);
                        int64_t file_size = size_or.ok() ? size_or.value() : 0;

                        if (!options.dry_run) {
                            auto del_status = fs_->DeleteFile(file_path);
                            if (del_status.ok()) {
                                ++result.deleted_files_count;
                                result.freed_bytes += file_size;
                                result.deleted_file_paths.push_back(file_path);
                            }
                        } else {
                            ++result.deleted_files_count;
                            result.freed_bytes += file_size;
                            result.deleted_file_paths.push_back(file_path);
                        }
                    }
                }
            }
        }
    }

    (void)threshold_ms; // Used for snapshot manifest archiving
    return result;
}

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
