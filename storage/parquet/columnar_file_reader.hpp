#pragma once

#include "storage/common/file_system.hpp"
#include "storage/common/status.hpp"
#include "storage/arrow/columnar_batch.hpp"
#include "storage/schema/market_data_schema.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace obsidian {
namespace storage {
namespace parquet {

class ColumnarFileReader {
public:
    static StatusOr<std::unique_ptr<ColumnarFileReader>> Open(
        const std::string& file_path,
        std::shared_ptr<IFileSystem> fs = LocalFileSystem::Default());

    [[nodiscard]] const schema::Schema& schema() const noexcept { return schema_; }
    [[nodiscard]] size_t num_rows() const noexcept { return num_rows_; }
    [[nodiscard]] size_t num_columns() const noexcept { return schema_.num_fields(); }
    [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }

    [[nodiscard]] const std::unordered_map<std::string, std::string>& min_values() const noexcept { return min_values_; }
    [[nodiscard]] const std::unordered_map<std::string, std::string>& max_values() const noexcept { return max_values_; }

    // Reads all columns into a 64-byte aligned ColumnarBatch
    StatusOr<std::shared_ptr<arrow::ColumnarBatch>> ReadAll();

    // Projection Pushdown: Reads only requested column names into a ColumnarBatch
    StatusOr<std::shared_ptr<arrow::ColumnarBatch>> ReadColumns(const std::vector<std::string>& column_names);

private:
    ColumnarFileReader(std::string file_path, std::shared_ptr<IFileSystem> fs);

    struct ColumnMeta {
        std::string name;
        schema::DataType type;
        bool nullable{false};
        std::string min_val;
        std::string max_val;
        uint64_t data_offset{0};
        uint64_t data_bytes{0};
        uint64_t null_count{0};
        uint64_t null_bitmap_offset{0};
        uint64_t null_bitmap_bytes{0};
    };

    std::shared_ptr<arrow::IColumnVector> ConstructColumnVector(
        const ColumnMeta& meta,
        const uint8_t* raw_data,
        const std::vector<uint8_t>& null_bitmap);

    std::string file_path_;
    std::shared_ptr<IFileSystem> fs_;
    schema::Schema schema_{{}};
    size_t num_rows_{0};
    std::vector<ColumnMeta> column_metas_;
    std::unordered_map<std::string, size_t> column_name_to_idx_;
    std::unordered_map<std::string, std::string> min_values_;
    std::unordered_map<std::string, std::string> max_values_;
    std::string cached_data_;
};

} // namespace parquet
} // namespace storage
} // namespace obsidian
