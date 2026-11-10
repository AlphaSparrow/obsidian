#pragma once

#include "storage/arrow/columnar_batch.hpp"
#include "storage/schema/market_data_schema.hpp"
#include "storage/lakehouse/partition_spec.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <memory>

namespace obsidian {
namespace storage {
namespace clickhouse {

struct RowBinaryExportResult {
    size_t rows_encoded{0};
    size_t total_bytes{0};
    std::string binary_payload;
};

class ClickHouseExporter {
public:
    // Generates optimal ClickHouse MergeTree DDL corresponding to an Obsidian Schema and PartitionSpec
    static std::string GenerateMergeTreeDDL(
        const std::string& table_name,
        const schema::Schema& schema,
        const std::string& order_by_clause = "");

    // Encodes an Arrow-compatible ColumnarBatch into ClickHouse Native RowBinary stream format
    // Validates column nullability constraints, endianness, and range limits
    static StatusOr<RowBinaryExportResult> EncodeRowBinary(
        const arrow::ColumnarBatch& batch);

    // Maps an Obsidian DataType to its corresponding ClickHouse column data type
    static std::string MapClickHouseType(schema::DataType type, bool nullable = false);
};

} // namespace clickhouse
} // namespace storage
} // namespace obsidian
