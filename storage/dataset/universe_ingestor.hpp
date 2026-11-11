#pragma once

#include "storage/common/status.hpp"
#include "storage/dataset/dataset_importer.hpp"
#include "storage/lakehouse/lakehouse_table.hpp"
#include "storage/parquet/columnar_file_writer.hpp"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace obsidian {
namespace storage {
namespace dataset {

struct IngestedPartitionStats {
    std::string symbol;
    int32_t year{0};
    std::string file_path;
    size_t row_count{0};
    size_t file_size_bytes{0};
    int64_t min_timestamp_ns{0};
    int64_t max_timestamp_ns{0};
};

struct UniverseIngestionReport {
    size_t assets_processed{0};
    size_t total_rows_ingested{0};
    size_t total_partitions_created{0};
    size_t total_bytes_written{0};
    std::vector<IngestedPartitionStats> partitions;
};

class UniverseIngestor {
public:
    // Ingests multiple cryptocurrency CSV files into a partitioned Lakehouse directory structure
    // symbol=<COIN>/year=<YYYY>/data-001.obsd
    static StatusOr<UniverseIngestionReport> IngestFiles(
        const std::vector<std::string>& csv_files,
        const std::string& target_table_dir,
        bool quarantine_anomalies = true);

    // Ingests a single combined CSV master file containing multiple assets (e.g. combined_crypto_data.csv)
    static StatusOr<UniverseIngestionReport> IngestCombinedCsv(
        const std::string& combined_csv_path,
        const std::string& target_table_dir,
        bool quarantine_anomalies = true);
};

} // namespace dataset
} // namespace storage
} // namespace obsidian
