#include "catalog.hpp"
#include <filesystem>

namespace obsidian {
namespace storage {
namespace lakehouse {

namespace fs_std = std::filesystem;

LakehouseCatalog::LakehouseCatalog(
    std::string warehouse_root,
    std::shared_ptr<IFileSystem> fs)
    : warehouse_root_(std::move(warehouse_root)), fs_(std::move(fs)) {}

std::string LakehouseCatalog::TableLocation(const std::string& ns, const std::string& table_name) const {
    return warehouse_root_ + "/" + ns + "/" + table_name;
}

Status LakehouseCatalog::CreateNamespace(const std::string& ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    return fs_->CreateDirectories(warehouse_root_ + "/" + ns);
}

StatusOr<std::vector<std::string>> LakehouseCatalog::ListNamespaces() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto list_or = fs_->ListDirectory(warehouse_root_);
    if (!list_or.ok()) {
        return list_or.status();
    }

    std::vector<std::string> namespaces;
    for (const auto& path_str : list_or.value()) {
        fs_std::path p(path_str);
        if (fs_std::is_directory(p)) {
            namespaces.push_back(p.filename().string());
        }
    }
    return namespaces;
}

StatusOr<std::shared_ptr<LakehouseTable>> LakehouseCatalog::CreateTable(
    const std::string& ns,
    const std::string& table_name,
    schema::Schema schema,
    PartitionSpec spec) {

    std::lock_guard<std::mutex> lock(mutex_);
    std::string loc = TableLocation(ns, table_name);
    std::string cache_key = ns + "." + table_name;

    auto exists_or = fs_->Exists(loc + "/_delta_log");
    if (exists_or.ok() && exists_or.value()) {
        return Status::AlreadyExists("Lakehouse table already exists: " + cache_key);
    }

    auto table_or = LakehouseTable::Create(loc, table_name, std::move(schema), std::move(spec), fs_);
    if (!table_or.ok()) {
        return table_or.status();
    }

    auto table = table_or.value();
    table_cache_[cache_key] = table;
    return table;
}

StatusOr<std::shared_ptr<LakehouseTable>> LakehouseCatalog::LoadTable(
    const std::string& ns,
    const std::string& table_name) {

    std::lock_guard<std::mutex> lock(mutex_);
    std::string cache_key = ns + "." + table_name;

    auto it = table_cache_.find(cache_key);
    if (it != table_cache_.end()) {
        return it->second;
    }

    std::string loc = TableLocation(ns, table_name);
    auto exists_or = fs_->Exists(loc + "/_delta_log");
    if (!exists_or.ok() || !exists_or.value()) {
        return Status::NotFound("Lakehouse table not found: " + cache_key);
    }

    auto table_or = LakehouseTable::Open(loc, fs_);
    if (!table_or.ok()) {
        return table_or.status();
    }

    auto table = table_or.value();
    table_cache_[cache_key] = table;
    return table;
}

Status LakehouseCatalog::DropTable(
    const std::string& ns,
    const std::string& table_name,
    bool purge_data) {

    std::lock_guard<std::mutex> lock(mutex_);
    std::string cache_key = ns + "." + table_name;
    table_cache_.erase(cache_key);

    std::string loc = TableLocation(ns, table_name);
    if (purge_data) {
        return fs_->DeleteRecursively(loc);
    }
    return Status::Ok();
}

StatusOr<std::vector<std::string>> LakehouseCatalog::ListTables(const std::string& ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string ns_path = warehouse_root_ + "/" + ns;

    auto list_or = fs_->ListDirectory(ns_path);
    if (!list_or.ok()) {
        return list_or.status();
    }

    std::vector<std::string> tables;
    for (const auto& path_str : list_or.value()) {
        fs_std::path p(path_str);
        if (fs_std::is_directory(p)) {
            std::string log_dir = (p / "_delta_log").string();
            auto log_exists_or = fs_->Exists(log_dir);
            if (log_exists_or.ok() && log_exists_or.value()) {
                tables.push_back(p.filename().string());
            }
        }
    }

    return tables;
}

StatusOr<bool> LakehouseCatalog::TableExists(const std::string& ns, const std::string& table_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string loc = TableLocation(ns, table_name);
    return fs_->Exists(loc + "/_delta_log");
}

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
