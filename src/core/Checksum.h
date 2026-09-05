#pragma once
#include <cstdint>
#include <string>
namespace aof2 {
// IEEE CRC-32 detects accidental corruption; this is not authentication.
inline uint32_t crc32(const std::string& bytes) {
    uint32_t crc = 0xffffffffu;
    for (unsigned char c : bytes) {
        crc ^= c;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
}
