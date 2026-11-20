// crypto_portable.h — implementasi referensi SHA-256 / HMAC / PBKDF2 murni C++.
// HANYA dikompilasi pada build non-Windows (ldcore_tests di Linux/CI) untuk memungkinkan
// pengujian deterministik tanpa OS API. Build produksi Windows memakai CNG (crypto_win.cpp).
// Kedua implementasi divalidasi dengan vektor uji resmi (FIPS 180-4, RFC 4231, RFC 7914).
#pragma once
#if !defined(_WIN32)
#include <array>
#include <cstdint>
#include <cstddef>

namespace ldr::sec {

using Bytes32 = std::array<uint8_t, 32>;

Bytes32 Sha256(const uint8_t* data, size_t len);
Bytes32 HmacSha256(const uint8_t* key, size_t keyLen,
                   const uint8_t* msg, size_t msgLen);
Bytes32 Pbkdf2Sha256(const uint8_t* pw, size_t pwLen,
                     const uint8_t* salt, size_t saltLen, uint32_t iterations);
bool RandomBytes(uint8_t* dst, size_t len);

} // namespace ldr::sec
#endif // !defined(_WIN32)
