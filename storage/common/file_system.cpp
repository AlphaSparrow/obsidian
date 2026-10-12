#include "file_system.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>

namespace obsidian {
namespace storage {

namespace fs = std::filesystem;

StatusOr<bool> LocalFileSystem::Exists(const std::string& path) {
    try {
        return fs::exists(fs::path(path));
    } catch (const std::exception& e) {
        return Status::IOError("Exists check failed for '" + path + "': " + e.what());
    }
}

StatusOr<int64_t> LocalFileSystem::GetFileSize(const std::string& path) {
    try {
        auto p = fs::path(path);
        if (!fs::exists(p)) {
            return Status::NotFound("File not found: " + path);
        }
        return static_cast<int64_t>(fs::file_size(p));
    } catch (const std::exception& e) {
        return Status::IOError("GetFileSize failed for '" + path + "': " + e.what());
    }
}

Status LocalFileSystem::CreateDirectories(const std::string& path) {
    try {
        auto p = fs::path(path);
        if (!p.empty() && !fs::exists(p)) {
            fs::create_directories(p);
        }
        return Status::Ok();
    } catch (const std::exception& e) {
        return Status::IOError("CreateDirectories failed for '" + path + "': " + e.what());
    }
}

StatusOr<std::string> LocalFileSystem::ReadAllText(const std::string& path) {
    try {
        auto p = fs::path(path);
        if (!fs::exists(p)) {
            return Status::NotFound("File not found: " + path);
        }
        std::ifstream file(p, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            return Status::IOError("Failed to open file for reading: " + path);
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    } catch (const std::exception& e) {
        return Status::IOError("ReadAllText failed for '" + path + "': " + e.what());
    }
}

Status LocalFileSystem::WriteFile(const std::string& path, const std::string& content) {
    try {
        auto p = fs::path(path);
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path());
        }
        std::ofstream file(p, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return Status::IOError("Failed to open file for writing: " + path);
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();
        if (!file.good()) {
            return Status::IOError("Stream error while writing to: " + path);
        }
        return Status::Ok();
    } catch (const std::exception& e) {
        return Status::IOError("WriteFile failed for '" + path + "': " + e.what());
    }
}

Status LocalFileSystem::WriteFileAtomic(const std::string& path, const std::string& content) {
    try {
        auto target_path = fs::path(path);
        if (target_path.has_parent_path()) {
            fs::create_directories(target_path.parent_path());
        }

        // Generate a unique temporary path in the same directory to allow atomic rename
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        static thread_local std::mt19937_64 rng(std::random_device{}());
        uint64_t nonce = rng();
        
        std::string temp_filename = target_path.filename().string() + ".tmp." + 
                                    std::to_string(now) + "." + std::to_string(nonce);
        auto temp_path = target_path.parent_path() / fs::path(temp_filename);

        {
            std::ofstream file(temp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                return Status::IOError("Failed to create temporary file: " + temp_path.string());
            }
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            file.flush();
            if (!file.good()) {
                file.close();
                fs::remove(temp_path);
                return Status::IOError("Stream error during atomic write to: " + temp_path.string());
            }
        }

        // Atomic replace
        std::error_code ec;
        if (fs::exists(target_path, ec)) {
            fs::remove(target_path, ec);
        }
        fs::rename(temp_path, target_path, ec);
        if (ec) {
            fs::remove(temp_path, ec);
            return Status::IOError("Atomic rename failed from '" + temp_path.string() + 
                                   "' to '" + path + "': " + ec.message());
        }

        return Status::Ok();
    } catch (const std::exception& e) {
        return Status::IOError("WriteFileAtomic failed for '" + path + "': " + e.what());
    }
}

Status LocalFileSystem::DeleteFile(const std::string& path) {
    try {
        auto p = fs::path(path);
        if (!fs::exists(p)) {
            return Status::Ok();
        }
        if (!fs::remove(p)) {
            return Status::IOError("Failed to remove file: " + path);
        }
        return Status::Ok();
    } catch (const std::exception& e) {
        return Status::IOError("DeleteFile failed for '" + path + "': " + e.what());
    }
}

Status LocalFileSystem::DeleteRecursively(const std::string& path) {
    try {
        auto p = fs::path(path);
        if (!fs::exists(p)) {
            return Status::Ok();
        }
        fs::remove_all(p);
        return Status::Ok();
    } catch (const std::exception& e) {
        return Status::IOError("DeleteRecursively failed for '" + path + "': " + e.what());
    }
}

StatusOr<std::vector<std::string>> LocalFileSystem::ListDirectory(const std::string& path) {
    try {
        auto p = fs::path(path);
        if (!fs::exists(p)) {
            return Status::NotFound("Directory not found: " + path);
        }
        if (!fs::is_directory(p)) {
            return Status::InvalidArgument("Path is not a directory: " + path);
        }

        std::vector<std::string> entries;
        for (const auto& entry : fs::directory_iterator(p)) {
            entries.push_back(entry.path().string());
        }
        return entries;
    } catch (const std::exception& e) {
        return Status::IOError("ListDirectory failed for '" + path + "': " + e.what());
    }
}

Status LocalFileSystem::CopyFile(const std::string& src, const std::string& dst) {
    try {
        auto src_p = fs::path(src);
        auto dst_p = fs::path(dst);
        if (!fs::exists(src_p)) {
            return Status::NotFound("Source file not found: " + src);
        }
        if (dst_p.has_parent_path()) {
            fs::create_directories(dst_p.parent_path());
        }
        fs::copy_file(src_p, dst_p, fs::copy_options::overwrite_existing);
        return Status::Ok();
    } catch (const std::exception& e) {
        return Status::IOError("CopyFile failed from '" + src + "' to '" + dst + "': " + e.what());
    }
}

std::shared_ptr<LocalFileSystem> LocalFileSystem::Default() {
    static auto instance = std::make_shared<LocalFileSystem>();
    return instance;
}

} // namespace storage
} // namespace obsidian
