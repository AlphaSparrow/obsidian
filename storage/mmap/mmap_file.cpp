#include "storage/mmap/mmap_file.hpp"
#include "storage/common/crc32c.hpp"

#include <cstring>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace obsidian {
namespace storage {
namespace mmap {

namespace fs = std::filesystem;

MemoryMappedJournal::MemoryMappedJournal() = default;

MemoryMappedJournal::~MemoryMappedJournal() {
    Close();
}

MemoryMappedJournal::MemoryMappedJournal(MemoryMappedJournal&& other) noexcept
    : path_(std::move(other.path_)),
      mode_(other.mode_),
      mapped_ptr_(other.mapped_ptr_),
      capacity_(other.capacity_) {
#ifdef _WIN32
    file_handle_ = other.file_handle_;
    map_handle_ = other.map_handle_;
    other.file_handle_ = nullptr;
    other.map_handle_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
    other.mapped_ptr_ = nullptr;
    other.capacity_ = 0;
}

MemoryMappedJournal& MemoryMappedJournal::operator=(MemoryMappedJournal&& other) noexcept {
    if (this != &other) {
        Close();
        path_ = std::move(other.path_);
        mode_ = other.mode_;
        mapped_ptr_ = other.mapped_ptr_;
        capacity_ = other.capacity_;
#ifdef _WIN32
        file_handle_ = other.file_handle_;
        map_handle_ = other.map_handle_;
        other.file_handle_ = nullptr;
        other.map_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.mapped_ptr_ = nullptr;
        other.capacity_ = 0;
    }
    return *this;
}

StatusOr<MemoryMappedJournal> MemoryMappedJournal::Open(
    const std::string& path,
    MemoryMapMode mode,
    size_t initial_capacity) {
    MemoryMappedJournal journal;
    journal.path_ = path;
    journal.mode_ = mode;
    journal.capacity_ = initial_capacity;

    fs::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    bool file_exists = fs::exists(p);

#ifdef _WIN32
    DWORD access = (mode == MemoryMapMode::ReadOnly) ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD creation = file_exists ? OPEN_EXISTING : OPEN_ALWAYS;

    HANDLE file_h = CreateFileA(path.c_str(), access, share, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_h == INVALID_HANDLE_VALUE) {
        return Status::IOError("Failed to open journal file: " + path + " (Win32 err=" + std::to_string(GetLastError()) + ")");
    }
    journal.file_handle_ = file_h;

    LARGE_INTEGER current_size;
    GetFileSizeEx(file_h, &current_size);
    if (!file_exists || current_size.QuadPart < static_cast<LONGLONG>(initial_capacity)) {
        if (mode == MemoryMapMode::ReadOnly) {
            CloseHandle(file_h);
            journal.file_handle_ = nullptr;
            return Status::IOError("Cannot enlarge read-only journal file: " + path);
        }
        LARGE_INTEGER new_size;
        new_size.QuadPart = static_cast<LONGLONG>(initial_capacity);
        if (!SetFilePointerEx(file_h, new_size, nullptr, FILE_BEGIN) || !SetEndOfFile(file_h)) {
            CloseHandle(file_h);
            journal.file_handle_ = nullptr;
            return Status::IOError("Failed to set journal file capacity on Windows");
        }
        journal.capacity_ = initial_capacity;
    } else {
        journal.capacity_ = static_cast<size_t>(current_size.QuadPart);
    }

    DWORD page_protect = (mode == MemoryMapMode::ReadOnly) ? PAGE_READONLY : PAGE_READWRITE;
    HANDLE map_h = CreateFileMappingA(file_h, nullptr, page_protect, 0, 0, nullptr);
    if (map_h == nullptr) {
        DWORD err = GetLastError();
        CloseHandle(file_h);
        journal.file_handle_ = nullptr;
        return Status::IOError("CreateFileMapping failed on Windows: err=" + std::to_string(err));
    }
    journal.map_handle_ = map_h;

    DWORD map_access = (mode == MemoryMapMode::ReadOnly) ? FILE_MAP_READ : (FILE_MAP_READ | FILE_MAP_WRITE);
    void* view = MapViewOfFile(map_h, map_access, 0, 0, 0);
    if (view == nullptr) {
        DWORD err = GetLastError();
        CloseHandle(map_h);
        CloseHandle(file_h);
        journal.map_handle_ = nullptr;
        journal.file_handle_ = nullptr;
        return Status::IOError("MapViewOfFile failed on Windows: err=" + std::to_string(err));
    }
    journal.mapped_ptr_ = static_cast<uint8_t*>(view);
#else
    int flags = (mode == MemoryMapMode::ReadOnly) ? O_RDONLY : (O_RDWR | O_CREAT);
    int fd = ::open(path.c_str(), flags, 0644);
    if (fd < 0) {
        return Status::IOError("Failed to open journal file on POSIX: " + path);
    }
    journal.fd_ = fd;

    struct stat st;
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        journal.fd_ = -1;
        return Status::IOError("fstat failed on journal file: " + path);
    }

