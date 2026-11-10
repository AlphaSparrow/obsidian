#include "duckdb_bridge.hpp"
#include "storage/parquet/columnar_file_reader.hpp"
#include <cstring>
#include <cstdlib>

namespace obsidian {
namespace storage {
namespace duckdb_engine {

namespace {

const char* MapArrowTypeFormat(schema::DataType type) {
    switch (type) {
        case schema::DataType::Boolean: return "b";
        case schema::DataType::Int8: return "c";
        case schema::DataType::Int16: return "s";
        case schema::DataType::Int32: return "i";
        case schema::DataType::Int64: return "l";
        case schema::DataType::Float: return "f";
        case schema::DataType::Double: return "g";
        case schema::DataType::TimestampNs: return "tsn:";
        default: return "u";
    }
}

void SchemaReleaseCallback(ArrowSchema* schema) {
    if (!schema || !schema->release) return;
    if (schema->children) {
        for (int64_t i = 0; i < schema->n_children; ++i) {
            if (schema->children[i]) {
                if (schema->children[i]->release) {
                    schema->children[i]->release(schema->children[i]);
                }
                delete schema->children[i];
            }
        }
        delete[] schema->children;
        schema->children = nullptr;
    }
    schema->release = nullptr;
}

void ArrayReleaseCallback(ArrowArray* array) {
    if (!array || !array->release) return;
    if (array->buffers) {
        delete[] array->buffers;
        array->buffers = nullptr;
    }
    if (array->children) {
        for (int64_t i = 0; i < array->n_children; ++i) {
            if (array->children[i]) {
                if (array->children[i]->release) {
                    array->children[i]->release(array->children[i]);
                }
                delete array->children[i];
            }
        }
        delete[] array->children;
        array->children = nullptr;
    }
    array->release = nullptr;
}

} // namespace

Status DuckDBArrowBridge::ExportSchema(
    const schema::Schema& schema,
    ArrowSchema* out_schema) {
    if (!out_schema) {
        return Status(StatusCode::InvalidArgument, "Null destination ArrowSchema pointer");
    }

    out_schema->format = "+s"; // Struct root representing a record batch
    out_schema->name = "";
    out_schema->metadata = nullptr;
    out_schema->flags = 0;
    out_schema->n_children = static_cast<int64_t>(schema.num_fields());
    out_schema->dictionary = nullptr;
    out_schema->private_data = nullptr;
    out_schema->release = &SchemaReleaseCallback;

    out_schema->children = new ArrowSchema*[out_schema->n_children];

    for (int64_t i = 0; i < out_schema->n_children; ++i) {
        const auto& f = schema.fields()[static_cast<size_t>(i)];
        auto* child = new ArrowSchema();
        child->format = MapArrowTypeFormat(f.type());
        child->name = f.name().c_str();
        child->metadata = nullptr;
        child->flags = f.nullable() ? 2 : 0; // ARROW_FLAG_NULLABLE = 2
        child->n_children = 0;
        child->children = nullptr;
        child->dictionary = nullptr;
        child->private_data = nullptr;
        child->release = &SchemaReleaseCallback;

        out_schema->children[i] = child;
    }

    return Status::OK();
}

Status DuckDBArrowBridge::ExportBatch(
    const arrow::ColumnarBatch& batch,
    ArrowArray* out_array) {
    if (!out_array) {
        return Status(StatusCode::InvalidArgument, "Null destination ArrowArray pointer");
    }

    size_t num_cols = batch.num_columns();
    int64_t num_rows = static_cast<int64_t>(batch.num_rows());

    out_array->length = num_rows;
    out_array->null_count = 0;
    out_array->offset = 0;
    out_array->n_buffers = 1;
    out_array->n_children = static_cast<int64_t>(num_cols);
    out_array->dictionary = nullptr;
    out_array->private_data = nullptr;
    out_array->release = &ArrayReleaseCallback;

    out_array->buffers = new const void*[1];
    out_array->buffers[0] = nullptr; // Root struct has no validity bitmap

    out_array->children = new ArrowArray*[out_array->n_children];

    for (size_t i = 0; i < num_cols; ++i) {
        auto col = batch.column(i);
        auto* child = new ArrowArray();
        child->length = num_rows;
        child->null_count = 0;
        child->offset = 0;
        child->n_buffers = 2; // [0] = null bitmap, [1] = data buffer
        child->n_children = 0;
        child->children = nullptr;
        child->dictionary = nullptr;
        child->private_data = nullptr;
        child->release = &ArrayReleaseCallback;

        child->buffers = new const void*[2];
        child->buffers[0] = nullptr; // Non-null fast path
        child->buffers[1] = col->raw_data(); // Zero-copy raw pointer!

        out_array->children[i] = child;
    }

    return Status::OK();
}

void DuckDBArrowBridge::ReleaseSchema(ArrowSchema* schema) {
    if (schema && schema->release) {
        schema->release(schema);
    }
}

void DuckDBArrowBridge::ReleaseArray(ArrowArray* array) {
    if (array && array->release) {
        array->release(array);
    }
}

StatusOr<AnalyticalQueryResult> DuckDBAnalyticalEngine::ExecuteFilter(
    const std::shared_ptr<arrow::ColumnarBatch>& batch,
    const std::string& column_name,
    const std::function<bool(double)>& predicate) {

    if (!batch) {
        return Status(StatusCode::InvalidArgument, "Null input ColumnarBatch");
    }

    int col_idx = batch->schema().GetFieldIndex(column_name);
    if (col_idx < 0) {
        return Status(StatusCode::NotFound, "Target filter column not found in schema: " + column_name);
    }

    auto target_col = batch->GetTypedColumn<double>(static_cast<size_t>(col_idx));
    size_t total_rows = batch->num_rows();

    std::vector<size_t> matched_indices;
    matched_indices.reserve(total_rows);

    for (size_t i = 0; i < total_rows; ++i) {
        if (!target_col->is_null(i) && predicate((*target_col)[i])) {
            matched_indices.push_back(i);
        }
    }

    AnalyticalQueryResult result;
    result.query = "FILTER " + column_name + " MATCH";
    result.rows_scanned = total_rows;
    result.rows_matched = matched_indices.size();

    // If all matched, return batch as is
    if (matched_indices.size() == total_rows) {
        result.result_batch = batch;
        return result;
    }

    // Build filtered batch
    std::vector<std::shared_ptr<arrow::IColumnVector>> filtered_cols;
    filtered_cols.reserve(batch->num_columns());

    for (size_t c = 0; c < batch->num_columns(); ++c) {
        auto col = batch->column(c);
        switch (col->type()) {
            case schema::DataType::Double: {
                auto tc = batch->GetTypedColumn<double>(c);
                std::vector<double> vals;
                vals.reserve(matched_indices.size());
                for (size_t idx : matched_indices) {
                    vals.push_back((*tc)[idx]);
                }
                filtered_cols.push_back(std::make_shared<arrow::ColumnVector<double>>(
                    schema::DataType::Double, vals.data(), vals.size()));
                break;
            }
            case schema::DataType::Int64:
            case schema::DataType::TimestampNs: {
                auto tc = batch->GetTypedColumn<int64_t>(c);
                std::vector<int64_t> vals;
                vals.reserve(matched_indices.size());
                for (size_t idx : matched_indices) {
                    vals.push_back((*tc)[idx]);
                }
                filtered_cols.push_back(std::make_shared<arrow::ColumnVector<int64_t>>(
                    col->type(), vals.data(), vals.size()));
                break;
            }
            case schema::DataType::Int32: {
                auto tc = batch->GetTypedColumn<int32_t>(c);
                std::vector<int32_t> vals;
                vals.reserve(matched_indices.size());
                for (size_t idx : matched_indices) {
                    vals.push_back((*tc)[idx]);
                }
                filtered_cols.push_back(std::make_shared<arrow::ColumnVector<int32_t>>(
                    schema::DataType::Int32, vals.data(), vals.size()));
                break;
            }
            default:
                break;
        }
    }

    result.result_batch = std::make_shared<arrow::ColumnarBatch>(
        batch->schema(), std::move(filtered_cols), matched_indices.size());

    return result;
}

StatusOr<std::vector<std::shared_ptr<arrow::ColumnarBatch>>> DuckDBAnalyticalEngine::ExecuteLakehouseScan(
    const std::shared_ptr<lakehouse::LakehouseTable>& table,
    const std::vector<lakehouse::ColumnPredicate>& predicates) {

    if (!table) {
        return Status(StatusCode::InvalidArgument, "Null LakehouseTable");
    }

    auto scan_files_or = table->Scan(predicates);
    if (!scan_files_or.ok()) return scan_files_or.status();

    std::vector<std::shared_ptr<arrow::ColumnarBatch>> batches;
    batches.reserve(scan_files_or.value().size());

    for (const auto& df : scan_files_or.value()) {
        auto reader_or = parquet::ColumnarFileReader::Open(df.path);
        if (!reader_or.ok()) continue;

        auto batch_or = reader_or.value()->ReadAll();
        if (batch_or.ok()) {
            batches.push_back(batch_or.value());
        }
    }

    return batches;
}

} // namespace duckdb_engine
} // namespace storage
} // namespace obsidian
