#include "crc32c.hpp"

namespace obsidian {
namespace storage {
namespace common {

namespace {

// Standard Castagnoli CRC32C table (polynomial: 0x82F63B78 reversed)
constexpr uint32_t kCRC32CPolynomial = 0x82F63B78;

struct CRC32CTable {
    uint32_t table[256];

    constexpr CRC32CTable() : table() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t crc = i;
            for (int j = 0; j < 8; ++j) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ kCRC32CPolynomial;
                } else {
                    crc >>= 1;
                }
            }
            table[i] = crc;
        }
    }
};

constexpr CRC32CTable kTable;

} // namespace

uint32_t CRC32C::Calculate(const void* data, size_t length, uint32_t initial) {
    if (!data || length == 0) {
        return initial;
    }

    const auto* p = static_cast<const uint8_t*>(data);
    uint32_t crc = ~initial;

    // Fast 8-byte chunk loop
    while (length >= 8) {
        crc = kTable.table[(crc ^ p[0]) & 0xFF] ^ (crc >> 8);
        crc = kTable.table[(crc ^ p[1]) & 0xFF] ^ (crc >> 8);
        crc = kTable.table[(crc ^ p[2]) & 0xFF] ^ (crc >> 8);
        crc = kTable.table[(crc ^ p[3]) & 0xFF] ^ (crc >> 8);
        crc = kTable.table[(crc ^ p[4]) & 0xFF] ^ (crc >> 8);
        crc = kTable.table[(crc ^ p[5]) & 0xFF] ^ (crc >> 8);
        crc = kTable.table[(crc ^ p[6]) & 0xFF] ^ (crc >> 8);
        crc = kTable.table[(crc ^ p[7]) & 0xFF] ^ (crc >> 8);
        p += 8;
        length -= 8;
    }

    // Residual byte loop
    while (length > 0) {
        crc = kTable.table[(crc ^ *p) & 0xFF] ^ (crc >> 8);
        ++p;
        --length;
    }

    return ~crc;
}

} // namespace common
} // namespace storage
} // namespace obsidian