    if (!file_exists || st.st_size < static_cast<off_t>(initial_capacity)) {
        if (mode == MemoryMapMode::ReadOnly) {
            ::close(fd);
            journal.fd_ = -1;
            return Status::IOError("Cannot enlarge read-only journal file: " + path);
        }
        if (::ftruncate(fd, static_cast<off_t>(initial_capacity)) != 0) {
            ::close(fd);
            journal.fd_ = -1;
            return Status::IOError("ftruncate failed on journal file: " + path);
        }
        journal.capacity_ = initial_capacity;
    } else {
        journal.capacity_ = static_cast<size_t>(st.st_size);
    }

    int prot = (mode == MemoryMapMode::ReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* view = ::mmap(nullptr, journal.capacity_, prot, MAP_SHARED, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        journal.fd_ = -1;
        return Status::IOError("mmap failed on POSIX: " + path);
    }
    journal.mapped_ptr_ = static_cast<uint8_t*>(view);
#endif

    // Initialize header if newly created
    if (!file_exists) {
        JournalFileHeader hdr;
        hdr.capacity_bytes = journal.capacity_;
        hdr.write_offset = sizeof(JournalFileHeader);
        hdr.record_count = 0;
        hdr.checksum = common::CRC32C::Calculate(&hdr, sizeof(JournalFileHeader) - sizeof(uint32_t) - sizeof(hdr.reserved));
        std::memcpy(journal.mapped_ptr_, &hdr, sizeof(JournalFileHeader));
        (void)journal.Flush();
    }

    return journal;
}

Status MemoryMappedJournal::AppendRecord(uint64_t sequence, std::span<const uint8_t> payload) {
    if (!IsOpen()) {
        return Status::IOError("Journal is not open for writing");
    }
    if (mode_ == MemoryMapMode::ReadOnly) {
        return Status::InvalidArgument("Cannot append record to read-only journal");
    }

    auto* header = reinterpret_cast<JournalFileHeader*>(mapped_ptr_);
    if (header->magic != 0x4F4253444D4D4150ULL) {
        return Status::Corruption("Journal header magic mismatch: file is corrupted");
    }

    size_t record_len = sizeof(JournalRecordHeader) + payload.size() + sizeof(JournalRecordTail);
    size_t current_offset = static_cast<size_t>(header->write_offset);

    if (current_offset + record_len > capacity_) {
        return Status::InvalidArgument("Journal capacity exceeded: current=" + std::to_string(current_offset) + 
                                      " req=" + std::to_string(record_len) + " cap=" + std::to_string(capacity_));
    }

    JournalRecordHeader rec;
    rec.magic = 0xAA55;
    rec.flags = 0;
    rec.length = static_cast<uint32_t>(payload.size());
    rec.sequence = sequence;
    rec.crc32c = common::CRC32C::Calculate(payload.data(), payload.size());

    JournalRecordTail tail;
    tail.tail_magic = 0x55AA;

    uint8_t* dest = mapped_ptr_ + current_offset;
    std::memcpy(dest, &rec, sizeof(JournalRecordHeader));
    if (!payload.empty()) {
        std::memcpy(dest + sizeof(JournalRecordHeader), payload.data(), payload.size());
    }
    std::memcpy(dest + sizeof(JournalRecordHeader) + payload.size(), &tail, sizeof(JournalRecordTail));

    // Update file header atomically
    header->write_offset = current_offset + record_len;
    header->record_count += 1;
    header->checksum = common::CRC32C::Calculate(header, sizeof(JournalFileHeader) - sizeof(uint32_t) - sizeof(header->reserved));

    return Status::Ok();
}

StatusOr<RecoveryReport> MemoryMappedJournal::ScanAndRecover(bool repair_torn_tail) {
    if (!IsOpen()) {
        return Status::IOError("Journal is not open for recovery scan");
    }

    RecoveryReport report;
    auto* header = reinterpret_cast<JournalFileHeader*>(mapped_ptr_);
    if (header->magic != 0x4F4253444D4D4150ULL) {
        return Status::Corruption("Fatal journal corruption: invalid file header magic");
    }

    size_t current_offset = sizeof(JournalFileHeader);
    size_t max_offset = static_cast<size_t>(header->write_offset);
    if (max_offset > capacity_) {
        report.has_torn_write = true;
        report.torn_offset = sizeof(JournalFileHeader);
        report.torn_bytes_quarantined = capacity_ - sizeof(JournalFileHeader);
        max_offset = capacity_;
    }

    while (current_offset < max_offset) {
        if (current_offset + sizeof(JournalRecordHeader) > max_offset) {
            // Incomplete header -> torn write!
            report.has_torn_write = true;
            report.torn_offset = current_offset;
            report.torn_bytes_quarantined = max_offset - current_offset;
            break;
        }

        const auto* rec = reinterpret_cast<const JournalRecordHeader*>(mapped_ptr_ + current_offset);
        if (rec->magic != 0xAA55) {
            report.has_torn_write = true;
            report.torn_offset = current_offset;
            report.torn_bytes_quarantined = max_offset - current_offset;
            break;
        }

        size_t full_rec_size = sizeof(JournalRecordHeader) + rec->length + sizeof(JournalRecordTail);
        if (current_offset + full_rec_size > max_offset) {
            // Incomplete body/tail -> torn write!
            report.has_torn_write = true;
            report.torn_offset = current_offset;
            report.torn_bytes_quarantined = max_offset - current_offset;
            break;
        }

        // Validate payload CRC32C
        const uint8_t* payload_ptr = mapped_ptr_ + current_offset + sizeof(JournalRecordHeader);
        uint32_t computed_crc = common::CRC32C::Calculate(payload_ptr, rec->length);
        if (computed_crc != rec->crc32c) {
            report.has_torn_write = true;
            report.torn_offset = current_offset;
            report.torn_bytes_quarantined = max_offset - current_offset;
            break;
        }

        // Validate tail marker
        const auto* tail = reinterpret_cast<const JournalRecordTail*>(payload_ptr + rec->length);
        if (tail->tail_magic != 0x55AA) {
            report.has_torn_write = true;
            report.torn_offset = current_offset;
            report.torn_bytes_quarantined = max_offset - current_offset;
            break;
        }

        report.valid_records++;
        report.valid_bytes += full_rec_size;
        current_offset += full_rec_size;
    }

    if (report.has_torn_write && repair_torn_tail && mode_ == MemoryMapMode::ReadWrite) {
        header->write_offset = report.torn_offset;
        header->record_count = report.valid_records;
        // Zero out quarantined region
        if (report.torn_bytes_quarantined > 0 && report.torn_offset + report.torn_bytes_quarantined <= capacity_) {
            std::memset(mapped_ptr_ + report.torn_offset, 0, report.torn_bytes_quarantined);
        }
        header->checksum = common::CRC32C::Calculate(header, sizeof(JournalFileHeader) - sizeof(uint32_t) - sizeof(header->reserved));
        (void)Flush();
    }

    return report;
}

Status MemoryMappedJournal::Flush() {
    if (!IsOpen()) {
        return Status::IOError("Journal is not open to flush");
    }
#ifdef _WIN32
    if (!FlushViewOfFile(mapped_ptr_, 0)) {
        return Status::IOError("FlushViewOfFile failed on Windows");
    }
#else
    if (::msync(mapped_ptr_, capacity_, MS_SYNC) != 0) {
        return Status::IOError("msync failed on POSIX");
    }
#endif
    return Status::Ok();
}

void MemoryMappedJournal::Close() {
    if (mapped_ptr_) {
#ifdef _WIN32
        UnmapViewOfFile(mapped_ptr_);
#else
        ::munmap(mapped_ptr_, capacity_);
#endif
        mapped_ptr_ = nullptr;
    }

#ifdef _WIN32
    if (map_handle_) {
        CloseHandle(map_handle_);
        map_handle_ = nullptr;
    }
    if (file_handle_) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
    capacity_ = 0;
}

size_t MemoryMappedJournal::WriteOffset() const noexcept {
    if (!IsOpen()) return 0;
    const auto* header = reinterpret_cast<const JournalFileHeader*>(mapped_ptr_);
    return static_cast<size_t>(header->write_offset);
}

uint64_t MemoryMappedJournal::RecordCount() const noexcept {
    if (!IsOpen()) return 0;
    const auto* header = reinterpret_cast<const JournalFileHeader*>(mapped_ptr_);
    return header->record_count;
}

} // namespace mmap
} // namespace storage
} // namespace obsidian
