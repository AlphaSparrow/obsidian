#include "manifest_manager.hpp"
#include <sstream>

namespace obsidian {
namespace storage {
namespace lakehouse {

namespace {

std::string EncodeKVs(const std::unordered_map<std::string, std::string>& kvs) {
    std::string out;
    bool first = true;
    for (const auto& [k, v] : kvs) {
        if (!first) out += ",";
        out += k + "=" + v;
        first = false;
    }
    return out;
}

std::unordered_map<std::string, std::string> DecodeKVs(const std::string& str) {
    std::unordered_map<std::string, std::string> kvs;
    if (str.empty()) return kvs;

    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        auto eq_pos = item.find('=');
        if (eq_pos != std::string::npos) {
            std::string k = item.substr(0, eq_pos);
            std::string v = item.substr(eq_pos + 1);
            kvs[k] = v;
        }
    }
    return kvs;
}

std::string EncodePartitions(const std::vector<PartitionValue>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += ",";
        out += parts[i].name + "=" + parts[i].value;
    }
    return out;
}

std::vector<PartitionValue> DecodePartitions(const std::string& str) {
    std::vector<PartitionValue> parts;
    if (str.empty()) return parts;

    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        auto eq_pos = item.find('=');
        if (eq_pos != std::string::npos) {
            parts.push_back({item.substr(0, eq_pos), item.substr(eq_pos + 1)});
        }
    }
    return parts;
}

} // namespace

StatusOr<ManifestFile> ManifestManager::WriteManifest(
    const std::string& manifest_path,
    int64_t snapshot_id,
    int32_t partition_spec_id,
    const std::vector<ManifestEntry>& entries) {

    std::ostringstream ss;
    ss << "# OBSIDIAN_MANIFEST_V1\n";
    ss << "HEADER|snapshot_id=" << snapshot_id 
       << "|partition_spec_id=" << partition_spec_id << "\n";

    int32_t added_count = 0;
    int32_t existing_count = 0;
    int32_t deleted_count = 0;

    for (const auto& entry : entries) {
        if (entry.status == ManifestEntry::Status::ADDED) ++added_count;
        else if (entry.status == ManifestEntry::Status::EXISTING) ++existing_count;
        else if (entry.status == ManifestEntry::Status::DELETED) ++deleted_count;

        ss << "ENTRY|" << static_cast<int>(entry.status)
           << "|" << entry.snapshot_id
           << "|" << entry.data_file.path
           << "|" << entry.data_file.format
           << "|" << entry.data_file.file_size_bytes
           << "|" << entry.data_file.record_count
           << "|" << EncodePartitions(entry.data_file.partitions)
           << "|" << EncodeKVs(entry.data_file.min_values)
           << "|" << EncodeKVs(entry.data_file.max_values)
           << "\n";
    }

    std::string content = ss.str();
    Status status = fs_->WriteFileAtomic(manifest_path, content);
    if (!status.ok()) {
        return status;
    }

    ManifestFile mf{};
    mf.path = manifest_path;
    mf.length = static_cast<int64_t>(content.size());
    mf.partition_spec_id = partition_spec_id;
    mf.added_snapshot_id = snapshot_id;
    mf.added_data_files_count = added_count;
    mf.existing_data_files_count = existing_count;
    mf.deleted_data_files_count = deleted_count;

    return mf;
}

StatusOr<std::vector<ManifestEntry>> ManifestManager::ReadManifest(const std::string& manifest_path) {
    auto text_or = fs_->ReadAllText(manifest_path);
    if (!text_or.ok()) {
        return text_or.status();
    }

    std::vector<ManifestEntry> entries;
    std::stringstream ss(text_or.value());
    std::string line;

    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("ENTRY|", 0) == 0) {
            std::vector<std::string> tokens;
            std::stringstream line_ss(line);
            std::string token;
            while (std::getline(line_ss, token, '|')) {
                tokens.push_back(token);
            }

            if (tokens.size() >= 10) {
                ManifestEntry entry{};
                entry.status = static_cast<ManifestEntry::Status>(std::stoi(tokens[1]));
                entry.snapshot_id = std::stoll(tokens[2]);
                entry.data_file.path = tokens[3];
                entry.data_file.format = tokens[4];
                entry.data_file.file_size_bytes = std::stoll(tokens[5]);
                entry.data_file.record_count = std::stoll(tokens[6]);
                entry.data_file.partitions = DecodePartitions(tokens[7]);
                entry.data_file.min_values = DecodeKVs(tokens[8]);
                entry.data_file.max_values = DecodeKVs(tokens[9]);
                entries.push_back(entry);
            }
        }
    }

    return entries;
}

std::vector<DataFile> ManifestManager::FilterFiles(
    const std::vector<ManifestEntry>& entries,
    const std::vector<ColumnPredicate>& column_predicates,
    const std::vector<PartitionValue>& partition_filters) const {

    std::vector<DataFile> matched;

    for (const auto& entry : entries) {
        // Exclude deleted files
        if (entry.status == ManifestEntry::Status::DELETED) {
            continue;
        }

        const auto& file = entry.data_file;

        // Partition matching
        bool partition_match = true;
        for (const auto& filter : partition_filters) {
            bool found_matching_part = false;
            for (const auto& part : file.partitions) {
                if (part.name == filter.name) {
                    if (part.value == filter.value) {
                        found_matching_part = true;
                    }
                    break;
                }
            }
            if (!found_matching_part) {
                partition_match = false;
                break;
            }
        }

        if (!partition_match) {
            continue;
        }

        // Column min/max statistics data skipping
        bool skipped = false;
        for (const auto& pred : column_predicates) {
            if (pred.CanSkipFile(file)) {
                skipped = true;
                break;
            }
        }

        if (!skipped) {
            matched.push_back(file);
        }
    }

    return matched;
}

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
