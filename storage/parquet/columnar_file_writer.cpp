#include "columnar_file_writer.hpp"
#include <algorithm>
#include <limits>
#include <cstring>

namespace obsidian {
namespace storage {
namespace parquet {

namespace {

template <typename T>
void WriteVal(std::string& buf, const T& val) {
    buf.append(reinterpret_cast<const char*>(&val), sizeof(T));
}

void WriteStr(std::string& buf, const std::string& s) {
    uint16_t len = static_cast<uint16_t>(s.size());
    WriteVal(buf, len);
    buf.append(s);
}

void AlignBuffer(std::string& buf, size_t alignment = 64) {
    size_t rem = buf.size() % alignment;
    if (rem != 0) {
        buf.append(alignment - rem, '\0');
    }
}

template <typename T>
void ComputeMinMax(
    const std::shared_ptr<arrow::IColumnVector>& col,
    std::string& out_min,
    std::string& out_max) {
    if (col->size() == 0) {
        out_min = "";
        out_max = "";
        return;
    }
    const T* data = static_cast<const T*>(col->raw_data());
    if (!data) return;

    T min_v = std::numeric_limits<T>::max();
    T max_v = std::numeric_limits<T>::lowest();
    bool found = false;

    for (size_t i = 0; i < col->size(); ++i) {
        if (col->is_null(i)) continue;
        min_v = std::min(min_v, data[i]);
        max_v = std::max(max_v, data[i]);
        found = true;
    }

    if (found) {
        out_min = std::to_string(min_v);
        out_max = std::to_string(max_v);
    } else {
        out_min = "";
        out_max = "";
    }
}

} // namespace

ColumnarFileWriter::ColumnarFileWriter(std::shared_ptr<IFileSystem> fs)
    : fs_(std::move(fs)) {
    if (!fs_) {
        fs_ = LocalFileSystem::Default();
    }
}

void ColumnarFileWriter::ExtractMinMax(
    const std::shared_ptr<arrow::IColumnVector>& col,
    std::string& out_min,
    std::string& out_max) {
    switch (col->type()) {
        case schema::DataType::Double:
            ComputeMinMax<double>(col, out_min, out_max);
            break;
        case schema::DataType::Float:
            ComputeMinMax<float>(col, out_min, out_max);
            break;
        case schema::DataType::Int64:
        case schema::DataType::TimestampNs:
            ComputeMinMax<int64_t>(col, out_min, out_max);
            break;
        case schema::DataType::Int32:
            ComputeMinMax<int32_t>(col, out_min, out_max);
            break;
        case schema::DataType::Int16:
            ComputeMinMax<int16_t>(col, out_min, out_max);
            break;
        case schema::DataType::Int8:
            ComputeMinMax<int8_t>(col, out_min, out_max);
            break;
        case schema::DataType::Boolean:
            ComputeMinMax<uint8_t>(col, out_min, out_max);
            break;
        default:
            out_min = "";
            out_max = "";
            break;
    }
}

StatusOr<lakehouse::DataFile> ColumnarFileWriter::Write(
    const std::string& file_path,
    const arrow::ColumnarBatch& batch,
    const std::vector<lakehouse::PartitionValue>& partitions) {

    size_t num_cols = batch.num_columns();
    size_t num_rows = batch.num_rows();
    const auto& schema = batch.schema();

    std::unordered_map<std::string, std::string> min_stats;
    std::unordered_map<std::string, std::string> max_stats;

    struct ColLayout {
        std::string name;
        schema::DataType type;
        bool nullable;
        std::string min_val;
        std::string max_val;
        uint64_t data_bytes;
        uint64_t null_count;
        uint64_t null_bitmap_bytes;
    };

    std::vector<ColLayout> layouts;
    layouts.reserve(num_cols);

    for (size_t i = 0; i < num_cols; ++i) {
        auto col = batch.column(i);
        const auto& field = schema.fields()[i];

        ColLayout lay{};
        lay.name = field.name();
        lay.type = field.type();
        lay.nullable = field.nullable();

        ExtractMinMax(col, lay.min_val, lay.max_val);
        if (!lay.min_val.empty()) {
            min_stats[lay.name] = lay.min_val;
            max_stats[lay.name] = lay.max_val;
        }

        size_t elem_size = schema::DataTypeSize(lay.type);
        lay.data_bytes = num_rows * elem_size;

        lay.null_count = 0;
        if (lay.nullable) {
            for (size_t r = 0; r < num_rows; ++r) {
                if (col->is_null(r)) lay.null_count++;
            }
            lay.null_bitmap_bytes = (num_rows + 7) / 8;
        } else {
            lay.null_bitmap_bytes = 0;
        }

        layouts.push_back(lay);
    }

    // Build the serialized binary buffer
    std::string buffer;
    buffer.reserve(sizeof(kColumnarMagic) + 64 + (num_rows * 64));

    // 1. Magic bytes & version
    buffer.append(reinterpret_cast<const char*>(kColumnarMagic), 4);
    WriteVal(buffer, kColumnarVersion);
    WriteVal(buffer, static_cast<uint64_t>(num_rows));
    WriteVal(buffer, static_cast<uint32_t>(num_cols));

    // Reserve offset for metadata table
    // Metadata entries: for each column:
    // [name_len (uint16)][name][type (uint8)][nullable (uint8)]
    // [min_len (uint16)][min][max_len (uint16)][max]
    // [data_offset (uint64)][data_bytes (uint64)]
    // [null_count (uint64)][null_bitmap_offset (uint64)][null_bitmap_bytes (uint64)]
    for (const auto& lay : layouts) {
        WriteStr(buffer, lay.name);
        WriteVal(buffer, static_cast<uint8_t>(lay.type));
        WriteVal(buffer, static_cast<uint8_t>(lay.nullable ? 1 : 0));
        WriteStr(buffer, lay.min_val);
        WriteStr(buffer, lay.max_val);

        // Placeholder offsets to be patched later
        WriteVal(buffer, static_cast<uint64_t>(0)); // data_offset placeholder
        WriteVal(buffer, lay.data_bytes);
        WriteVal(buffer, lay.null_count);
        WriteVal(buffer, static_cast<uint64_t>(0)); // null_bitmap_offset placeholder
        WriteVal(buffer, lay.null_bitmap_bytes);
    }

    // Record the byte positions where data_offset and null_bitmap_offset need patching
    struct PatchLocation {
        size_t data_offset_pos;
        size_t null_offset_pos;
    };
    std::vector<PatchLocation> patch_positions;
    patch_positions.reserve(num_cols);

    // Re-scan buffer to locate patch positions
    size_t scan_pos = 4 + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t);
    for (const auto& lay : layouts) {
        uint16_t nlen;
        std::memcpy(&nlen, buffer.data() + scan_pos, sizeof(uint16_t));
        scan_pos += sizeof(uint16_t) + nlen + 2; // name + type + nullable

        uint16_t min_len;
        std::memcpy(&min_len, buffer.data() + scan_pos, sizeof(uint16_t));
        scan_pos += sizeof(uint16_t) + min_len;

        uint16_t max_len;
        std::memcpy(&max_len, buffer.data() + scan_pos, sizeof(uint16_t));
        scan_pos += sizeof(uint16_t) + max_len;

        PatchLocation loc{};
        loc.data_offset_pos = scan_pos;
        scan_pos += sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint64_t); // data_offset + data_bytes + null_count
        loc.null_offset_pos = scan_pos;
        scan_pos += sizeof(uint64_t) + sizeof(uint64_t); // null_bitmap_offset + null_bitmap_bytes

