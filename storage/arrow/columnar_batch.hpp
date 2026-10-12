#pragma once

#include "storage/schema/market_data_schema.hpp"
#include <vector>
#include <memory>
#include <string>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#include <malloc.h>
#endif

namespace obsidian {
namespace storage {
namespace arrow {

// Aligned RAII buffer ensuring 64-byte cache line alignment for SIMD / AVX compatibility
class AlignedBuffer {
public:
    explicit AlignedBuffer(size_t bytes, size_t alignment = 64)
        : size_(bytes), alignment_(alignment) {
        if (bytes == 0) {
            data_ = nullptr;
            return;
        }
        size_t alloc_bytes = bytes;
        size_t rem = alloc_bytes % alignment;
        if (rem != 0) {
            alloc_bytes += (alignment - rem);
        }
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
        data_ = static_cast<uint8_t*>(_aligned_malloc(alloc_bytes, alignment));
#else
        void* ptr = nullptr;
        if (posix_memalign(&ptr, alignment, alloc_bytes) != 0) {
            ptr = nullptr;
        }
        data_ = static_cast<uint8_t*>(ptr);
#endif
        if (!data_) {
            throw std::bad_alloc();
        }
    }

    ~AlignedBuffer() {
        if (data_) {
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
            _aligned_free(data_);
#else
            free(data_);
#endif
        }
    }

    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    AlignedBuffer(AlignedBuffer&& other) noexcept
        : data_(other.data_), size_(other.size_), alignment_(other.alignment_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            if (data_) {
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
                _aligned_free(data_);
#else
                free(data_);
#endif
            }
            data_ = other.data_;
            size_ = other.size_;
            alignment_ = other.alignment_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    [[nodiscard]] uint8_t* data() noexcept { return data_; }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }

private:
    uint8_t* data_{nullptr};
    size_t size_{0};
    size_t alignment_{64};
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
    ColumnVector(schema::DataType type, const T* data, size_t count, std::vector<uint8_t> null_bitmap = {})
        : type_(type), offset_(0), length_(count), null_bitmap_(std::move(null_bitmap)) {
        buffer_ = std::make_shared<AlignedBuffer>(count * sizeof(T), 64);
        if (data && count > 0) {
            std::memcpy(buffer_->data(), data, count * sizeof(T));
        }
    }

    ColumnVector(schema::DataType type, std::initializer_list<T> list, std::vector<uint8_t> null_bitmap = {})
        : ColumnVector(type, std::data(list), list.size(), std::move(null_bitmap)) {}

    // View constructor for zero-copy slicing
    ColumnVector(schema::DataType type, std::shared_ptr<AlignedBuffer> buffer,
                 size_t offset, size_t length, std::vector<uint8_t> null_bitmap = {})
        : type_(type), buffer_(std::move(buffer)), offset_(offset), length_(length),
          null_bitmap_(std::move(null_bitmap)) {}

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
        if (!buffer_ || !buffer_->data()) return nullptr;
        const T* base_ptr = reinterpret_cast<const T*>(buffer_->data());
        return static_cast<const void*>(base_ptr + offset_);
    }

    [[nodiscard]] const T& operator[](size_t index) const {
        if (index >= length_) {
            throw std::out_of_range("Index out of range in ColumnVector");
        }
        const T* base_ptr = reinterpret_cast<const T*>(buffer_->data());
        return base_ptr[offset_ + index];
    }

    [[nodiscard]] std::shared_ptr<IColumnVector> Slice(size_t offset, size_t length) const override {
        if (offset + length > length_) {
            throw std::out_of_range("Slice range exceeds vector bounds");
        }
        return std::make_shared<ColumnVector<T>>(type_, buffer_, offset_ + offset, length, null_bitmap_);
    }

private:
    schema::DataType type_;
    std::shared_ptr<AlignedBuffer> buffer_;
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
