#include "lakehouse_table.hpp"
#include "transaction.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace obsidian {
namespace storage {
namespace lakehouse {

namespace {

std::string FormatSnapshotFilename(int64_t snapshot_id) {
    std::ostringstream ss;
    ss << std::setw(20) << std::setfill('0') << snapshot_id << ".json";
    return ss.str();
}

int64_t CurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

LakehouseTable::LakehouseTable(
    std::string location,
    TableMetadata metadata,
    schema::Schema schema,
    PartitionSpec partition_spec,
    std::shared_ptr<IFileSystem> fs)
    : location_(std::move(location)),
      metadata_(std::move(metadata)),
      schema_(std::move(schema)),
      partition_spec_(std::move(partition_spec)),
      fs_(std::move(fs)),
      manifest_mgr_(std::make_unique<ManifestManager>(fs_)) {}

std::string LakehouseTable::delta_log_path() const {
    return location_ + "/_delta_log";
}

std::string LakehouseTable::manifests_dir_path() const {
    return delta_log_path() + "/manifests";
}

int64_t LakehouseTable::current_snapshot_id() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metadata_.current_snapshot_id;
}

std::optional<Snapshot> LakehouseTable::current_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metadata_.snapshots.empty()) {
        return std::nullopt;
    }
    return metadata_.snapshots.back();
}

std::optional<Snapshot> LakehouseTable::GetSnapshot(int64_t snapshot_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& snap : metadata_.snapshots) {
        if (snap.snapshot_id == snapshot_id) {
            return snap;
        }
    }
    return std::nullopt;
}

std::optional<Snapshot> LakehouseTable::GetSnapshotAsOfTimestamp(int64_t timestamp_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Snapshot* best_match = nullptr;

    for (const auto& snap : metadata_.snapshots) {
        if (snap.timestamp_ms <= timestamp_ms) {
            if (!best_match || snap.timestamp_ms > best_match->timestamp_ms) {
                best_match = &snap;
            }
        }
    }

    if (best_match) {
        return *best_match;
    }
    return std::nullopt;
}

StatusOr<std::shared_ptr<LakehouseTable>> LakehouseTable::Create(
    std::string location,
    std::string table_name,
    schema::Schema schema,
    PartitionSpec partition_spec,
    std::shared_ptr<IFileSystem> fs) {

    auto log_path = location + "/_delta_log";
    auto manifests_path = log_path + "/manifests";

    auto status = fs->CreateDirectories(manifests_path);
    if (!status.ok()) {
        return status;
    }

    TableMetadata meta{};
    meta.table_name = std::move(table_name);
    meta.location = location;
    meta.format_version = 1;
    meta.current_snapshot_id = 0;

    auto table = std::make_shared<LakehouseTable>(
        std::move(location),
        std::move(meta),
        std::move(schema),
        std::move(partition_spec),
        std::move(fs));

    status = table->SaveMetadataUnlocked();
    if (!status.ok()) {
        return status;
    }

    return table;
}

StatusOr<std::shared_ptr<LakehouseTable>> LakehouseTable::Open(
    std::string location,
    std::shared_ptr<IFileSystem> fs) {

    auto meta_or = LoadMetadata(location, fs);
    if (!meta_or.ok()) {
        return meta_or.status();
    }

    auto meta = meta_or.value();
    // Default schema/spec for loaded table (extended in catalog)
    schema::Schema schema{};
    PartitionSpec spec{};

    return std::make_shared<LakehouseTable>(
        std::move(location),
        std::move(meta),
        std::move(schema),
        std::move(spec),
        std::move(fs));
}

std::unique_ptr<Transaction> LakehouseTable::NewTransaction(OperationType op) {
    return std::make_unique<Transaction>(shared_from_this(), op);
}

Status LakehouseTable::CommitTransaction(
    int64_t base_snapshot_id,
    OperationType op,
    const std::vector<DataFile>& added_files,
    const std::vector<DataFile>& deleted_files) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Optimistic Concurrency Control (OCC) validation
    if (metadata_.current_snapshot_id != base_snapshot_id) {
        return Status::ConcurrentModification(
            "Transaction conflict: table has moved from snapshot " + 
            std::to_string(base_snapshot_id) + " to snapshot " + 
            std::to_string(metadata_.current_snapshot_id));
    }

    int64_t next_snapshot_id = metadata_.current_snapshot_id + 1;
    std::vector<ManifestEntry> entries;

    // Preserve existing active files from previous snapshot
    if (!metadata_.snapshots.empty()) {
        const auto& prev_snap = metadata_.snapshots.back();
        auto prev_entries_or = manifest_mgr_->ReadManifest(prev_snap.manifest_list_path);
        if (prev_entries_or.ok()) {
            for (auto& entry : prev_entries_or.value()) {
                if (entry.status == ManifestEntry::Status::DELETED) {
                    continue; // Skip already deleted files
                }

                // Check if this file is explicitly deleted in this transaction
                bool to_delete = false;
                for (const auto& df : deleted_files) {
                    if (df.path == entry.data_file.path) {
                        to_delete = true;
                        break;
                    }
                }

                if (to_delete) {
                    entry.status = ManifestEntry::Status::DELETED;
                    entry.snapshot_id = next_snapshot_id;
                    entries.push_back(entry);
                } else {
                    entry.status = ManifestEntry::Status::EXISTING;
                    entries.push_back(entry);
                }
            }
        }
    }

    // Append newly added files
    for (const auto& df : added_files) {
        ManifestEntry entry{};
        entry.status = ManifestEntry::Status::ADDED;
        entry.snapshot_id = next_snapshot_id;
        entry.data_file = df;
        entries.push_back(entry);
    }

    std::string manifest_filename = "manifest-snap-" + std::to_string(next_snapshot_id) + ".bin";
    std::string manifest_path = manifests_dir_path() + "/" + manifest_filename;

    auto mf_or = manifest_mgr_->WriteManifest(manifest_path, next_snapshot_id, partition_spec_.spec_id(), entries);
    if (!mf_or.ok()) {
        return mf_or.status();
    }

    Snapshot new_snapshot{};
    new_snapshot.snapshot_id = next_snapshot_id;
    new_snapshot.parent_snapshot_id = metadata_.current_snapshot_id;
    new_snapshot.timestamp_ms = CurrentTimeMs();
    new_snapshot.manifest_list_path = manifest_path;
    new_snapshot.operation = OperationTypeToString(op);

    metadata_.current_snapshot_id = next_snapshot_id;
    metadata_.snapshots.push_back(new_snapshot);

    return SaveMetadataUnlocked();
}

