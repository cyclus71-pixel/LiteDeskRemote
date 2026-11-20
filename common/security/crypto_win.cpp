// crypto_win.cpp — implementasi CNG (BCrypt) dari crypto_win.h.
// Alasan CNG: bawaan Windows, nol dependensi, sertifikasi FIPS pada konfigurasi tertentu.
#include "crypto_win.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <cstring>
#include <memory>
#include <vector>

#pragma comment(lib, "bcrypt.lib") // MSVC; mingw/FindCMake juga link bcrypt eksplisit

namespace ldr::sec {

namespace {

struct AlgHandle {
    BCRYPT_ALG_HANDLE h = nullptr;
    NTSTATUS status = -1;
    ~AlgHandle() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
};

// SHA-256 polos
AlgHandle& ShaAlg() {
    static AlgHandle a = [] {
        AlgHandle x;
        x.status = BCryptOpenAlgorithmProvider(&x.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        return x;
    }();
    return a;
}

// SHA-256 dengan flag HMAC (untuk HMAC dan PBKDF2)
AlgHandle& HmacAlg() {
    static AlgHandle a = [] {
        AlgHandle x;
        x.status = BCryptOpenAlgorithmProvider(&x.h, BCRYPT_SHA256_ALGORITHM, nullptr,
                                              BCRYPT_ALG_HANDLE_HMAC_FLAG);
        return x;
    }();
    return a;
}

struct HashSession {
    BCRYPT_HASH_HANDLE h = nullptr;
    std::vector<uint8_t> objBuf;
    ~HashSession() {
        if (h) BCryptDestroyHash(h);
    }
};

std::unique_ptr<HashSession> StartHash(BCRYPT_ALG_HANDLE alg, const uint8_t* key, size_t keyLen) {
    if (!alg) return nullptr;
    DWORD objSize = 0, cb = 0;
    NTSTATUS st = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                                    reinterpret_cast<PUCHAR>(&objSize), sizeof(objSize), &cb, 0);
    if (!BCRYPT_SUCCESS(st) || objSize == 0) return nullptr;

    auto session = std::make_unique<HashSession>();
    session->objBuf.resize(objSize);

    st = (key && keyLen > 0)
        ? BCryptCreateHash(alg, &session->h, session->objBuf.data(),
                           static_cast<ULONG>(session->objBuf.size()),
                           const_cast<PUCHAR>(key), static_cast<ULONG>(keyLen), 0)
        : BCryptCreateHash(alg, &session->h, session->objBuf.data(),
                           static_cast<ULONG>(session->objBuf.size()),
                           nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(st) || !session->h) return nullptr;
    return session;
}

} // namespace

Bytes32 Sha256(const uint8_t* data, size_t len) {
    Bytes32 out{};
    if (!BCRYPT_SUCCESS(ShaAlg().status)) return out;
    auto hash = StartHash(ShaAlg().h, nullptr, 0);
    if (!hash) return out;

    PUCHAR ptr = (len > 0 && data) ? const_cast<PUCHAR>(data) : reinterpret_cast<PUCHAR>(&out);
    ULONG ulen = (len > 0 && data) ? static_cast<ULONG>(len) : 0;
    if (!BCRYPT_SUCCESS(BCryptHashData(hash->h, ptr, ulen, 0))) {
        return Bytes32{};
    }
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash->h, out.data(), static_cast<ULONG>(out.size()), 0))) {
        return Bytes32{};
    }
    return out;
}

Bytes32 HmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen) {
    Bytes32 out{};
    if (!BCRYPT_SUCCESS(HmacAlg().status)) return out;
    auto hash = StartHash(HmacAlg().h, key, keyLen);
    if (!hash) return out;

    PUCHAR mptr = (msgLen > 0 && msg) ? const_cast<PUCHAR>(msg) : reinterpret_cast<PUCHAR>(&out);
    ULONG mlen = (msgLen > 0 && msg) ? static_cast<ULONG>(msgLen) : 0;
    if (!BCRYPT_SUCCESS(BCryptHashData(hash->h, mptr, mlen, 0))) {
        return Bytes32{};
    }
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash->h, out.data(), static_cast<ULONG>(out.size()), 0))) {
        return Bytes32{};
    }
    return out;
}

Bytes32 Pbkdf2Sha256(const uint8_t* pw, size_t pwLen, const uint8_t* salt,
                     size_t saltLen, uint32_t iterations) {
    Bytes32 out{};
    if (!BCRYPT_SUCCESS(HmacAlg().status) || iterations == 0) return out;

    PUCHAR pwPtr = (pwLen > 0 && pw) ? const_cast<PUCHAR>(pw) : reinterpret_cast<PUCHAR>(&out);
    ULONG pwULen = (pwLen > 0 && pw) ? static_cast<ULONG>(pwLen) : 0;

    PUCHAR saltPtr = (saltLen > 0 && salt) ? const_cast<PUCHAR>(salt) : reinterpret_cast<PUCHAR>(&out);
    ULONG saltULen = (saltLen > 0 && salt) ? static_cast<ULONG>(saltLen) : 0;

    NTSTATUS st = BCryptDeriveKeyPBKDF2(HmacAlg().h,
                                        pwPtr, pwULen,
                                        saltPtr, saltULen,
                                        static_cast<ULONGLONG>(iterations),
                                        out.data(), static_cast<ULONG>(out.size()), 0);
    if (!BCRYPT_SUCCESS(st)) return Bytes32{};
    return out;
}

bool RandomBytes(uint8_t* dst, size_t len) {
    if (!dst || len == 0) return true;
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, dst, static_cast<ULONG>(len),
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

} // namespace ldr::sec
