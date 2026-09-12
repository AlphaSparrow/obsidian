#include "allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace obsidian {
namespace kernel {
namespace memory {

/// Zero-Copy Memory-Mapped Storage.
///
/// DDIA Chapter 3 (Storage & Retrieval - Append-Only Log Segments & SSTables):
/// Maps disk-persisted data directly into the application's virtual address space,
/// allowing the OS page cache to handle hardware prefetching and page eviction
/// without user/kernel buffer copy overhead.
class MmapStorage {
public:
    MmapStorage() = default;

    ~MmapStorage() noexcept {
        close();
    }

    MmapStorage(const MmapStorage&) = delete;
    MmapStorage& operator=(const MmapStorage&) = delete;

    MmapStorage(MmapStorage&& other) noexcept
        : data_(other.data_), size_(other.size_)
#if defined(_WIN32)
        , file_handle_(other.file_handle_), map_handle_(other.map_handle_)
#else
        , fd_(other.fd_)
#endif
    {
        other.data_ = nullptr;
        other.size_ = 0;
#if defined(_WIN32)
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.map_handle_ = nullptr;
#else
        other.fd_ = -1;
#endif
    }

    bool open_mapping(const std::string& path, size_t size, bool read_only = false) {
        close();

#if defined(_WIN32)
        DWORD access = read_only ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
        DWORD share_mode = FILE_SHARE_READ;
        DWORD creation = read_only ? OPEN_EXISTING : OPEN_ALWAYS;

        file_handle_ = CreateFileA(
            path.c_str(), access, share_mode, nullptr, creation,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr
        );

        if (file_handle_ == INVALID_HANDLE_VALUE) {
            return false;
        }

        if (!read_only && size > 0) {
            LARGE_INTEGER li;
            li.QuadPart = static_cast<LONGLONG>(size);
            SetFilePointerEx(file_handle_, li, nullptr, FILE_BEGIN);
            SetEndOfFile(file_handle_);
        }

        DWORD protect = read_only ? PAGE_READONLY : PAGE_READWRITE;
        map_handle_ = CreateFileMappingA(file_handle_, nullptr, protect, 0, 0, nullptr);
        if (map_handle_ == nullptr) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        DWORD map_access = read_only ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;
        data_ = MapViewOfFile(map_handle_, map_access, 0, 0, size);
        if (data_ == nullptr) {
            CloseHandle(map_handle_);
            CloseHandle(file_handle_);
            map_handle_ = nullptr;
            file_handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        size_ = size;
        return true;
#else
        int flags = read_only ? O_RDONLY : (O_RDWR | O_CREAT);
        fd_ = open(path.c_str(), flags, 0644);
        if (fd_ == -1) {
            return false;
        }

        if (!read_only && size > 0) {
            if (ftruncate(fd_, static_cast<off_t>(size)) != 0) {
                ::close(fd_);
                fd_ = -1;
                return false;
            }
        }

        int prot = read_only ? PROT_READ : (PROT_READ | PROT_WRITE);
        void* addr = mmap(nullptr, size, prot, MAP_SHARED, fd_, 0);
        if (addr == MAP_FAILED) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        madvise(addr, size, MADV_SEQUENTIAL);

        data_ = addr;
        size_ = size;
        return true;
#endif
    }

    void sync() noexcept {
        if (data_ && size_ > 0) {
#if defined(_WIN32)
            FlushViewOfFile(data_, size_);
            FlushFileBuffers(file_handle_);
#else
            msync(data_, size_, MS_SYNC);
#endif
        }
    }

    void close() noexcept {
        if (data_) {
#if defined(_WIN32)
            UnmapViewOfFile(data_);
            if (map_handle_) {
                CloseHandle(map_handle_);
                map_handle_ = nullptr;
            }
            if (file_handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(file_handle_);
                file_handle_ = INVALID_HANDLE_VALUE;
            }
#else
            munmap(data_, size_);
            if (fd_ != -1) {
                ::close(fd_);
                fd_ = -1;
            }
#endif
            data_ = nullptr;
            size_ = 0;
        }
    }

    OBSIDIAN_NODISCARD void* data() noexcept { return data_; }
    OBSIDIAN_NODISCARD const void* data() const noexcept { return data_; }
    OBSIDIAN_NODISCARD size_t size() const noexcept { return size_; }
    OBSIDIAN_NODISCARD bool is_mapped() const noexcept { return data_ != nullptr; }

private:
    void* data_{nullptr};
    size_t size_{0};

#if defined(_WIN32)
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    HANDLE map_handle_{nullptr};
#else
    int fd_{-1};
#endif
};

} // namespace memory
} // namespace kernel
} // namespace obsidian
