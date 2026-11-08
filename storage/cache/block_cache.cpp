#include "storage/cache/block_cache.hpp"
#include "storage/common/crc32c.hpp"
#include <mutex>

namespace obsidian {
namespace storage {
namespace cache {

BlockCache::BlockCache(size_t max_bytes, size_t max_entries)
    : max_bytes_(max_bytes), max_entries_(max_entries) {
    metrics_.max_bytes = max_bytes_;
    metrics_.max_entries = max_entries_;
}

Status BlockCache::Put(const std::string& key, std::span<const uint8_t> data) {
    if (data.empty()) {
        return Status::InvalidArgument("Cannot cache empty data block");
    }

    if (data.size() > max_bytes_) {
        return Status::InvalidArgument("Block size (" + std::to_string(data.size()) + 
                                      " bytes) exceeds total cache capacity (" + 
                                      std::to_string(max_bytes_) + " bytes)");
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);

    // If key already exists, erase it first
    auto it = table_.find(key);
    if (it != table_.end()) {
        current_bytes_ -= it->second->size_bytes;
        lru_list_.erase(it->second);
        table_.erase(it);
    }

    // Evict if necessary to stay within budget
    while ((current_bytes_ + data.size() > max_bytes_ || table_.size() >= max_entries_) && !lru_list_.empty()) {
        EvictLruLocked();
    }

    uint32_t checksum = common::CRC32C::Calculate(data.data(), data.size());

    CacheEntry entry;
    entry.key = key;
    entry.data.assign(data.begin(), data.end());
    entry.expected_crc32c = checksum;
    entry.size_bytes = data.size();

    lru_list_.push_front(std::move(entry));
    table_[key] = lru_list_.begin();
    current_bytes_ += data.size();

    metrics_.current_bytes = current_bytes_;
    metrics_.entry_count = table_.size();

    return Status::Ok();
}

StatusOr<std::vector<uint8_t>> BlockCache::Get(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = table_.find(key);
    if (it == table_.end()) {
        metrics_.misses++;
        return Status::NotFound("Cache miss for block: " + key);
    }

    // Move to front of LRU
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second);

    const auto& entry = *it->second;
    // Real-time bit-rot / cache poisoning detection
    uint32_t actual_crc = common::CRC32C::Calculate(entry.data.data(), entry.data.size());
    if (actual_crc != entry.expected_crc32c) {
        metrics_.poisoning_detections++;
        // Poisoned entry detected! Evict immediately to prevent query contagion
        current_bytes_ -= entry.size_bytes;
        lru_list_.erase(it->second);
        table_.erase(it);

        metrics_.current_bytes = current_bytes_;
        metrics_.entry_count = table_.size();

        return Status::Corruption("Memory bit-rot / cache poisoning detected for key '" + key + 
                                  "': expected CRC32C 0x" + std::to_string(entry.expected_crc32c) + 
                                  " but calculated 0x" + std::to_string(actual_crc));
    }

    metrics_.hits++;
    return entry.data;
}

bool BlockCache::Erase(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = table_.find(key);
    if (it != table_.end()) {
        current_bytes_ -= it->second->size_bytes;
        lru_list_.erase(it->second);
        table_.erase(it);
        metrics_.current_bytes = current_bytes_;
        metrics_.entry_count = table_.size();
        return true;
    }
    return false;
}

void BlockCache::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    lru_list_.clear();
    table_.clear();
    current_bytes_ = 0;
    metrics_.current_bytes = 0;
    metrics_.entry_count = 0;
}

CacheMetrics BlockCache::GetMetrics() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    CacheMetrics snapshot = metrics_;
    snapshot.current_bytes = current_bytes_;
    snapshot.entry_count = table_.size();
    return snapshot;
}

void BlockCache::EvictLruLocked() {
    if (lru_list_.empty()) return;

    auto back_it = std::prev(lru_list_.end());
    current_bytes_ -= back_it->size_bytes;
    table_.erase(back_it->key);
    lru_list_.pop_back();

    metrics_.evictions++;
    metrics_.current_bytes = current_bytes_;
    metrics_.entry_count = table_.size();
}

} // namespace cache
} // namespace storage
} // namespace obsidian
