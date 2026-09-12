#include "allocator.hpp"

#include <cstdlib>
#include <cstring>

#if defined(_MSC_VER)
#include <malloc.h>
#elif defined(__MINGW32__) || defined(__MINGW64__)
#include <malloc.h>
#endif

namespace obsidian {
namespace kernel {
namespace memory {

/// Allocates memory aligned to the specified power-of-two boundary.
/// DDIA hardware guideline: Essential for preventing false sharing between
/// producer and consumer threads accessing adjacent memory addresses.
void* cache_aligned_alloc(size_t size, size_t alignment = kCacheLineSize) noexcept {
    if (size == 0) {
        return nullptr;
    }
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#elif defined(__MINGW32__) || defined(__MINGW64__)
    return __mingw_aligned_malloc(size, alignment);
#elif defined(_ISOC11_SOURCE) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L)
    const size_t rounded_size = align_up(size, alignment);
    return std::aligned_alloc(alignment, rounded_size);
#elif defined(__APPLE__) || defined(__linux__)
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
#else
    // Fully portable fallback guaranteeing exact alignment
    void* raw = std::malloc(size + alignment + sizeof(void*));
    if (!raw) return nullptr;
    void* aligned = reinterpret_cast<void*>(align_up(reinterpret_cast<uintptr_t>(raw) + sizeof(void*), alignment));
    reinterpret_cast<void**>(aligned)[-1] = raw;
    return aligned;
#endif
}

/// Frees memory previously allocated with `cache_aligned_alloc`.
void cache_aligned_free(void* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
#if defined(_MSC_VER)
    _aligned_free(ptr);
#elif defined(__MINGW32__) || defined(__MINGW64__)
    __mingw_aligned_free(ptr);
#elif defined(_ISOC11_SOURCE) || defined(__APPLE__) || defined(__linux__)
    std::free(ptr);
#else
    void* raw = reinterpret_cast<void**>(ptr)[-1];
    std::free(raw);
#endif
}

/// RAII wrapper for cache-aligned contiguous byte buffers.
class CacheAlignedBuffer {
public:
    explicit CacheAlignedBuffer(size_t capacity, size_t alignment = kCacheLineSize) noexcept
        : capacity_(capacity), alignment_(alignment) {
        data_ = static_cast<uint8_t*>(cache_aligned_alloc(capacity, alignment));
    }

    ~CacheAlignedBuffer() noexcept {
        if (data_) {
            cache_aligned_free(data_);
            data_ = nullptr;
        }
    }

    CacheAlignedBuffer(const CacheAlignedBuffer&) = delete;
    CacheAlignedBuffer& operator=(const CacheAlignedBuffer&) = delete;

    CacheAlignedBuffer(CacheAlignedBuffer&& other) noexcept
        : data_(other.data_), capacity_(other.capacity_), alignment_(other.alignment_) {
        other.data_ = nullptr;
        other.capacity_ = 0;
    }

    CacheAlignedBuffer& operator=(CacheAlignedBuffer&& other) noexcept {
        if (this != &other) {
            if (data_) {
                cache_aligned_free(data_);
            }
            data_ = other.data_;
            capacity_ = other.capacity_;
            alignment_ = other.alignment_;
            other.data_ = nullptr;
            other.capacity_ = 0;
        }
        return *this;
    }

    OBSIDIAN_NODISCARD uint8_t* data() noexcept { return data_; }
    OBSIDIAN_NODISCARD const uint8_t* data() const noexcept { return data_; }
    OBSIDIAN_NODISCARD size_t capacity() const noexcept { return capacity_; }
    OBSIDIAN_NODISCARD size_t alignment() const noexcept { return alignment_; }
    OBSIDIAN_NODISCARD bool is_valid() const noexcept { return data_ != nullptr; }

    void zero() noexcept {
        if (data_ && capacity_ > 0) {
            std::memset(data_, 0, capacity_);
        }
    }

private:
    uint8_t* data_{nullptr};
    size_t capacity_{0};
    size_t alignment_{kCacheLineSize};
};

} // namespace memory
} // namespace kernel
} // namespace obsidian
