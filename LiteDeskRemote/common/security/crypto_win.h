// crypto_win.h — wrapper CNG (BCrypt): SHA-256, HMAC-SHA-256, PBKDF2-HMAC-SHA-256.
// Alasan memakai CNG: komponen OS, nol dependensi, update keamanan ikut Windows.
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace ldr::sec {

using Bytes32 = std::array<uint8_t, 32>;

Bytes32 Sha256(const uint8_t* data, size_t len);
Bytes32 HmacSha256(const uint8_t* key, size_t keyLen,
                   const uint8_t* msg, size_t msgLen);
// OWASP: PBKDF2-HMAC-SHA-256 >= 210.000 iterasi (nilai default konfigurasi).
Bytes32 Pbkdf2Sha256(const uint8_t* pw, size_t pwLen,
                     const uint8_t* salt, size_t saltLen, uint32_t iterations);
bool RandomBytes(uint8_t* dst, size_t len);

} // namespace ldr::sec
