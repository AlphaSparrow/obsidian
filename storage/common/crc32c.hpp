#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace obsidian {
namespace storage {
namespace common {

// Hardware-standard CRC32C (Castagnoli, polynomial 0x82F63B78)
// Used across Apache Parquet, Arrow, and Lakehouse storage formats for bit-rot detection.
class CRC32C {
public:
    // Calculates CRC32C checksum over a contiguous memory buffer
    static uint32_t Calculate(const void* data, size_t length, uint32_t initial = 0);

    // Convenience overload for std::string
    static uint32_t Calculate(const std::string& str, uint32_t initial = 0) {
        return Calculate(str.data(), str.size(), initial);
    }
};

} // namespace common
} // namespace storage
} // namespace obsidian
