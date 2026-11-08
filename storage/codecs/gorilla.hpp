#pragma once

#include "storage/common/status.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>

namespace obsidian {
namespace storage {
namespace codecs {

// BitWriter packs arbitrary bits into a contiguous byte buffer
class BitWriter {
public:
    BitWriter() = default;

    void WriteBit(uint8_t bit);
    void WriteBits(uint64_t value, uint32_t num_bits);
    void Flush();

    [[nodiscard]] const std::vector<uint8_t>& GetData() const noexcept { return buffer_; }
    [[nodiscard]] size_t BitCount() const noexcept { return total_bits_; }

private:
    std::vector<uint8_t> buffer_;
    uint8_t current_byte_{0};
    uint8_t bits_in_current_{0};
    size_t total_bits_{0};
};

// BitReader extracts arbitrary bits from a contiguous byte buffer with bounds detection
class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> buffer);

    StatusOr<uint8_t> ReadBit();
    StatusOr<uint64_t> ReadBits(uint32_t num_bits);
    [[nodiscard]] bool HasMoreBits() const noexcept;
    [[nodiscard]] size_t BitsRemaining() const noexcept;

private:
    std::span<const uint8_t> buffer_;
    size_t byte_index_{0};
    uint8_t bits_left_in_byte_{0};
    uint8_t current_byte_{0};
};

struct GorillaCompressionStats {
    size_t raw_bytes{0};
    size_t compressed_bytes{0};
    double compression_ratio{0.0};
    size_t values_encoded{0};
};

class GorillaCodec {
public:
    // Compress an array of 64-bit IEEE double floating-point numbers
    // Rejects NaN/Inf with Status::Corruption to prevent poisoned upstream analytical calculations
    static StatusOr<std::vector<uint8_t>> Compress(std::span<const double> values);

    // Decompress into output vector of doubles with strict bounds and bitstream corruption checks
    static Status Decompress(std::span<const uint8_t> input, size_t expected_count, std::vector<double>& output);

    // Convenience calculation of compression stats
    static GorillaCompressionStats GetStats(size_t value_count, size_t compressed_size_bytes);
};

} // namespace codecs
} // namespace storage
} // namespace obsidian
