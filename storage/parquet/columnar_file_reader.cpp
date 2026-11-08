#include "columnar_file_reader.hpp"
#include "columnar_file_writer.hpp"
#include <cstring>
#include <stdexcept>

namespace obsidian {
namespace storage {
namespace parquet {

namespace {

template <typename T>
T ReadVal(const char*& ptr, const char* end) {
    if (ptr + sizeof(T) > end) {
        throw std::runtime_error("Unexpected EOF while reading columnar binary file");
    }
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    ptr += sizeof(T);
    return val;
}

std::string ReadStr(const char*& ptr, const char* end) {
    uint16_t len = ReadVal<uint16_t>(ptr, end);
    if (ptr + len > end) {
        throw std::runtime_error("Unexpected EOF while reading string field");
    }
    std::string s(ptr, len);
    ptr += len;
    return s;
}

} // namespace

ColumnarFileReader::ColumnarFileReader(std::string file_path, std::shared_ptr<IFileSystem> fs)
    : file_path_(std::move(file_path)), fs_(std::move(fs)) {}

StatusOr<std::unique_ptr<ColumnarFileReader>> ColumnarFileReader::Open(
    const std::string& file_path,
    std::shared_ptr<IFileSystem> fs) {
    if (!fs) {
        fs = LocalFileSystem::Default();
    }

    auto exists_st = fs->Exists(file_path);
    if (!exists_st.ok()) return exists_st.status();
    if (!exists_st.value()) {
        return Status(StatusCode::NotFound, "Columnar file not found: " + file_path);
    }

    auto data_st = fs->ReadAllText(file_path);
    if (!data_st.ok()) return data_st.status();

    const std::string& raw = data_st.value();
    if (raw.size() < 24) {
        return Status(StatusCode::DataLoss, "Columnar file too small or corrupted: " + file_path);
    }

    // Verify magic bytes
    if (std::memcmp(raw.data(), kColumnarMagic, 4) != 0) {
        return Status(StatusCode::InvalidArgument, "Invalid columnar file magic bytes in: " + file_path);
    }

    auto reader = std::unique_ptr<ColumnarFileReader>(new ColumnarFileReader(file_path, fs));
    reader->cached_data_ = std::move(data_st.value());

    const char* ptr = reader->cached_data_.data() + 4;
    const char* end = reader->cached_data_.data() + reader->cached_data_.size();

    try {
        uint32_t version = ReadVal<uint32_t>(ptr, end);
        if (version != kColumnarVersion) {
            return Status(StatusCode::InvalidArgument, "Unsupported columnar file version: " + std::to_string(version));
        }

        reader->num_rows_ = static_cast<size_t>(ReadVal<uint64_t>(ptr, end));
        uint32_t num_cols = ReadVal<uint32_t>(ptr, end);

        std::vector<schema::Field> fields;
        fields.reserve(num_cols);
        reader->column_metas_.reserve(num_cols);

        for (uint32_t i = 0; i < num_cols; ++i) {
            ColumnMeta meta{};
            meta.name = ReadStr(ptr, end);
            meta.type = static_cast<schema::DataType>(ReadVal<uint8_t>(ptr, end));
            meta.nullable = (ReadVal<uint8_t>(ptr, end) != 0);
            meta.min_val = ReadStr(ptr, end);
            meta.max_val = ReadStr(ptr, end);
            meta.data_offset = ReadVal<uint64_t>(ptr, end);
            meta.data_bytes = ReadVal<uint64_t>(ptr, end);
            meta.null_count = ReadVal<uint64_t>(ptr, end);
            meta.null_bitmap_offset = ReadVal<uint64_t>(ptr, end);
            meta.null_bitmap_bytes = ReadVal<uint64_t>(ptr, end);

            fields.emplace_back(meta.name, meta.type, meta.nullable);
            if (!meta.min_val.empty()) {
                reader->min_values_[meta.name] = meta.min_val;
                reader->max_values_[meta.name] = meta.max_val;
            }
            reader->column_name_to_idx_[meta.name] = i;
            reader->column_metas_.push_back(std::move(meta));
        }

        reader->schema_ = schema::Schema(std::move(fields));
    } catch (const std::exception& e) {
        return Status(StatusCode::DataLoss, std::string("Corrupted metadata: ") + e.what());
    }

    return reader;
}

std::shared_ptr<arrow::IColumnVector> ColumnarFileReader::ConstructColumnVector(
    const ColumnMeta& meta,
    const uint8_t* raw_data,
    const std::vector<uint8_t>& null_bitmap) {

    switch (meta.type) {
        case schema::DataType::Double:
            return std::make_shared<arrow::ColumnVector<double>>(
                meta.type, reinterpret_cast<const double*>(raw_data), num_rows_, null_bitmap);
        case schema::DataType::Float:
            return std::make_shared<arrow::ColumnVector<float>>(
                meta.type, reinterpret_cast<const float*>(raw_data), num_rows_, null_bitmap);
        case schema::DataType::Int64:
        case schema::DataType::TimestampNs:
            return std::make_shared<arrow::ColumnVector<int64_t>>(
                meta.type, reinterpret_cast<const int64_t*>(raw_data), num_rows_, null_bitmap);
        case schema::DataType::Int32:
            return std::make_shared<arrow::ColumnVector<int32_t>>(
                meta.type, reinterpret_cast<const int32_t*>(raw_data), num_rows_, null_bitmap);
        case schema::DataType::Int16:
            return std::make_shared<arrow::ColumnVector<int16_t>>(
                meta.type, reinterpret_cast<const int16_t*>(raw_data), num_rows_, null_bitmap);
        case schema::DataType::Int8:
            return std::make_shared<arrow::ColumnVector<int8_t>>(
                meta.type, reinterpret_cast<const int8_t*>(raw_data), num_rows_, null_bitmap);
        case schema::DataType::Boolean:
            return std::make_shared<arrow::ColumnVector<uint8_t>>(
                meta.type, reinterpret_cast<const uint8_t*>(raw_data), num_rows_, null_bitmap);
        default:
            throw std::runtime_error("Unsupported column type: " + std::to_string(static_cast<int>(meta.type)));
    }
}

StatusOr<std::shared_ptr<arrow::ColumnarBatch>> ColumnarFileReader::ReadAll() {
    std::vector<std::shared_ptr<arrow::IColumnVector>> columns;
    columns.reserve(column_metas_.size());

    const char* base_ptr = cached_data_.data();
    size_t file_len = cached_data_.size();

    for (const auto& meta : column_metas_) {
        if (meta.data_offset + meta.data_bytes > file_len) {
            return Status(StatusCode::DataLoss, "Column data offset out of bounds for: " + meta.name);
        }

        const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(base_ptr + meta.data_offset);
        std::vector<uint8_t> null_bitmap;

        if (meta.null_bitmap_bytes > 0) {
            if (meta.null_bitmap_offset + meta.null_bitmap_bytes > file_len) {
                return Status(StatusCode::DataLoss, "Null bitmap offset out of bounds for: " + meta.name);
            }
            null_bitmap.resize(meta.null_bitmap_bytes);
            std::memcpy(null_bitmap.data(), base_ptr + meta.null_bitmap_offset, meta.null_bitmap_bytes);
        }

        try {
            columns.push_back(ConstructColumnVector(meta, raw_data, null_bitmap));
        } catch (const std::exception& e) {
            return Status(StatusCode::Internal, e.what());
        }
    }

    return std::make_shared<arrow::ColumnarBatch>(schema_, std::move(columns), num_rows_);
}

StatusOr<std::shared_ptr<arrow::ColumnarBatch>> ColumnarFileReader::ReadColumns(
    const std::vector<std::string>& column_names) {

    std::vector<schema::Field> projected_fields;
    std::vector<std::shared_ptr<arrow::IColumnVector>> projected_columns;
    projected_fields.reserve(column_names.size());
    projected_columns.reserve(column_names.size());

    const char* base_ptr = cached_data_.data();
    size_t file_len = cached_data_.size();

    for (const auto& name : column_names) {
        auto it = column_name_to_idx_.find(name);
        if (it == column_name_to_idx_.end()) {
            return Status(StatusCode::NotFound, "Requested column not present in file: " + name);
        }

        const auto& meta = column_metas_[it->second];
        projected_fields.emplace_back(meta.name, meta.type, meta.nullable);

        if (meta.data_offset + meta.data_bytes > file_len) {
            return Status(StatusCode::DataLoss, "Column data offset out of bounds for: " + meta.name);
        }

        const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(base_ptr + meta.data_offset);
        std::vector<uint8_t> null_bitmap;

        if (meta.null_bitmap_bytes > 0) {
            if (meta.null_bitmap_offset + meta.null_bitmap_bytes > file_len) {
                return Status(StatusCode::DataLoss, "Null bitmap offset out of bounds for: " + meta.name);
            }
            null_bitmap.resize(meta.null_bitmap_bytes);
            std::memcpy(null_bitmap.data(), base_ptr + meta.null_bitmap_offset, meta.null_bitmap_bytes);
        }

        try {
            projected_columns.push_back(ConstructColumnVector(meta, raw_data, null_bitmap));
        } catch (const std::exception& e) {
            return Status(StatusCode::Internal, e.what());
        }
    }

    schema::Schema proj_schema(std::move(projected_fields));
    return std::make_shared<arrow::ColumnarBatch>(std::move(proj_schema), std::move(projected_columns), num_rows_);
}

} // namespace parquet
} // namespace storage
} // namespace obsidian
