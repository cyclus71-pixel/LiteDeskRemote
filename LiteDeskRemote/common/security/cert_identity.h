// cert_identity.h — identitas TLS host: sertifikat RSA-2048 self-signed (CryptoAPI),
// container kunci persisten, DER disimpan di security/identity.der.
#pragma once
#include <array>
#include <memory>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#else
struct _CERT_CONTEXT;
typedef const struct _CERT_CONTEXT* PCCERT_CONTEXT;
#endif

namespace ldr::sec {

class HostIdentity {
public:
    // Memuat identity.der bila ada (container kunci harus masih ada), atau membuat
    // identitas baru (container + cert + file DER). deviceId ikut ke CN sertifikat.
    static std::unique_ptr<HostIdentity> LoadOrCreate(const std::wstring& securityDir,
                                                      const std::string& deviceId,
                                                      std::string* err);
    ~HostIdentity();

    PCCERT_CONTEXT Cert() const { return cert_; }
    const uint8_t* CertSha256() const { return sha256_.data(); }
    std::string FingerprintHex() const;

private:
    HostIdentity() = default;
    PCCERT_CONTEXT cert_ = nullptr;
    std::array<uint8_t, 32> sha256_{};
    std::wstring container_;
};

} // namespace ldr::sec
