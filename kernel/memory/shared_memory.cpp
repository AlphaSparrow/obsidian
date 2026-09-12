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

/// Low-Latency Inter-Process Shared Memory Region.
///
/// DDIA High-Throughput Stream Pipeline Architecture:
/// Enables zero-copy, sub-microsecond IPC between local engine processes
/// (e.g. Market Data Ingestion Process -> Options Pricing Process -> Order Execution Engine).
class SharedMemoryRegion {
public:
    SharedMemoryRegion() = default;

    ~SharedMemoryRegion() noexcept {
        detach();
    }

    SharedMemoryRegion(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;

    bool create_or_open(const std::string& name, size_t size, bool create_new = true) {
        detach();

#if defined(_WIN32)
        DWORD protect = PAGE_READWRITE;
        DWORD high_size = static_cast<DWORD>((static_cast<uint64_t>(size) >> 32) & 0xFFFFFFFF);
        DWORD low_size = static_cast<DWORD>(size & 0xFFFFFFFF);

        if (create_new) {
            handle_ = CreateFileMappingA(
                INVALID_HANDLE_VALUE, nullptr, protect, high_size, low_size, name.c_str()
            );
        } else {
            handle_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        }

        if (handle_ == nullptr) {
            return false;
        }

        data_ = MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (data_ == nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
            return false;
        }

        size_ = size;
        name_ = name;
        return true;
#else
        int flags = create_new ? (O_RDWR | O_CREAT) : O_RDWR;
        fd_ = shm_open(name.c_str(), flags, 0666);
        if (fd_ == -1) {
            return false;
        }

        if (create_new && size > 0) {
            if (ftruncate(fd_, static_cast<off_t>(size)) != 0) {
                ::close(fd_);
                fd_ = -1;
                return false;
            }
        }

        void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (addr == MAP_FAILED) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        data_ = addr;
        size_ = size;
        name_ = name;
        return true;
#endif
    }

    void detach() noexcept {
        if (data_) {
#if defined(_WIN32)
            UnmapViewOfFile(data_);
            if (handle_) {
                CloseHandle(handle_);
                handle_ = nullptr;
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
            name_.clear();
        }
    }

    OBSIDIAN_NODISCARD void* data() noexcept { return data_; }
    OBSIDIAN_NODISCARD const void* data() const noexcept { return data_; }
    OBSIDIAN_NODISCARD size_t size() const noexcept { return size_; }
    OBSIDIAN_NODISCARD bool is_attached() const noexcept { return data_ != nullptr; }

private:
    void* data_{nullptr};
    size_t size_{0};
    std::string name_;

#if defined(_WIN32)
    HANDLE handle_{nullptr};
#else
    int fd_{-1};
#endif
};

} // namespace memory
} // namespace kernel
} // namespace obsidian
