#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace obsidian {
namespace storage {
namespace parquet {

// Super basic Parquet structures based on Apache Parquet format

enum class Type {
    BOOLEAN = 0,
    INT32 = 1,
    INT64 = 2,
    INT96 = 3,
    FLOAT = 4,
    DOUBLE = 5,
    BYTE_ARRAY = 6,
    FIXED_LEN_BYTE_ARRAY = 7
};

enum class Encoding {
    PLAIN = 0,
    PLAIN_DICTIONARY = 2,
    RLE = 3,
    BIT_PACKED = 4
};

enum class CompressionCodec {
    UNCOMPRESSED = 0,
    SNAPPY = 1,
    GZIP = 2,
    LZO = 3,
    BROTLI = 4,
    LZ4 = 5,
    ZSTD = 6
};

struct SchemaElement {
    std::string name;
    Type type;
    int32_t type_length;
    int32_t repetition_type; // 0: REQUIRED, 1: OPTIONAL, 2: REPEATED
    int32_t num_children;
};

struct ColumnMetaData {
    Type type;
    std::vector<Encoding> encodings;
    std::vector<std::string> path_in_schema;
    CompressionCodec codec;
    int64_t num_values;
    int64_t total_uncompressed_size;
    int64_t total_compressed_size;
    int64_t data_page_offset;
    int64_t dictionary_page_offset;
};

struct ColumnChunk {
    std::string file_path;
    int64_t file_offset;
    ColumnMetaData meta_data;
};

struct RowGroup {
    std::vector<ColumnChunk> columns;
    int64_t total_byte_size;
    int64_t num_rows;
};

struct FileMetaData {
    int32_t version;
    std::vector<SchemaElement> schema;
    int64_t num_rows;
    std::vector<RowGroup> row_groups;
    std::string created_by;
};

} // namespace parquet
} // namespace storage
} // namespace obsidian
