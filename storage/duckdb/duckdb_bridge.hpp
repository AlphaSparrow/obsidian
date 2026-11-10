#pragma once

#include "storage/arrow/columnar_batch.hpp"
#include "storage/lakehouse/lakehouse_table.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace obsidian {
namespace storage {
namespace duckdb_engine {

// Standard Apache Arrow C Data Interface ABI structs
// Compatible with DuckDB, Polars, and Apache Arrow C API
#ifndef ARROW_C_DATA_INTERFACE
#define ARROW_C_DATA_INTERFACE

struct ArrowSchema {
    const char* format;
    const char* name;
    const char* metadata;
    int64_t flags;
    int64_t n_children;
    struct ArrowSchema** children;
    struct ArrowSchema* dictionary;
    void (*release)(struct ArrowSchema*);
    void* private_data;
};

struct ArrowArray {
    int64_t length;
    int64_t null_count;
    int64_t offset;
    int64_t n_buffers;
    int64_t n_children;
    const void** buffers;
    struct ArrowArray** children;
    struct ArrowArray* dictionary;
    void (*release)(struct ArrowArray*);
    void* private_data;
};

#endif // ARROW_C_DATA_INTERFACE

class DuckDBArrowBridge {
public:
    // Exports Obsidian Schema to an ArrowSchema struct (C Data Interface)
    static Status ExportSchema(
        const schema::Schema& schema,
        ArrowSchema* out_schema);

    // Exports an aligned ColumnarBatch to an ArrowArray struct with zero copies
    static Status ExportBatch(
        const arrow::ColumnarBatch& batch,
        ArrowArray* out_array);

    // Releases resources allocated during Arrow C struct export
    static void ReleaseSchema(ArrowSchema* schema);
    static void ReleaseArray(ArrowArray* array);
};

struct AnalyticalQueryResult {
    std::string query;
    size_t rows_scanned{0};
    size_t rows_matched{0};
    std::shared_ptr<arrow::ColumnarBatch> result_batch;
};

// Embedded quantitative vector execution engine
class DuckDBAnalyticalEngine {
public:
    DuckDBAnalyticalEngine() = default;

    // Filters and evaluates columnar batches with zero-copy vectorised execution
    StatusOr<AnalyticalQueryResult> ExecuteFilter(
        const std::shared_ptr<arrow::ColumnarBatch>& batch,
        const std::string& column_name,
        const std::function<bool(double)>& predicate);

    // Evaluates range predicates on timestamps or prices across Lakehouse tables
    StatusOr<std::vector<std::shared_ptr<arrow::ColumnarBatch>>> ExecuteLakehouseScan(
        const std::shared_ptr<lakehouse::LakehouseTable>& table,
        const std::vector<lakehouse::ColumnPredicate>& predicates);
};

} // namespace duckdb_engine
} // namespace storage
} // namespace obsidian
