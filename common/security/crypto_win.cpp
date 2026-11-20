// crypto_win.cpp — implementasi CNG (BCrypt) dari crypto_win.h.
// Alasan CNG: bawaan Windows, nol dependensi, sertifikasi FIPS pada konfigurasi tertentu.
#include "crypto_win.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <memory>
#include <vector>

#pragma comment(lib, "bcrypt.lib") // MSVC; mingw/FindCMake juga link bcrypt eksplisit

namespace ldr::sec {

namespace {

struct AlgHandle {
    BCRYPT_ALG_HANDLE h = nullptr;
    ~AlgHandle() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
};

// SHA-256 polos
AlgHandle& ShaAlg() {
    static AlgHandle a = [] {
        AlgHandle x;
        BCryptOpenAlgorithmProvider(&x.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        return x;
    }();
    return a;
}

// SHA-256 dengan flag HMAC (untuk HMAC dan PBKDF2)
AlgHandle& HmacAlg() {
    static AlgHandle a = [] {
        AlgHandle x;
        BCryptOpenAlgorithmProvider(&x.h, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG);
        return x;
    }();
    return a;
}

struct HashCloser {
    void operator()(BCRYPT_HASH_HANDLE h) const { if (h) BCryptDestroyHash(h); }
};
using HashPtr = std::unique_ptr<void, HashCloser>;

HashPtr StartHash(BCRYPT_ALG_HANDLE alg, const uint8_t* key, size_t keyLen) {
    DWORD objSize = 0, cb = 0;
    if (FAILED(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                                 reinterpret_cast<PUCHAR>(&objSize), sizeof(objSize), &cb, 0)))
        return nullptr;
    std::vector<uint8_t> obj(objSize);
    BCRYPT_HASH_HANDLE h = nullptr;
    NTSTATUS st = key
        ? BCryptCreateHash(alg, &h, obj.data(), static_cast<ULONG>(obj.size()),
                           const_cast<PUCHAR>(key), static_cast<ULONG>(keyLen), 0)
        : BCryptCreateHash(alg, &h, obj.data(), static_cast<ULONG>(obj.size()),
                           nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(st)) return nullptr;
    return HashPtr(h);
}

} // namespace

Bytes32 Sha256(const uint8_t* data, size_t len) {
    Bytes32 out{};
    auto hash = StartHash(ShaAlg().h, nullptr, 0);
    if (!hash) return out;
    BCryptHashData(hash.get(), const_cast<PUCHAR>(data), static_cast<ULONG>(len), 0);
    BCryptFinishHash(hash.get(), out.data(), static_cast<ULONG>(out.size()), 0);
    return out;
}

Bytes32 HmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen) {
    Bytes32 out{};
    auto hash = StartHash(HmacAlg().h, key, keyLen);
    if (!hash) return out;
    BCryptHashData(hash.get(), const_cast<PUCHAR>(msg), static_cast<ULONG>(msgLen), 0);
    BCryptFinishHash(hash.get(), out.data(), static_cast<ULONG>(out.size()), 0);
    return out;
}

Bytes32 Pbkdf2Sha256(const uint8_t* pw, size_t pwLen, const uint8_t* salt,
                     size_t saltLen, uint32_t iterations) {
    Bytes32 out{};
    BCryptDeriveKeyPBKDF2(HmacAlg().h,
                          const_cast<PUCHAR>(pw), static_cast<ULONG>(pwLen),
                          const_cast<PUCHAR>(salt), static_cast<ULONG>(saltLen),
                          iterations, out.data(), static_cast<ULONG>(out.size()), 0);
    return out;
}

bool RandomBytes(uint8_t* dst, size_t len) {
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, dst, static_cast<ULONG>(len),
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

} // namespace ldr::sec
