#pragma once

#include "status.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace obsidian {
namespace storage {

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    virtual StatusOr<bool> Exists(const std::string& path) = 0;
    virtual StatusOr<int64_t> GetFileSize(const std::string& path) = 0;
    virtual Status CreateDirectories(const std::string& path) = 0;
    virtual StatusOr<std::string> ReadAllText(const std::string& path) = 0;
    virtual Status WriteFile(const std::string& path, const std::string& content) = 0;
    virtual Status WriteFileAtomic(const std::string& path, const std::string& content) = 0;
    virtual Status DeleteFile(const std::string& path) = 0;
    virtual Status DeleteRecursively(const std::string& path) = 0;
    virtual StatusOr<std::vector<std::string>> ListDirectory(const std::string& path) = 0;
    virtual Status CopyFile(const std::string& src, const std::string& dst) = 0;
};

class LocalFileSystem : public IFileSystem {
public:
    LocalFileSystem() = default;
    ~LocalFileSystem() override = default;

    StatusOr<bool> Exists(const std::string& path) override;
    StatusOr<int64_t> GetFileSize(const std::string& path) override;
    Status CreateDirectories(const std::string& path) override;
    StatusOr<std::string> ReadAllText(const std::string& path) override;
    Status WriteFile(const std::string& path, const std::string& content) override;
    Status WriteFileAtomic(const std::string& path, const std::string& content) override;
    Status DeleteFile(const std::string& path) override;
    Status DeleteRecursively(const std::string& path) override;
    StatusOr<std::vector<std::string>> ListDirectory(const std::string& path) override;
    Status CopyFile(const std::string& src, const std::string& dst) override;

    static std::shared_ptr<LocalFileSystem> Default();
};

} // namespace storage
} // namespace obsidian
