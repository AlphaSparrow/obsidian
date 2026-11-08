#pragma once

#include "storage/common/status.hpp"
#include "storage/arrow/columnar_batch.hpp"
#include "storage/schema/market_data_schema.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace obsidian {
namespace storage {
namespace dataset {

struct AnomalyMetrics {
    uint32_t negative_or_zero_price_count{0};
    uint32_t negative_volume_count{0};
    uint32_t high_less_than_low_count{0};
    uint32_t open_close_out_of_bounds_count{0};
    uint32_t nan_or_inf_count{0};
    uint32_t temporal_regression_count{0};

    [[nodiscard]] bool HasAnomalies() const noexcept {
        return (negative_or_zero_price_count + negative_volume_count +
                high_less_than_low_count + open_close_out_of_bounds_count +
                nan_or_inf_count + temporal_regression_count) > 0;
    }
};

struct IngestionResult {
    std::shared_ptr<arrow::ColumnarBatch> batch;
    size_t total_rows_read{0};
    size_t valid_rows{0};
    size_t quarantined_rows{0};
    AnomalyMetrics anomalies;
    int64_t min_timestamp_ns{0};
    int64_t max_timestamp_ns{0};
};

class DatasetImporter {
public:
    // Imports a historical cryptocurrency OHLCV CSV file (semicolon or comma-separated).
    // Performs strict financial error detection and anomaly quarantine.
    // Automatically orders output chronologically into an aligned ColumnarBatch with BarOHLCV schema.
    static StatusOr<IngestionResult> ImportCsv(
        const std::string& csv_path,
        int32_t symbol_id,
        int32_t resolution_ms = 86400000,
        bool quarantine_invalid_rows = true);

    // Ingests directly from an in-memory CSV string content
    static StatusOr<IngestionResult> ImportCsvContent(
        std::string_view csv_content,
        int32_t symbol_id,
        int32_t resolution_ms = 86400000,
        bool quarantine_invalid_rows = true);

    // Parses ISO-8601 timestamp string (e.g. "2026-10-31T00:00:00.000Z") to UTC nanoseconds since epoch
    static StatusOr<int64_t> ParseIso8601ToNs(std::string_view iso_str);
};

} // namespace dataset
} // namespace storage
} // namespace obsidian
