#pragma once

#include "storage/common/status.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <list>
#include <shared_mutex>
#include <span>

namespace obsidian {
namespace storage {
namespace cache {

struct CacheMetrics {
    uint64_t hits{0};
    uint64_t misses{0};
    uint64_t evictions{0};
    uint64_t poisoning_detections{0};
    size_t current_bytes{0};
    size_t max_bytes{0};
    size_t entry_count{0};
    size_t max_entries{0};
};

class BlockCache {
public:
    explicit BlockCache(size_t max_bytes = 64 * 1024 * 1024, size_t max_entries = 10000);
    ~BlockCache() = default;

    BlockCache(const BlockCache&) = delete;
    BlockCache& operator=(const BlockCache&) = delete;

    // Insert block into cache. Automatically calculates and stores CRC32C checksum.
    Status Put(const std::string& key, std::span<const uint8_t> data);

    // Retrieve block from cache. Performs integrity check via CRC32C to detect memory corruption/poisoning.
    // If corruption is detected, evicts the poisoned entry and returns Status::Corruption.
    StatusOr<std::vector<uint8_t>> Get(const std::string& key);

    // Removes an entry explicitly
    bool Erase(const std::string& key);

    // Clears all cache contents
    void Clear();

    // Returns a snapshot of cache metrics
    [[nodiscard]] CacheMetrics GetMetrics() const;

private:
    struct CacheEntry {
        std::string key;
        std::vector<uint8_t> data;
        uint32_t expected_crc32c{0};
        size_t size_bytes{0};
    };

    mutable std::shared_mutex mutex_;
    size_t max_bytes_{0};
    size_t max_entries_{0};
    size_t current_bytes_{0};

    // LRU list: most recently used at front
    std::list<CacheEntry> lru_list_;
    std::unordered_map<std::string, std::list<CacheEntry>::iterator> table_;

    // Telemetry
    mutable CacheMetrics metrics_;

    void EvictLruLocked();
};

} // namespace cache
} // namespace storage
} // namespace obsidian
