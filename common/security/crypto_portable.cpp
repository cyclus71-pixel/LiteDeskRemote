// crypto_portable.cpp — lihat crypto_portable.h. SHA-256 sesuai FIPS 180-4,
// HMAC sesuai RFC 2104, PBKDF2 sesuai RFC 2898. Divalidasi oleh vektor uji resmi
// pada tests/core/test_core.cpp.
#if !defined(_WIN32)
#include "crypto_portable.h"
#include <cstring>
#include <random>
#include <vector>

namespace ldr::sec {
namespace {

struct Sha256Ctx {
    uint32_t h[8];
    uint64_t total = 0;
    uint8_t buf[64];
    size_t bufLen = 0;
};

const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t RoR(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void ShaInit(Sha256Ctx& c) {
    c.h[0] = 0x6a09e667; c.h[1] = 0xbb67ae85; c.h[2] = 0x3c6ef372; c.h[3] = 0xa54ff53a;
    c.h[4] = 0x510e527f; c.h[5] = 0x9b05688c; c.h[6] = 0x1f83d9ab; c.h[7] = 0x5be0cd19;
    c.total = 0;
    c.bufLen = 0;
}

void ShaBlock(Sha256Ctx& c, const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 |
               (uint32_t)p[i*4+2] << 8 | (uint32_t)p[i*4+3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = RoR(w[i-15], 7) ^ RoR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = RoR(w[i-2], 17) ^ RoR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3];
    uint32_t e = c.h[4], f = c.h[5], g = c.h[6], h = c.h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = RoR(e, 6) ^ RoR(e, 11) ^ RoR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = RoR(a, 2) ^ RoR(a, 13) ^ RoR(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c.h[0] += a; c.h[1] += b; c.h[2] += cc; c.h[3] += d;
    c.h[4] += e; c.h[5] += f; c.h[6] += g; c.h[7] += h;
}

void ShaUpdate(Sha256Ctx& c, const uint8_t* data, size_t len) {
    c.total += len;
    while (len > 0) {
        size_t take = 64 - c.bufLen;
        if (take > len) take = len;
        std::memcpy(c.buf + c.bufLen, data, take);
        c.bufLen += take;
        data += take;
        len -= take;
        if (c.bufLen == 64) {
            ShaBlock(c, c.buf);
            c.bufLen = 0;
        }
    }
}

void ShaFinal(Sha256Ctx& c, uint8_t out[32]) {
    uint64_t bits = c.total * 8;
    uint8_t pad = 0x80;
    ShaUpdate(c, &pad, 1);
    uint8_t zero = 0;
    while (c.bufLen != 56) ShaUpdate(c, &zero, 1);
    uint8_t lenBuf[8];
    for (int i = 0; i < 8; ++i) lenBuf[i] = static_cast<uint8_t>((bits >> (56 - 8 * i)) & 0xFF);
    std::memcpy(c.buf + 56, lenBuf, 8);
    ShaBlock(c, c.buf);
    c.bufLen = 0;
    for (int i = 0; i < 8; ++i) {
        out[i*4]     = static_cast<uint8_t>(c.h[i] >> 24);
        out[i*4 + 1] = static_cast<uint8_t>(c.h[i] >> 16);
        out[i*4 + 2] = static_cast<uint8_t>(c.h[i] >> 8);
        out[i*4 + 3] = static_cast<uint8_t>(c.h[i]);
    }
}

} // namespace

Bytes32 Sha256(const uint8_t* data, size_t len) {
    Sha256Ctx c;
    ShaInit(c);
    ShaUpdate(c, data, len);
    Bytes32 out;
    ShaFinal(c, out.data());
    return out;
}

Bytes32 HmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen) {
    uint8_t k[64];
    std::memset(k, 0, sizeof(k));
    if (keyLen > 64) {
        auto hk = Sha256(key, keyLen);
        std::memcpy(k, hk.data(), 32);
    } else {
        std::memcpy(k, key, keyLen);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    Sha256Ctx inner;
    ShaInit(inner);
    ShaUpdate(inner, ipad, 64);
    ShaUpdate(inner, msg, msgLen);
    uint8_t innerHash[32];
    ShaFinal(inner, innerHash);

    Sha256Ctx outer;
    ShaInit(outer);
    ShaUpdate(outer, opad, 64);
    ShaUpdate(outer, innerHash, 32);
    Bytes32 out;
    ShaFinal(outer, out.data());
    return out;
}

Bytes32 Pbkdf2Sha256(const uint8_t* pw, size_t pwLen, const uint8_t* salt,
                     size_t saltLen, uint32_t iterations) {
    // RFC 2898: DK = T1 untuk 32 byte = PRF(P, S || INT(1)) XOR PRF(P, U1) XOR ...
    // PRF = HMAC-SHA256 dengan kunci = password.
    Bytes32 acc{};
    uint8_t u[32];

    // U1 = PRF(P, S || INT(1))
    std::vector<uint8_t> msg;
    msg.reserve(saltLen + 4);
    msg.insert(msg.end(), salt, salt + saltLen);
    msg.push_back(0);
    msg.push_back(0);
    msg.push_back(0);
    msg.push_back(1);
    auto first = HmacSha256(pw, pwLen, msg.data(), msg.size());
    std::memcpy(u, first.data(), 32);
    for (int j = 0; j < 32; ++j) acc[j] ^= u[j];

    // Uc = PRF(P, U(c-1))
    for (uint32_t i = 1; i < iterations; ++i) {
        auto next = HmacSha256(pw, pwLen, u, 32);
        std::memcpy(u, next.data(), 32);
        for (int j = 0; j < 32; ++j) acc[j] ^= u[j];
    }
    return acc;
}

bool RandomBytes(uint8_t* dst, size_t len) {
    // Untuk test runner non-Windows; bukan jalur produksi.
    std::random_device rd;
    for (size_t i = 0; i < len; ++i) dst[i] = static_cast<uint8_t>(rd() & 0xFF);
    return true;
}

} // namespace ldr::sec
#endif // !defined(_WIN32)
