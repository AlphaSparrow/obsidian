#pragma once

#include "storage/schema/market_data_schema.hpp"
#include <vector>
#include <memory>
#include <string>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <cstdlib>

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#include <malloc.h>
#endif

namespace obsidian {
namespace storage {
namespace arrow {

// Aligned allocator ensuring 64-byte cache line alignment for SIMD / AVX compatibility
template <typename T, size_t Alignment = 64>
class AlignedAllocator {
public:
    using value_type = T;

    AlignedAllocator() noexcept = default;
    template <typename U>
    constexpr AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(size_t n) {
        if (n == 0) return nullptr;
        size_t bytes = n * sizeof(T);
        // Align bytes to multiple of Alignment
        size_t remainder = bytes % Alignment;
        if (remainder != 0) {
            bytes += (Alignment - remainder);
        }
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
        void* ptr = _aligned_malloc(bytes, Alignment);
        if (!ptr) throw std::bad_alloc();
#else
        void* ptr = nullptr;
        if (posix_memalign(&ptr, Alignment, bytes) != 0) {
            throw std::bad_alloc();
        }
#endif
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, size_t) noexcept {
        if (!ptr) return;
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }

    template <typename U>
    bool operator==(const AlignedAllocator<U, Alignment>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const AlignedAllocator<U, Alignment>&) const noexcept { return false; }
};

class IColumnVector {
public:
    virtual ~IColumnVector() = default;

    [[nodiscard]] virtual schema::DataType type() const noexcept = 0;
    [[nodiscard]] virtual size_t size() const noexcept = 0;
    [[nodiscard]] virtual bool is_null(size_t index) const = 0;
    [[nodiscard]] virtual const void* raw_data() const noexcept = 0;
    [[nodiscard]] virtual std::shared_ptr<IColumnVector> Slice(size_t offset, size_t length) const = 0;
};

template <typename T>
class ColumnVector : public IColumnVector {
public:
    using Allocator = AlignedAllocator<T, 64>;

    ColumnVector(schema::DataType type, std::vector<T, Allocator> data, std::vector<uint8_t> null_bitmap = {})
        : type_(type), data_(std::move(data)), null_bitmap_(std::move(null_bitmap)), 
          offset_(0), length_(data_.size()) {}

    // View constructor for zero-copy slicing
    ColumnVector(schema::DataType type, std::shared_ptr<const std::vector<T, Allocator>> shared_data,
                 size_t offset, size_t length, std::vector<uint8_t> null_bitmap = {})
        : type_(type), shared_data_(std::move(shared_data)),
          offset_(offset), length_(length), null_bitmap_(std::move(null_bitmap)) {}

    [[nodiscard]] schema::DataType type() const noexcept override {
        return type_;
    }

    [[nodiscard]] size_t size() const noexcept override {
        return length_;
    }

    [[nodiscard]] bool is_null(size_t index) const override {
        if (index >= length_) {
            throw std::out_of_range("Index out of range in ColumnVector::is_null");
        }
        if (null_bitmap_.empty()) {
            return false;
        }
        size_t actual_idx = offset_ + index;
        size_t byte_idx = actual_idx / 8;
        size_t bit_idx = actual_idx % 8;
        if (byte_idx >= null_bitmap_.size()) {
            return false;
        }
        return !(null_bitmap_[byte_idx] & (1 << bit_idx));
    }

    [[nodiscard]] const void* raw_data() const noexcept override {
        const T* base_ptr = shared_data_ ? shared_data_->data() : data_.data();
        return static_cast<const void*>(base_ptr + offset_);
    }

    [[nodiscard]] const T& operator[](size_t index) const {
        if (index >= length_) {
            throw std::out_of_range("Index out of range in ColumnVector");
        }
        const T* base_ptr = shared_data_ ? shared_data_->data() : data_.data();
        return base_ptr[offset_ + index];
    }

    [[nodiscard]] std::shared_ptr<IColumnVector> Slice(size_t offset, size_t length) const override {
        if (offset + length > length_) {
            throw std::out_of_range("Slice range exceeds vector bounds");
        }

        std::shared_ptr<const std::vector<T, Allocator>> parent_data;
        if (shared_data_) {
            parent_data = shared_data_;
        } else {
            parent_data = std::make_shared<const std::vector<T, Allocator>>(data_);
        }

        return std::make_shared<ColumnVector<T>>(type_, parent_data, offset_ + offset, length, null_bitmap_);
    }

private:
    schema::DataType type_;
    std::vector<T, Allocator> data_;
    std::shared_ptr<const std::vector<T, Allocator>> shared_data_;
    size_t offset_{0};
    size_t length_{0};
    std::vector<uint8_t> null_bitmap_;
};

class ColumnarBatch {
public:
    ColumnarBatch(schema::Schema schema, std::vector<std::shared_ptr<IColumnVector>> columns, size_t num_rows)
        : schema_(std::move(schema)), columns_(std::move(columns)), num_rows_(num_rows) {
        if (schema_.num_fields() != columns_.size()) {
            throw std::invalid_argument("Schema field count does not match column count");
        }
    }

    [[nodiscard]] const schema::Schema& schema() const noexcept {
        return schema_;
    }

    [[nodiscard]] size_t num_columns() const noexcept {
        return columns_.size();
    }

    [[nodiscard]] size_t num_rows() const noexcept {
        return num_rows_;
    }

    [[nodiscard]] std::shared_ptr<IColumnVector> column(size_t index) const {
        if (index >= columns_.size()) {
            throw std::out_of_range("Column index out of range: " + std::to_string(index));
        }
        return columns_[index];
    }

    [[nodiscard]] std::shared_ptr<IColumnVector> column(const std::string& name) const {
        int idx = schema_.GetFieldIndex(name);
        if (idx < 0) {
            throw std::invalid_argument("Column not found in schema: " + name);
        }
        return columns_[static_cast<size_t>(idx)];
    }

    template <typename T>
    [[nodiscard]] std::shared_ptr<ColumnVector<T>> GetTypedColumn(size_t index) const {
        auto col = column(index);
        auto typed = std::dynamic_pointer_cast<ColumnVector<T>>(col);
        if (!typed) {
            throw std::runtime_error("Column type mismatch at index: " + std::to_string(index));
        }
        return typed;
    }

    [[nodiscard]] std::shared_ptr<ColumnarBatch> Slice(size_t offset, size_t length) const {
        if (offset + length > num_rows_) {
            throw std::out_of_range("Slice range exceeds batch row count");
        }

        std::vector<std::shared_ptr<IColumnVector>> sliced_columns;
        sliced_columns.reserve(columns_.size());
        for (const auto& col : columns_) {
            sliced_columns.push_back(col->Slice(offset, length));
        }

        return std::make_shared<ColumnarBatch>(schema_, std::move(sliced_columns), length);
    }

private:
    schema::Schema schema_;
    std::vector<std::shared_ptr<IColumnVector>> columns_;
    size_t num_rows_;
};

} // namespace arrow
} // namespace storage
} // namespace obsidian
