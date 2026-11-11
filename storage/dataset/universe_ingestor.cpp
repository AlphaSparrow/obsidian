#include "storage/dataset/universe_ingestor.hpp"
#include "storage/common/file_system.hpp"
#include "storage/parquet/columnar_file_writer.hpp"
#include <filesystem>
#include <fstream>
#include <map>

namespace obsidian {
namespace storage {
namespace dataset {

namespace fs = std::filesystem;

namespace {

int32_t ExtractYearFromEpochNs(int64_t epoch_ns) {
    int64_t total_sec = epoch_ns / 1000000000LL;
    int64_t days = total_sec / 86400LL;
    if (total_sec < 0 && (total_sec % 86400LL != 0)) {
        days -= 1;
    }

    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = static_cast<unsigned>(z - era * 146097);
    unsigned yoe = (doe - doe / 1024 + doe / 1461 - doe / 142441) / 365;
    int32_t year = static_cast<int32_t>(yoe) + static_cast<int32_t>(era * 400);
    return year;
}

std::string ExtractSymbolFromPath(const std::string& path) {
    fs::path p(path);
    std::string stem = p.stem().string();
    size_t underscore = stem.find('_');
    if (underscore != std::string::npos) {
        return stem.substr(0, underscore);
    }
    return stem;
}

} // namespace

StatusOr<UniverseIngestionReport> UniverseIngestor::IngestFiles(
    const std::vector<std::string>& csv_files,
    const std::string& target_table_dir,
    bool quarantine_anomalies) {
    auto local_fs = LocalFileSystem::Default();
    UniverseIngestionReport report;

    int32_t symbol_id_seq = 1;

    for (const auto& file : csv_files) {
        std::string symbol = ExtractSymbolFromPath(file);
        int32_t symbol_id = symbol_id_seq++;

        auto import_or = DatasetImporter::ImportCsv(file, symbol_id, 86400000, quarantine_anomalies);
        if (!import_or.ok()) {
            continue; // Skip failed file
        }

        auto& res = import_or.value();
        if (!res.batch || res.batch->RowCount() == 0) {
            continue;
        }

        report.assets_processed++;
        report.total_rows_ingested += res.batch->RowCount();

        // Extract timestamp column
        auto ts_col = std::dynamic_pointer_cast<arrow::ColumnVector<int64_t>>(res.batch->GetColumn(0));
        if (!ts_col) continue;

        // Group row indices by year
        std::map<int32_t, std::vector<size_t>> year_indices;
        for (size_t i = 0; i < res.batch->RowCount(); ++i) {
            int32_t year = ExtractYearFromEpochNs(ts_col->Value(i));
            year_indices[year].push_back(i);
        }

        // For each year partition, write a columnar file
        for (const auto& [year, indices] : year_indices) {
            std::string part_dir = target_table_dir + "/symbol=" + symbol + "/year=" + std::to_string(year);
            local_fs->CreateDirectories(part_dir);

            std::string file_path = part_dir + "/data-001.obsd";

            // Build partitioned sub-batch
            size_t part_rows = indices.size();
            auto schema = res.batch->GetSchema();
            std::vector<std::shared_ptr<arrow::IColumnVector>> part_cols;

            // TimestampNs (col 0)
            std::vector<int64_t> part_ts(part_rows);
            for (size_t i = 0; i < part_rows; ++i) part_ts[i] = ts_col->Value(indices[i]);
            part_cols.push_back(std::make_shared<arrow::ColumnVector<int64_t>>(schema::DataType::TimestampNs, part_ts.data(), part_rows));

            // Symbol ID (col 1)
            std::vector<int32_t> part_sym(part_rows, symbol_id);
            part_cols.push_back(std::make_shared<arrow::ColumnVector<int32_t>>(schema::DataType::Int32, part_sym.data(), part_rows));

            // Resolution Ms (col 2)
            std::vector<int32_t> part_res(part_rows, 86400000);
            part_cols.push_back(std::make_shared<arrow::ColumnVector<int32_t>>(schema::DataType::Int32, part_res.data(), part_rows));

            // Open, High, Low, Close, Volume, VWAP (cols 3..8)
            for (size_t c = 3; c <= 8; ++c) {
                auto dbl_col = std::dynamic_pointer_cast<arrow::ColumnVector<double>>(res.batch->GetColumn(c));
                std::vector<double> vals(part_rows, 0.0);
                if (dbl_col) {
                    for (size_t i = 0; i < part_rows; ++i) vals[i] = dbl_col->Value(indices[i]);
                }
                part_cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, vals.data(), part_rows));
            }

            // Tick count (col 9)
            std::vector<int64_t> part_ticks(part_rows, 1);
            part_cols.push_back(std::make_shared<arrow::ColumnVector<int64_t>>(schema::DataType::Int64, part_ticks.data(), part_rows));

            // Buy Vol, Sell Vol (cols 10, 11)
            for (size_t c = 10; c <= 11; ++c) {
                auto dbl_col = std::dynamic_pointer_cast<arrow::ColumnVector<double>>(res.batch->GetColumn(c));
                std::vector<double> vals(part_rows, 0.0);
                if (dbl_col) {
                    for (size_t i = 0; i < part_rows; ++i) vals[i] = dbl_col->Value(indices[i]);
                }
                part_cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, vals.data(), part_rows));
            }

            arrow::ColumnarBatch part_batch(schema, std::move(part_cols), part_rows);

            auto write_or = parquet::ColumnarFileWriter::WriteBatch(file_path, part_batch);
            if (write_or.ok()) {
                const auto& df = write_or.value();
                report.total_partitions_created++;
                report.total_bytes_written += df.file_size_bytes;

                IngestedPartitionStats stats;
                stats.symbol = symbol;
                stats.year = year;
                stats.file_path = file_path;
                stats.row_count = part_rows;
                stats.file_size_bytes = df.file_size_bytes;
                stats.min_timestamp_ns = part_ts.front();
                stats.max_timestamp_ns = part_ts.back();
                report.partitions.push_back(std::move(stats));
            }
        }
    }

    return report;
}

StatusOr<UniverseIngestionReport> UniverseIngestor::IngestCombinedCsv(
    const std::string& combined_csv_path,
    const std::string& target_table_dir,
    bool quarantine_anomalies) {
    return IngestFiles({combined_csv_path}, target_table_dir, quarantine_anomalies);
}

} // namespace dataset
} // namespace storage
} // namespace obsidian
