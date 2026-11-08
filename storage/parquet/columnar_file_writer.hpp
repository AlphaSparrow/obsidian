#pragma once

#include "storage/common/file_system.hpp"
#include "storage/common/status.hpp"
#include "storage/arrow/columnar_batch.hpp"
#include "storage/lakehouse/lakehouse_structures.hpp"
#include "storage/schema/market_data_schema.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace obsidian {
namespace storage {
namespace parquet {

// Magic bytes identifying an Obsidian Columnar File ("OBSD")
constexpr uint8_t kColumnarMagic[4] = {'O', 'B', 'S', 'D'};
constexpr uint32_t kColumnarVersion = 1;

class ColumnarFileWriter {
public:
    explicit ColumnarFileWriter(std::shared_ptr<IFileSystem> fs = LocalFileSystem::Default());

    // Writes an Arrow-compatible ColumnarBatch into a 64-byte aligned columnar file.
    // Automatically extracts min/max column statistics and returns a populated DataFile descriptor.
    StatusOr<lakehouse::DataFile> Write(
        const std::string& file_path,
        const arrow::ColumnarBatch& batch,
        const std::vector<lakehouse::PartitionValue>& partitions = {});

private:
    std::shared_ptr<IFileSystem> fs_;

    static void ExtractMinMax(
        const std::shared_ptr<arrow::IColumnVector>& col,
        std::string& out_min,
        std::string& out_max);
};

} // namespace parquet
} // namespace storage
} // namespace obsidian
