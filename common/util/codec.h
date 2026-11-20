// codec.h — utilitas byte lintas-platform: endian little-endian, hex, base32,
// perbandingan constant-time. Bagian dari ldcore (murni C++, tanpa OS API).
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <array>

namespace ldr::util {

inline void PutU16LE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
inline void PutU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
inline void PutU64LE(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}
inline uint16_t GetU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t GetU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t GetU64LE(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

inline void AppendU8(std::vector<uint8_t>& v, uint8_t b) { v.push_back(b); }
inline void AppendU16(std::vector<uint8_t>& v, uint16_t x) {
    uint8_t t[2]; PutU16LE(t, x); v.insert(v.end(), t, t + 2);
}
inline void AppendU32(std::vector<uint8_t>& v, uint32_t x) {
    uint8_t t[4]; PutU32LE(t, x); v.insert(v.end(), t, t + 4);
}
inline void AppendU64(std::vector<uint8_t>& v, uint64_t x) {
    uint8_t t[8]; PutU64LE(t, x); v.insert(v.end(), t, t + 8);
}
inline void AppendBytes(std::vector<uint8_t>& v, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    v.insert(v.end(), b, b + n);
}

inline std::string HexEncode(const uint8_t* d, size_t n, bool spaces = false) {
    static const char* kH = "0123456789abcdef";
    std::string out;
    out.reserve(n * (spaces ? 3 : 2));
    for (size_t i = 0; i < n; ++i) {
        if (i && spaces) out.push_back(' ');
        out.push_back(kH[d[i] >> 4]);
        out.push_back(kH[d[i] & 0xF]);
    }
    return out;
}

// RFC 4648 base32 tanpa padding ("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567").
inline std::string Base32Encode(const uint8_t* d, size_t n) {
    static const char* kA = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string out;
    out.reserve(((n * 8) + 4) / 5);
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        buf = (buf << 8) | d[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.push_back(kA[(buf >> bits) & 0x1F]);
        }
    }
    if (bits > 0) out.push_back(kA[(buf << (5 - bits)) & 0x1F]);
    return out;
}

// Perbandingan constant-time (tidak mengungkap posisi mismatch lewat timing).
inline bool ConstTimeEqual(const uint8_t* a, const uint8_t* b, size_t n) {
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

} // namespace ldr::util
