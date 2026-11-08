#pragma once

#include "storage/common/status.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>

namespace obsidian {
namespace storage {
namespace codecs {

struct DeltaDeltaCompressionStats {
    size_t raw_bytes{0};
    size_t compressed_bytes{0};
    double compression_ratio{0.0};
    size_t values_encoded{0};
};

class DeltaDeltaCodec {
public:
    // Compresses 64-bit timestamps (e.g. nanoseconds since epoch) or sequence numbers.
    // Checks for temporal regression: if allow_regression is false and timestamp decreases,
    // returns Status::Corruption with anomaly details.
    static StatusOr<std::vector<uint8_t>> CompressTimestamps(
        std::span<const int64_t> timestamps,
        bool allow_regression = false);

    // Decompresses timestamp bitstream back into int64_t sequence
    // Detects bitstream truncation and integer overflow/underflow during delta accumulation.
    static Status DecompressTimestamps(
        std::span<const uint8_t> input,
        size_t expected_count,
        std::vector<int64_t>& output);

    // Compresses strictly monotonic sequence IDs (e.g. trade/order sequence numbers).
    // Rejects non-monotonic or disordered IDs with Status::Corruption.
    static StatusOr<std::vector<uint8_t>> CompressMonotonicSequence(
        std::span<const int64_t> sequence_ids);

    // Decompresses sequence IDs
    static Status DecompressMonotonicSequence(
        std::span<const uint8_t> input,
        size_t expected_count,
        std::vector<int64_t>& output);

    static DeltaDeltaCompressionStats GetStats(size_t value_count, size_t compressed_size_bytes);
};

} // namespace codecs
} // namespace storage
} // namespace obsidian