        patch_positions.push_back(loc);
    }

    // 2. Write Column Chunks (64-byte cache line aligned)
    for (size_t i = 0; i < num_cols; ++i) {
        auto col = batch.column(i);
        const auto& lay = layouts[i];

        AlignBuffer(buffer, 64);
        uint64_t data_offset = static_cast<uint64_t>(buffer.size());

        // Write raw column payload
        if (lay.data_bytes > 0 && col->raw_data()) {
            buffer.append(static_cast<const char*>(col->raw_data()), lay.data_bytes);
        }

        // Patch data_offset
        std::memcpy(buffer.data() + patch_positions[i].data_offset_pos, &data_offset, sizeof(uint64_t));

        // Write null bitmap if applicable
        uint64_t null_offset = 0;
        if (lay.null_bitmap_bytes > 0) {
            AlignBuffer(buffer, 8);
            null_offset = static_cast<uint64_t>(buffer.size());
            std::vector<uint8_t> bitmap(lay.null_bitmap_bytes, 0);
            for (size_t r = 0; r < num_rows; ++r) {
                if (!col->is_null(r)) {
                    bitmap[r / 8] |= static_cast<uint8_t>(1 << (r % 8));
                }
            }
            buffer.append(reinterpret_cast<const char*>(bitmap.data()), bitmap.size());
        }

        // Patch null_bitmap_offset
        std::memcpy(buffer.data() + patch_positions[i].null_offset_pos, &null_offset, sizeof(uint64_t));
    }

    // 3. Footer Magic
    AlignBuffer(buffer, 64);
    buffer.append(reinterpret_cast<const char*>(kColumnarMagic), 4);

    // Ensure parent directory exists
    size_t last_slash = file_path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        std::string parent_dir = file_path.substr(0, last_slash);
        auto create_st = fs_->CreateDirectories(parent_dir);
        if (!create_st.ok()) return create_st;
    }

    // Write file atomically
    auto write_st = fs_->WriteFileAtomic(file_path, buffer);
    if (!write_st.ok()) return write_st;

    lakehouse::DataFile df{};
    df.path = file_path;
    df.format = "parquet";
    df.file_size_bytes = static_cast<int64_t>(buffer.size());
    df.record_count = static_cast<int64_t>(num_rows);
    df.partitions = partitions;
    df.min_values = std::move(min_stats);
    df.max_values = std::move(max_stats);

    return df;
}

} // namespace parquet
} // namespace storage
} // namespace obsidian
