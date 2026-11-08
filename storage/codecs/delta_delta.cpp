#include "storage/codecs/delta_delta.hpp"
#include "storage/codecs/gorilla.hpp"
#include <limits>
#include <string>

namespace obsidian {
namespace storage {
namespace codecs {

namespace {

inline uint64_t ZigZagEncode64(int64_t n) {
    return static_cast<uint64_t>((n << 1) ^ (n >> 63));
}

inline int64_t ZigZagDecode64(uint64_t n) {
    return static_cast<int64_t>((n >> 1) ^ (~(n & 1) + 1));
}

} // namespace

StatusOr<std::vector<uint8_t>> DeltaDeltaCodec::CompressTimestamps(
    std::span<const int64_t> timestamps,
    bool allow_regression) {
    if (timestamps.empty()) {
        return std::vector<uint8_t>{};
    }

    BitWriter writer;

    // Write initial timestamp as 64-bit integer
    int64_t t0 = timestamps[0];
    writer.WriteBits(static_cast<uint64_t>(t0), 64);

    if (timestamps.size() == 1) {
        writer.Flush();
        return writer.GetData();
    }

    // Check second timestamp
    int64_t t1 = timestamps[1];
    if (!allow_regression && t1 < t0) {
        return Status::Corruption("Temporal regression detected at index 1: timestamp decreased from " + 
                                  std::to_string(t0) + " to " + std::to_string(t1));
    }

    int64_t prev_delta = t1 - t0;
    // Store first delta using 64-bit zigzag encoding
    writer.WriteBits(ZigZagEncode64(prev_delta), 64);

    int64_t prev_ts = t1;

    for (size_t i = 2; i < timestamps.size(); ++i) {
        int64_t cur_ts = timestamps[i];
        if (!allow_regression && cur_ts < prev_ts) {
            return Status::Corruption("Temporal regression detected at index " + std::to_string(i) + 
                                      ": timestamp decreased from " + std::to_string(prev_ts) + 
                                      " to " + std::to_string(cur_ts));
        }

        int64_t cur_delta = cur_ts - prev_ts;
        int64_t dod = cur_delta - prev_delta; // delta of delta

        if (dod == 0) {
            writer.WriteBit(0);
        } else if (dod >= -63 && dod <= 64) {
            // '10' + 7 bits (bias 63)
            writer.WriteBit(1);
            writer.WriteBit(0);
            writer.WriteBits(static_cast<uint64_t>(dod + 63), 7);
        } else if (dod >= -255 && dod <= 256) {
            // '110' + 9 bits (bias 255)
            writer.WriteBit(1);
            writer.WriteBit(1);
            writer.WriteBit(0);
            writer.WriteBits(static_cast<uint64_t>(dod + 255), 9);
        } else if (dod >= -2047 && dod <= 2048) {
            // '1110' + 12 bits (bias 2047)
            writer.WriteBit(1);
            writer.WriteBit(1);
            writer.WriteBit(1);
            writer.WriteBit(0);
            writer.WriteBits(static_cast<uint64_t>(dod + 2047), 12);
        } else {
            // '1111' + 64-bit zigzag dod
            writer.WriteBit(1);
            writer.WriteBit(1);
            writer.WriteBit(1);
            writer.WriteBit(1);
            writer.WriteBits(ZigZagEncode64(dod), 64);
        }

        prev_delta = cur_delta;
        prev_ts = cur_ts;
    }

    writer.Flush();
    return writer.GetData();
}

Status DeltaDeltaCodec::DecompressTimestamps(
    std::span<const uint8_t> input,
    size_t expected_count,
    std::vector<int64_t>& output) {
    output.clear();
    if (expected_count == 0) {
        return Status::Ok();
    }

    if (input.empty()) {
        return Status::Corruption("DeltaDelta decompressor error: input bitstream is empty");
    }

    output.reserve(expected_count);
    BitReader reader(input);

    auto t0_or = reader.ReadBits(64);
    if (!t0_or.ok()) {
        return t0_or.status();
    }
    int64_t cur_ts = static_cast<int64_t>(t0_or.value());
    output.push_back(cur_ts);

    if (expected_count == 1) {
        return Status::Ok();
    }

    auto d1_or = reader.ReadBits(64);
    if (!d1_or.ok()) {
        return d1_or.status();
    }
    int64_t prev_delta = ZigZagDecode64(d1_or.value());

    // Check overflow: cur_ts + prev_delta
    if ((prev_delta > 0 && cur_ts > std::numeric_limits<int64_t>::max() - prev_delta) ||
        (prev_delta < 0 && cur_ts < std::numeric_limits<int64_t>::min() - prev_delta)) {
        return Status::Corruption("Integer overflow detected during initial delta timestamp reconstruction");
    }

    cur_ts += prev_delta;
    output.push_back(cur_ts);

    for (size_t i = 2; i < expected_count; ++i) {
        auto bit_or = reader.ReadBit();
        if (!bit_or.ok()) {
            return bit_or.status();
        }

        int64_t dod = 0;
        if (bit_or.value() == 0) {
            dod = 0;
        } else {
            auto b2_or = reader.ReadBit();
            if (!b2_or.ok()) return b2_or.status();

            if (b2_or.value() == 0) {
                // '10' -> 7 bits
                auto val_or = reader.ReadBits(7);
                if (!val_or.ok()) return val_or.status();
                dod = static_cast<int64_t>(val_or.value()) - 63;
            } else {
                auto b3_or = reader.ReadBit();
                if (!b3_or.ok()) return b3_or.status();

                if (b3_or.value() == 0) {
                    // '110' -> 9 bits
                    auto val_or = reader.ReadBits(9);
                    if (!val_or.ok()) return val_or.status();
                    dod = static_cast<int64_t>(val_or.value()) - 255;
                } else {
                    auto b4_or = reader.ReadBit();
                    if (!b4_or.ok()) return b4_or.status();

                    if (b4_or.value() == 0) {
                        // '1110' -> 12 bits
                        auto val_or = reader.ReadBits(12);
                        if (!val_or.ok()) return val_or.status();
                        dod = static_cast<int64_t>(val_or.value()) - 2047;
                    } else {
                        // '1111' -> 64-bit zigzag
                        auto val_or = reader.ReadBits(64);
                        if (!val_or.ok()) return val_or.status();
                        dod = ZigZagDecode64(val_or.value());
                    }
                }
            }
        }

        // Check overflow for prev_delta + dod
        if ((dod > 0 && prev_delta > std::numeric_limits<int64_t>::max() - dod) ||
            (dod < 0 && prev_delta < std::numeric_limits<int64_t>::min() - dod)) {
            return Status::Corruption("Integer overflow detected in delta-of-delta accumulation at index " + std::to_string(i));
        }

        int64_t cur_delta = prev_delta + dod;

        // Check overflow for cur_ts + cur_delta
        if ((cur_delta > 0 && cur_ts > std::numeric_limits<int64_t>::max() - cur_delta) ||
            (cur_delta < 0 && cur_ts < std::numeric_limits<int64_t>::min() - cur_delta)) {
            return Status::Corruption("Integer overflow detected in timestamp accumulation at index " + std::to_string(i));
        }

        cur_ts += cur_delta;
        output.push_back(cur_ts);
        prev_delta = cur_delta;
    }

    return Status::Ok();
}

StatusOr<std::vector<uint8_t>> DeltaDeltaCodec::CompressMonotonicSequence(
    std::span<const int64_t> sequence_ids) {
    if (sequence_ids.empty()) {
        return std::vector<uint8_t>{};
    }

    for (size_t i = 1; i < sequence_ids.size(); ++i) {
        if (sequence_ids[i] <= sequence_ids[i - 1]) {
            return Status::Corruption("Monotonic sequence violation at index " + std::to_string(i) + 
                                      ": current ID " + std::to_string(sequence_ids[i]) + 
                                      " <= previous ID " + std::to_string(sequence_ids[i - 1]));
        }
    }

    return CompressTimestamps(sequence_ids, /*allow_regression=*/false);
}

Status DeltaDeltaCodec::DecompressMonotonicSequence(
    std::span<const uint8_t> input,
    size_t expected_count,
    std::vector<int64_t>& output) {
    auto status = DecompressTimestamps(input, expected_count, output);
    if (!status.ok()) {
        return status;
    }

    for (size_t i = 1; i < output.size(); ++i) {
        if (output[i] <= output[i - 1]) {
            return Status::Corruption("Decompressed sequence invariant violated: non-monotonic IDs detected");
        }
    }

    return Status::Ok();
}

DeltaDeltaCompressionStats DeltaDeltaCodec::GetStats(size_t value_count, size_t compressed_size_bytes) {
    DeltaDeltaCompressionStats stats;
    stats.values_encoded = value_count;
    stats.raw_bytes = value_count * sizeof(int64_t);
    stats.compressed_bytes = compressed_size_bytes;
    stats.compression_ratio = stats.compressed_bytes > 0 
        ? static_cast<double>(stats.raw_bytes) / static_cast<double>(stats.compressed_bytes) 
        : 1.0;
    return stats;
}

} // namespace codecs
} // namespace storage
} // namespace obsidian
