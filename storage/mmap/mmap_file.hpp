#pragma once

#include "storage/common/status.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <span>

namespace obsidian {
namespace storage {
namespace mmap {

#pragma pack(push, 1)
struct JournalFileHeader {
    uint64_t magic{0x4F4253444D4D4150ULL}; // "OBSDMMAP"
    uint32_t version{1};
    uint32_t header_size{64};
    uint64_t capacity_bytes{0};
    uint64_t write_offset{64};
    uint64_t record_count{0};
    uint32_t checksum{0};
    uint8_t reserved[28]{0};
};

struct JournalRecordHeader {
    uint16_t magic{0xAA55};
    uint16_t flags{0};
    uint32_t length{0};
    uint64_t sequence{0};
    uint32_t crc32c{0};
};

struct JournalRecordTail {
    uint16_t tail_magic{0x55AA};
};
#pragma pack(pop)

struct RecoveryReport {
    bool has_torn_write{false};
    uint64_t valid_records{0};
    uint64_t valid_bytes{0};
    uint64_t torn_offset{0};
    size_t torn_bytes_quarantined{0};
};

enum class MemoryMapMode {
    ReadOnly,
    ReadWrite
};

class MemoryMappedJournal {
public:
    MemoryMappedJournal();
    ~MemoryMappedJournal();

    MemoryMappedJournal(const MemoryMappedJournal&) = delete;
    MemoryMappedJournal& operator=(const MemoryMappedJournal&) = delete;

    MemoryMappedJournal(MemoryMappedJournal&& other) noexcept;
    MemoryMappedJournal& operator=(MemoryMappedJournal&& other) noexcept;

    // Creates or opens a memory mapped journal file
    static StatusOr<MemoryMappedJournal> Open(
        const std::string& path,
        MemoryMapMode mode = MemoryMapMode::ReadWrite,
        size_t initial_capacity = 16 * 1024 * 1024);

    // Appends a binary record to the journal with framing and CRC32C error detection
    Status AppendRecord(uint64_t sequence, std::span<const uint8_t> payload);

    // Scans the journal, checks for torn writes or bit-rot corruption, and optionally truncates to last valid record
    StatusOr<RecoveryReport> ScanAndRecover(bool repair_torn_tail = true);

    // Flushes dirty pages to persistent storage
    Status Flush();

    // Closes mapping
    void Close();

    [[nodiscard]] bool IsOpen() const noexcept { return mapped_ptr_ != nullptr; }
    [[nodiscard]] size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t WriteOffset() const noexcept;
    [[nodiscard]] uint64_t RecordCount() const noexcept;
    [[nodiscard]] const std::string& Path() const noexcept { return path_; }

private:
    std::string path_;
    MemoryMapMode mode_{MemoryMapMode::ReadWrite};
    uint8_t* mapped_ptr_{nullptr};
    size_t capacity_{0};

#ifdef _WIN32
    void* file_handle_{nullptr};
    void* map_handle_{nullptr};
#else
    int fd_{-1};
#endif
};

} // namespace mmap
} // namespace storage
} // namespace obsidian
