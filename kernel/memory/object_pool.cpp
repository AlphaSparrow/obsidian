#include "allocator.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

namespace obsidian {
namespace kernel {
namespace memory {

extern void* cache_aligned_alloc(size_t size, size_t alignment) noexcept;
extern void cache_aligned_free(void* ptr) noexcept;

/// High-Performance Fixed-Size Block Object Pool.
///
/// DDIA Chapter 3 (Storage Engine Record Allocation):
/// Eliminates runtime heap thrashing by recycling pre-allocated uniform memory blocks.
///
/// Complexity:
///   - acquire(): O(1) deterministic free-list pop
///   - release(): O(1) deterministic free-list push
/// Memory Layout:
///   Uses intrusive linked list nodes inside free slots, requiring 0 extra memory overhead
///   per unallocated slot.
template <typename T>
class ObjectPool {
public:
    union Node {
        Node* next;
        alignas(alignof(T)) uint8_t storage[sizeof(T)];
    };

    explicit ObjectPool(size_t initial_capacity = 1024)
        : block_size_(initial_capacity < 16 ? 16 : initial_capacity),
          free_list_(nullptr),
          allocated_count_(0) {
        expand_pool();
    }

    ~ObjectPool() noexcept {
        for (void* block : memory_blocks_) {
            cache_aligned_free(block);
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    template <typename... Args>
    OBSIDIAN_NODISCARD T* acquire(Args&&... args) {
        if (free_list_ == nullptr) {
            expand_pool();
        }

        Node* node = free_list_;
        free_list_ = node->next;
        ++allocated_count_;

        T* obj = reinterpret_cast<T*>(node->storage);
        new (obj) T(std::forward<Args>(args)...);
        return obj;
    }

    void release(T* obj) noexcept {
        if (obj == nullptr) {
            return;
        }

        obj->~T();

        Node* node = reinterpret_cast<Node*>(obj);
        node->next = free_list_;
        free_list_ = node;

        if (allocated_count_ > 0) {
            --allocated_count_;
        }
    }

    OBSIDIAN_NODISCARD size_t allocated_count() const noexcept { return allocated_count_; }
    OBSIDIAN_NODISCARD size_t total_capacity() const noexcept { return memory_blocks_.size() * block_size_; }

private:
    void expand_pool() {
        const size_t bytes = block_size_ * sizeof(Node);
        Node* new_block = static_cast<Node*>(cache_aligned_alloc(bytes, kCacheLineSize));
        if (new_block == nullptr) {
            return;
        }

        memory_blocks_.push_back(new_block);

        for (size_t i = 0; i < block_size_ - 1; ++i) {
            new_block[i].next = &new_block[i + 1];
        }
        new_block[block_size_ - 1].next = free_list_;
        free_list_ = &new_block[0];
    }

    size_t block_size_;
    Node* free_list_;
    size_t allocated_count_;
    std::vector<void*> memory_blocks_;
};

template class ObjectPool<uint64_t>;

} // namespace memory
} // namespace kernel
} // namespace obsidian
