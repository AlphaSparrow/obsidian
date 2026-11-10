#include "integrity_guard.hpp"
#include <unordered_set>
#include <filesystem>
#include <algorithm>

namespace fs_std = std::filesystem;

namespace obsidian {
namespace storage {
namespace lakehouse {

LakehouseIntegrityGuard::LakehouseIntegrityGuard(std::shared_ptr<IFileSystem> fs)
    : fs_(std::move(fs)) {
    if (!fs_) {
        fs_ = LocalFileSystem::Default();
    }
}

void LakehouseIntegrityGuard::CheckBoundaryAnomalies(
    const DataFile& file,
    IntegrityReport& report) {

    if (file.file_size_bytes < 0) {
        report.is_healthy = false;
        report.boundary_anomalies_count++;
        report.detected_issues.push_back("Negative file size (" + std::to_string(file.file_size_bytes) + ") in: " + file.path);
    }

    if (file.record_count < 0) {
        report.is_healthy = false;
        report.boundary_anomalies_count++;
        report.detected_issues.push_back("Negative record count (" + std::to_string(file.record_count) + ") in: " + file.path);
    }

    // Inspect min/max boundary coherence
    for (const auto& [col_name, min_str] : file.min_values) {
        auto max_it = file.max_values.find(col_name);
        if (max_it != file.max_values.end()) {
            const std::string& max_str = max_it->second;
            try {
                // If numeric float or integer, check min <= max
                double min_num = std::stod(min_str);
                double max_num = std::stod(max_str);
                if (min_num > max_num) {
                    report.is_healthy = false;
                    report.boundary_anomalies_count++;
                    report.detected_issues.push_back(
                        "Inverted min/max boundary anomaly in file '" + file.path + "' for column '" +
                        col_name + "': min (" + min_str + ") > max (" + max_str + ")");
                }

                // Check timestamp anomaly
                if (col_name.find("timestamp") != std::string::npos || col_name.find("time") != std::string::npos) {
                    if (min_num < 0) {
                        report.is_healthy = false;
                        report.boundary_anomalies_count++;
                        report.detected_issues.push_back(
                            "Negative timestamp anomaly in file '" + file.path + "': " + min_str);
                    }
                }
            } catch (const std::exception&) {
                // String boundary lexical comparison
                if (min_str > max_str) {
                    report.is_healthy = false;
                    report.boundary_anomalies_count++;
                    report.detected_issues.push_back(
                        "Lexically inverted boundary anomaly in file '" + file.path + "' for column '" +
                        col_name + "': min (" + min_str + ") > max (" + max_str + ")");
                }
            }
        }
    }
}

void LakehouseIntegrityGuard::FindOrphanFiles(
    const std::string& dir_path,
    const std::unordered_set<std::string>& active_paths,
    IntegrityReport& report) {

    auto list_st = fs_->ListDirectory(dir_path);
    if (!list_st.ok()) return;

    for (const auto& entry : list_st.value()) {
        std::string clean_p = entry;
        std::replace(clean_p.begin(), clean_p.end(), '\\', '/');

        // Skip metadata log directory
        if (clean_p.find("/_delta_log") != std::string::npos) {
            continue;
        }

        // If directory, recurse into partition
        if (fs_std::is_directory(clean_p)) {
            FindOrphanFiles(clean_p, active_paths, report);
        } else if (clean_p.ends_with(".parquet") || clean_p.ends_with(".obsd")) {
            // Check if file is tracked in active manifest
            if (active_paths.find(clean_p) == active_paths.end()) {
                report.is_healthy = false;
                report.orphan_data_files++;
                report.orphan_file_paths.push_back(clean_p);

                auto sz_or = fs_->GetFileSize(clean_p);
                if (sz_or.ok()) {
                    report.orphan_leaked_bytes += sz_or.value();
                }
                report.detected_issues.push_back("Unreferenced orphan file detected on disk: " + clean_p);
            }
        }
    }
}

StatusOr<IntegrityReport> LakehouseIntegrityGuard::Audit(
    const LakehouseTable& table,
    int64_t snapshot_id) {

    IntegrityReport report;

    // 1. Snapshot Lineage Verification
    int64_t target_snap = snapshot_id >= 0 ? snapshot_id : table.current_snapshot_id();
    auto snap_opt = table.GetSnapshot(target_snap);
    if (!snap_opt.has_value()) {
        return Status(StatusCode::NotFound, "Target snapshot ID not found: " + std::to_string(target_snap));
    }

    // 2. Scan active files in snapshot
    auto scan_st = table.Scan({}, {}, target_snap);
    if (!scan_st.ok()) {
        return scan_st.status();
    }

    const auto& active_files = scan_st.value();
    report.active_data_files = static_cast<int32_t>(active_files.size());

    std::unordered_set<std::string> active_paths;

    for (const auto& df : active_files) {
        std::string clean_path = df.path;
        std::replace(clean_path.begin(), clean_path.end(), '\\', '/');
        active_paths.insert(clean_path);

        // Verify physical existence on disk
        auto exists_st = fs_->Exists(clean_path);
        if (!exists_st.ok() || !exists_st.value()) {
            report.is_healthy = false;
            report.missing_data_files++;
            report.detected_issues.push_back("Missing physical data file referenced in manifest: " + clean_path);
        }

        // Verify boundary statistics
        CheckBoundaryAnomalies(df, report);
    }

    // 3. Scan for untracked orphan files on disk
    FindOrphanFiles(table.location(), active_paths, report);

    return report;
}

Status LakehouseIntegrityGuard::CleanOrphanFiles(
    const std::vector<std::string>& orphan_paths) {
    for (const auto& p : orphan_paths) {
        auto del_st = fs_->DeleteFile(p);
        if (!del_st.ok()) {
            return del_st;
        }
    }
    return Status::OK();
}

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