StatusOr<std::vector<DataFile>> LakehouseTable::Scan(
    const std::vector<ColumnPredicate>& predicates,
    const std::vector<PartitionValue>& partition_filters,
    int64_t snapshot_id) const {

    std::lock_guard<std::mutex> lock(mutex_);

    if (metadata_.snapshots.empty()) {
        return std::vector<DataFile>{};
    }

    const Snapshot* target_snap = nullptr;
    if (snapshot_id < 0) {
        target_snap = &metadata_.snapshots.back();
    } else {
        for (const auto& s : metadata_.snapshots) {
            if (s.snapshot_id == snapshot_id) {
                target_snap = &s;
                break;
            }
        }
    }

    if (!target_snap) {
        return Status::NotFound("Snapshot ID not found: " + std::to_string(snapshot_id));
    }

    auto entries_or = manifest_mgr_->ReadManifest(target_snap->manifest_list_path);
    if (!entries_or.ok()) {
        return entries_or.status();
    }

    return manifest_mgr_->FilterFiles(entries_or.value(), predicates, partition_filters);
}

Status LakehouseTable::SaveMetadataUnlocked() {
    std::ostringstream ss;
    ss << "# OBSIDIAN_TABLE_METADATA_V1\n";
    ss << "table_name=" << metadata_.table_name << "\n";
    ss << "location=" << metadata_.location << "\n";
    ss << "format_version=" << metadata_.format_version << "\n";
    ss << "current_snapshot_id=" << metadata_.current_snapshot_id << "\n";
    
    for (const auto& snap : metadata_.snapshots) {
        ss << "SNAPSHOT|" << snap.snapshot_id
           << "|" << snap.parent_snapshot_id
           << "|" << snap.timestamp_ms
           << "|" << snap.manifest_list_path
           << "|" << snap.operation
           << "\n";
    }

    std::string meta_filename = FormatSnapshotFilename(metadata_.current_snapshot_id);
    std::string meta_path = delta_log_path() + "/" + meta_filename;

    return fs_->WriteFileAtomic(meta_path, ss.str());
}

StatusOr<TableMetadata> LakehouseTable::LoadMetadata(
    const std::string& location,
    const std::shared_ptr<IFileSystem>& fs) {

    auto log_path = location + "/_delta_log";
    auto files_or = fs->ListDirectory(log_path);
    if (!files_or.ok()) {
        return files_or.status();
    }

    std::vector<std::string> meta_files;
    for (const auto& file : files_or.value()) {
        if (file.find(".json") != std::string::npos && file.find(".tmp") == std::string::npos) {
            meta_files.push_back(file);
        }
    }

    if (meta_files.empty()) {
        return Status::NotFound("No valid table metadata found in: " + log_path);
    }

    std::sort(meta_files.begin(), meta_files.end());
    const std::string& latest_meta_path = meta_files.back();

    auto text_or = fs->ReadAllText(latest_meta_path);
    if (!text_or.ok()) {
        return text_or.status();
    }

    TableMetadata meta{};
    std::stringstream ss(text_or.value());
    std::string line;

    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("table_name=", 0) == 0) {
            meta.table_name = line.substr(11);
        } else if (line.rfind("location=", 0) == 0) {
            meta.location = line.substr(9);
        } else if (line.rfind("format_version=", 0) == 0) {
            meta.format_version = std::stoi(line.substr(15));
        } else if (line.rfind("current_snapshot_id=", 0) == 0) {
            meta.current_snapshot_id = std::stoll(line.substr(20));
        } else if (line.rfind("SNAPSHOT|", 0) == 0) {
            std::vector<std::string> tokens;
            std::stringstream line_ss(line);
            std::string token;
            while (std::getline(line_ss, token, '|')) {
                tokens.push_back(token);
            }
            if (tokens.size() >= 6) {
                Snapshot snap{};
                snap.snapshot_id = std::stoll(tokens[1]);
                snap.parent_snapshot_id = std::stoll(tokens[2]);
                snap.timestamp_ms = std::stoll(tokens[3]);
                snap.manifest_list_path = tokens[4];
                snap.operation = tokens[5];
                meta.snapshots.push_back(snap);
            }
        }
    }

    return meta;
}

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
