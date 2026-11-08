#include "clickhouse_exporter.hpp"
#include <cstring>
#include <sstream>

namespace obsidian {
namespace storage {
namespace clickhouse {

std::string ClickHouseExporter::MapClickHouseType(schema::DataType type, bool nullable) {
    std::string base_type;
    switch (type) {
        case schema::DataType::Boolean: base_type = "UInt8"; break;
        case schema::DataType::Int8: base_type = "Int8"; break;
        case schema::DataType::Int16: base_type = "Int16"; break;
        case schema::DataType::Int32: base_type = "Int32"; break;
        case schema::DataType::Int64: base_type = "Int64"; break;
        case schema::DataType::Float: base_type = "Float32"; break;
        case schema::DataType::Double: base_type = "Float64"; break;
        case schema::DataType::TimestampNs: base_type = "DateTime64(9, 'UTC')"; break;
        case schema::DataType::String: base_type = "String"; break;
        default: base_type = "String"; break;
    }
    if (nullable) {
        return "Nullable(" + base_type + ")";
    }
    return base_type;
}

std::string ClickHouseExporter::GenerateMergeTreeDDL(
    const std::string& table_name,
    const schema::Schema& schema,
    const std::string& order_by_clause) {

    std::ostringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS " << table_name << " (\n";

    const auto& fields = schema.fields();
    std::string default_order = "";

    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& f = fields[i];
        ss << "    `" << f.name() << "` " << MapClickHouseType(f.type(), f.nullable());
        if (i + 1 < fields.size()) {
            ss << ",";
        }
        ss << "\n";

        if (default_order.empty() && (f.name() == "timestamp_ns" || f.name() == "bar_timestamp_ns")) {
            default_order = "`" + f.name() + "`";
        }
    }

    ss << ") ENGINE = MergeTree()\n";

    // Auto-detect partition and sorting keys
    if (schema.HasField("timestamp_ns")) {
        ss << "PARTITION BY toYYYYMM(timestamp_ns)\n";
    } else if (schema.HasField("bar_timestamp_ns")) {
        ss << "PARTITION BY toYYYYMM(bar_timestamp_ns)\n";
    }

    if (!order_by_clause.empty()) {
        ss << "ORDER BY (" << order_by_clause << ");";
    } else if (schema.HasField("symbol_id")) {
        ss << "ORDER BY (`symbol_id`, " << default_order << ");";
    } else {
        ss << "ORDER BY (" << default_order << ");";
    }

    return ss.str();
}

StatusOr<RowBinaryExportResult> ClickHouseExporter::EncodeRowBinary(
    const arrow::ColumnarBatch& batch) {

    size_t num_rows = batch.num_rows();
    size_t num_cols = batch.num_columns();
    const auto& schema = batch.schema();

    std::string buffer;
    buffer.reserve(num_rows * num_cols * 8);

    for (size_t r = 0; r < num_rows; ++r) {
        for (size_t c = 0; c < num_cols; ++c) {
            auto col = batch.column(c);
            const auto& field = schema.fields()[c];
            bool is_null = col->is_null(r);

            // Nullability constraint verification
            if (field.nullable()) {
                if (is_null) {
                    uint8_t null_byte = 1;
                    buffer.append(reinterpret_cast<const char*>(&null_byte), 1);
                    continue; // Skip payload for null value
                } else {
                    uint8_t null_byte = 0;
                    buffer.append(reinterpret_cast<const char*>(&null_byte), 1);
                }
            } else if (is_null) {
                return Status(StatusCode::InvalidArgument,
                    "Null value constraint violation in non-nullable column '" + field.name() +
                    "' at row " + std::to_string(r));
            }

            // Encode native value in Little-Endian format
            switch (field.type()) {
                case schema::DataType::Double: {
                    auto tc = batch.GetTypedColumn<double>(c);
                    double val = (*tc)[r];
                    buffer.append(reinterpret_cast<const char*>(&val), sizeof(double));
                    break;
                }
                case schema::DataType::Float: {
                    auto tc = batch.GetTypedColumn<float>(c);
                    float val = (*tc)[r];
                    buffer.append(reinterpret_cast<const char*>(&val), sizeof(float));
                    break;
                }
                case schema::DataType::Int64:
                case schema::DataType::TimestampNs: {
                    auto tc = batch.GetTypedColumn<int64_t>(c);
                    int64_t val = (*tc)[r];
                    buffer.append(reinterpret_cast<const char*>(&val), sizeof(int64_t));
                    break;
                }
                case schema::DataType::Int32: {
                    auto tc = batch.GetTypedColumn<int32_t>(c);
                    int32_t val = (*tc)[r];
                    buffer.append(reinterpret_cast<const char*>(&val), sizeof(int32_t));
                    break;
                }
                case schema::DataType::Int16: {
                    auto tc = batch.GetTypedColumn<int16_t>(c);
                    int16_t val = (*tc)[r];
                    buffer.append(reinterpret_cast<const char*>(&val), sizeof(int16_t));
                    break;
                }
                case schema::DataType::Int8:
                case schema::DataType::Boolean: {
                    auto tc = batch.GetTypedColumn<uint8_t>(c);
                    uint8_t val = (*tc)[r];
                    buffer.append(reinterpret_cast<const char*>(&val), sizeof(uint8_t));
                    break;
                }
                default:
                    return Status(StatusCode::InvalidArgument,
                        "Unsupported ClickHouse RowBinary export type: " + schema::DataTypeToString(field.type()));
            }
        }
    }

    RowBinaryExportResult res{};
    res.rows_encoded = num_rows;
    res.total_bytes = buffer.size();
    res.binary_payload = std::move(buffer);

    return res;
}

} // namespace clickhouse
} // namespace storage
} // namespace obsidian
