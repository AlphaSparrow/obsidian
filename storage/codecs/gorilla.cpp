#include "storage/codecs/gorilla.hpp"
#include <cmath>
#include <cstring>
#include <bit>

namespace obsidian {
namespace storage {
namespace codecs {

void BitWriter::WriteBit(uint8_t bit) {
    current_byte_ = static_cast<uint8_t>((current_byte_ << 1) | (bit & 1));
    bits_in_current_++;
    total_bits_++;

    if (bits_in_current_ == 8) {
        buffer_.push_back(current_byte_);
        current_byte_ = 0;
        bits_in_current_ = 0;
    }
}

void BitWriter::WriteBits(uint64_t value, uint32_t num_bits) {
    for (int32_t i = static_cast<int32_t>(num_bits) - 1; i >= 0; --i) {
        WriteBit(static_cast<uint8_t>((value >> i) & 1));
    }
}

void BitWriter::Flush() {
    if (bits_in_current_ > 0) {
        current_byte_ = static_cast<uint8_t>(current_byte_ << (8 - bits_in_current_));
        buffer_.push_back(current_byte_);
        current_byte_ = 0;
        bits_in_current_ = 0;
    }
}

BitReader::BitReader(std::span<const uint8_t> buffer)
    : buffer_(buffer) {}

StatusOr<uint8_t> BitReader::ReadBit() {
    if (bits_left_in_byte_ == 0) {
        if (byte_index_ >= buffer_.size()) {
            return Status::Corruption("Gorilla bitstream corrupted: premature end-of-stream while reading bit");
        }
        current_byte_ = buffer_[byte_index_++];
        bits_left_in_byte_ = 8;
    }

    bits_left_in_byte_--;
    uint8_t bit = static_cast<uint8_t>((current_byte_ >> bits_left_in_byte_) & 1);
    return bit;
}

StatusOr<uint64_t> BitReader::ReadBits(uint32_t num_bits) {
    if (num_bits > 64) {
        return Status::InvalidArgument("Requested bit count exceeds 64 bits");
    }
    uint64_t result = 0;
    for (uint32_t i = 0; i < num_bits; ++i) {
        auto bit_or = ReadBit();
        if (!bit_or.ok()) {
            return bit_or.status();
        }
        result = (result << 1) | bit_or.value();
    }
    return result;
}

bool BitReader::HasMoreBits() const noexcept {
    return bits_left_in_byte_ > 0 || byte_index_ < buffer_.size();
}

size_t BitReader::BitsRemaining() const noexcept {
    size_t remaining_bytes = (byte_index_ < buffer_.size()) ? (buffer_.size() - byte_index_) : 0;
    return bits_left_in_byte_ + (remaining_bytes * 8);
}

StatusOr<std::vector<uint8_t>> GorillaCodec::Compress(std::span<const double> values) {
    if (values.empty()) {
        return std::vector<uint8_t>{};
    }

    BitWriter writer;

    // First value written verbatim as 64-bit raw representation
    double first_val = values[0];
    if (std::isnan(first_val) || std::isinf(first_val)) {
        return Status::Corruption("Gorilla encoder error: NaN or Infinity floating-point anomaly detected at index 0");
    }

    uint64_t prev_bits = std::bit_cast<uint64_t>(first_val);
    writer.WriteBits(prev_bits, 64);

    uint32_t prev_leading = 0xFFFFFFFF;
    uint32_t prev_trailing = 0xFFFFFFFF;

    for (size_t i = 1; i < values.size(); ++i) {
        double val = values[i];
        if (std::isnan(val) || std::isinf(val)) {
            return Status::Corruption("Gorilla encoder error: NaN or Infinity floating-point anomaly detected at index " + std::to_string(i));
        }

        uint64_t cur_bits = std::bit_cast<uint64_t>(val);
        uint64_t xor_diff = cur_bits ^ prev_bits;

        if (xor_diff == 0) {
            // Case 0: identical value, write bit 0
            writer.WriteBit(0);
        } else {
            // Case 1: non-zero XOR, write bit 1
            writer.WriteBit(1);

            uint32_t leading = static_cast<uint32_t>(std::countl_zero(xor_diff));
            uint32_t trailing = static_cast<uint32_t>(std::countr_zero(xor_diff));

            // Clamp leading zeros to 31 (5 bits) for Gorilla header efficiency
            if (leading > 31) {
                leading = 31;
            }

            if (prev_leading != 0xFFFFFFFF && leading >= prev_leading && trailing >= prev_trailing) {
                // Subcase: previous leading and trailing zeros can be reused
                writer.WriteBit(0);
                uint32_t meaningful_bits = 64 - prev_leading - prev_trailing;
                uint64_t payload = (xor_diff >> prev_trailing);
                writer.WriteBits(payload, meaningful_bits);
            } else {
                // Subcase: encode new leading and meaningful bit length
                writer.WriteBit(1);
                writer.WriteBits(leading, 5); // 5 bits for leading (0-31)
                uint32_t meaningful_bits = 64 - leading - trailing;
                writer.WriteBits(meaningful_bits, 6); // 6 bits for length (0-64)
                uint64_t payload = (xor_diff >> trailing);
                writer.WriteBits(payload, meaningful_bits);

                prev_leading = leading;
                prev_trailing = trailing;
            }
        }

        prev_bits = cur_bits;
    }

    writer.Flush();
    return writer.GetData();
}

Status GorillaCodec::Decompress(std::span<const uint8_t> input, size_t expected_count, std::vector<double>& output) {
    output.clear();
    if (expected_count == 0) {
        return Status::Ok();
    }

    if (input.empty()) {
        return Status::Corruption("Gorilla decompressor error: empty bitstream for non-empty expected count");
    }

    output.reserve(expected_count);
    BitReader reader(input);

    // Read first 64 bits verbatim
    auto first_bits_or = reader.ReadBits(64);
    if (!first_bits_or.ok()) {
        return first_bits_or.status();
    }

    uint64_t prev_bits = first_bits_or.value();
    double first_val = std::bit_cast<double>(prev_bits);
    if (std::isnan(first_val) || std::isinf(first_val)) {
        return Status::Corruption("Gorilla decompressor error: decoded NaN/Inf value in bitstream");
    }
    output.push_back(first_val);

    uint32_t prev_leading = 0xFFFFFFFF;
    uint32_t prev_trailing = 0xFFFFFFFF;

    for (size_t i = 1; i < expected_count; ++i) {
        auto bit_or = reader.ReadBit();
        if (!bit_or.ok()) {
            return bit_or.status();
        }

        if (bit_or.value() == 0) {
            // Identical to previous
            output.push_back(std::bit_cast<double>(prev_bits));
        } else {
            auto sub_bit_or = reader.ReadBit();
            if (!sub_bit_or.ok()) {
                return sub_bit_or.status();
            }

            uint32_t leading = 0;
            uint32_t meaningful_bits = 0;

            if (sub_bit_or.value() == 0) {
                // Reuse previous boundaries
                if (prev_leading == 0xFFFFFFFF) {
                    return Status::Corruption("Gorilla bitstream corrupted: attempted to reuse uninitialized leading/trailing bounds");
                }
                leading = prev_leading;
                meaningful_bits = 64 - prev_leading - prev_trailing;
            } else {
                // New boundaries
                auto lead_or = reader.ReadBits(5);
                if (!lead_or.ok()) {
                    return lead_or.status();
                }
                leading = static_cast<uint32_t>(lead_or.value());

                auto len_or = reader.ReadBits(6);
                if (!len_or.ok()) {
                    return len_or.status();
                }
                meaningful_bits = static_cast<uint32_t>(len_or.value());
                if (meaningful_bits == 0) {
                    meaningful_bits = 64; // 0 in 6 bits can represent 64
                }

                if (leading + meaningful_bits > 64) {
                    return Status::Corruption("Gorilla bitstream corrupted: leading (" + std::to_string(leading) + 
                                              ") + meaningful (" + std::to_string(meaningful_bits) + ") exceeds 64 bits");
                }

                prev_leading = leading;
                prev_trailing = 64 - leading - meaningful_bits;
            }

            auto payload_or = reader.ReadBits(meaningful_bits);
            if (!payload_or.ok()) {
                return payload_or.status();
            }

            uint64_t payload = payload_or.value();
            uint32_t trailing = 64 - leading - meaningful_bits;
            uint64_t xor_diff = (payload << trailing);
            uint64_t cur_bits = prev_bits ^ xor_diff;

            double decoded_val = std::bit_cast<double>(cur_bits);
            if (std::isnan(decoded_val) || std::isinf(decoded_val)) {
                return Status::Corruption("Gorilla bitstream corrupted: decoded NaN/Inf value at index " + std::to_string(i));
            }

            output.push_back(decoded_val);
            prev_bits = cur_bits;
        }
    }

    return Status::Ok();
}

GorillaCompressionStats GorillaCodec::GetStats(size_t value_count, size_t compressed_size_bytes) {
    GorillaCompressionStats stats;
    stats.values_encoded = value_count;
    stats.raw_bytes = value_count * sizeof(double);
    stats.compressed_bytes = compressed_size_bytes;
    stats.compression_ratio = stats.compressed_bytes > 0 
        ? static_cast<double>(stats.raw_bytes) / static_cast<double>(stats.compressed_bytes) 
        : 1.0;
    return stats;
}

} // namespace codecs
} // namespace storage
} // namespace obsidian
