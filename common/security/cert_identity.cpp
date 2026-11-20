// cert_identity.cpp — identitas TLS host.
// RSA-2048 dibuat di container CryptoAPI persisten ("LiteDesk-Host-Identity"),
// sertifikat self-signed 10 tahun (EKU ServerAuth+ClientAuth), DER disimpan ke
// security/identity.der. Pada start berikutnya, sertifikat dimuat dan container kunci
// yang sama dipakai Schannel (via CERT_KEY_PROV_INFO_PROP_ID).
#include "cert_identity.h"
#include "crypto_win.h"
#include "security.h"
#include "../util/log.h"
#include "../util/codec.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace ldr::sec {

namespace {

const wchar_t* kContainer = L"LiteDesk-Host-Identity";
const wchar_t* kProvName = MS_ENH_RSA_AES_PROV_W;
const DWORD kProvType = PROV_RSA_AES;

// RAII kecil untuk handle provider (HCRYPTPROV bukan pointer -> tak bisa unique_ptr).
struct CryptoProv {
    HCRYPTPROV h = 0;
    bool ok = false;
    CryptoProv() = default;
    ~CryptoProv() { if (ok && h) CryptReleaseContext(h, 0); }
    CryptoProv(const CryptoProv&) = delete;
    CryptoProv& operator=(const CryptoProv&) = delete;
};

bool AcquireContainer(CryptoProv& out, bool* created) {
    HCRYPTPROV h = 0;
    if (CryptAcquireContextW(&h, kContainer, kProvName, kProvType, 0)) {
        if (created) *created = false;
        out.h = h;
        out.ok = true;
        return true;
    }
    if (GetLastError() == static_cast<DWORD>(NTE_BAD_KEYSET)) {
        if (CryptAcquireContextW(&h, kContainer, kProvName, kProvType, CRYPT_NEWKEYSET)) {
            if (created) *created = true;
            out.h = h;
            out.ok = true;
            return true;
        }
    }
    return false;
}

std::vector<uint8_t> ReadFileBytes(const std::wstring& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

} // namespace

HostIdentity::~HostIdentity() {
    if (cert_) CertFreeCertificateContext(cert_);
}

std::string HostIdentity::FingerprintHex() const {
    return util::HexEncode(sha256_.data(), sha256_.size(), true);
}

std::unique_ptr<HostIdentity> HostIdentity::LoadOrCreate(const std::wstring& securityDir,
                                                         const std::string& deviceId,
                                                         std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(securityDir, ec);
    std::wstring derPath = securityDir + L"\\identity.der";

    // 1. Container kunci (persisten di profil pengguna).
    CryptoProv prov;
    bool created = false;
    if (!AcquireContainer(prov, &created)) {
        if (err) *err = "tidak bisa membuka/membuat key container CryptoAPI";
        return nullptr;
    }
    if (created) {
        // RSA-2048 (ukuran di word tinggi), AT_KEYEXCHANGE untuk TLS server.
        HCRYPTKEY hKey = 0;
        if (!CryptGenKey(prov.h, AT_KEYEXCHANGE, (2048 << 16) | CRYPT_EXPORTABLE, &hKey)) {
            if (err) *err = "gagal membuat kunci RSA-2048";
            return nullptr;
        }
        CryptDestroyKey(hKey);
    }

    auto id = std::unique_ptr<HostIdentity>(new HostIdentity());

    // 2. Muat DER yang ada, atau buat sertifikat self-signed baru.
    auto der = ReadFileBytes(derPath);
    if (!der.empty()) {
        id->cert_ = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                 der.data(), static_cast<DWORD>(der.size()));
        if (!id->cert_) {
            LLOG_W("identity.der korup; identitas baru akan dibuat");
        }
    }

    if (!id->cert_) {
        // CN = LiteDesk Host <deviceId>
        std::wstring dn = L"CN=LiteDesk Host " + std::wstring(deviceId.begin(), deviceId.end()) +
                          L", O=LiteDesk Remote";
        DWORD nameLen = 0;
        if (!CertStrToNameW(X509_ASN_ENCODING, dn.c_str(), CERT_X500_NAME_STR, nullptr,
                            nullptr, &nameLen, nullptr) || nameLen == 0) {
            if (err) *err = "gagal encode subject name";
            return nullptr;
        }
        std::vector<BYTE> nameBlob(nameLen);
        if (!CertStrToNameW(X509_ASN_ENCODING, dn.c_str(), CERT_X500_NAME_STR, nullptr,
                            nameBlob.data(), &nameLen, nullptr)) {
            if (err) *err = "gagal encode subject name (2)";
            return nullptr;
        }
        CERT_NAME_BLOB blob{static_cast<DWORD>(nameBlob.size()), nameBlob.data()};

        // EKU: Server Auth + Client Auth.
        CERT_ENHKEY_USAGE eku{};
        LPSTR usage[2] = {const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH),
                          const_cast<LPSTR>(szOID_PKIX_KP_CLIENT_AUTH)};
        eku.cUsageIdentifier = 2;
        eku.rgpszUsageIdentifier = usage;
        DWORD ekuLen = 0;
        if (!CryptEncodeObject(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &eku,
                               nullptr, &ekuLen) || ekuLen == 0) {
            if (err) *err = "gagal encode EKU";
            return nullptr;
        }
        std::vector<BYTE> ekuBlob(ekuLen);
        if (!CryptEncodeObject(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &eku,
                               ekuBlob.data(), &ekuLen)) {
            if (err) *err = "gagal encode EKU (2)";
            return nullptr;
        }
        CERT_EXTENSION exts[1]{};
        exts[0].pszObjId = const_cast<LPSTR>(szOID_ENHANCED_KEY_USAGE);
        exts[0].fCritical = FALSE;
        exts[0].Value.cbData = ekuLen;
        exts[0].Value.pbData = ekuBlob.data();
        CERT_EXTENSIONS extensions{1, exts};

        SYSTEMTIME st{}, et{};
        GetSystemTime(&st);
        et = st;
        et.wYear += 10;

        // Biarkan pSignatureAlgorithm = nullptr agar CryptoAPI memilih algoritma default
        // yang sesuai dengan kunci container (RSA-2048) tanpa memicu NTE_BAD_FLAGS.
        PCCERT_CONTEXT cert = CertCreateSelfSignCertificate(
            prov.h, &blob, 0, nullptr, nullptr, &st, &et, &extensions);
        if (!cert) {
            DWORD e = GetLastError();
            char buf[96];
            std::snprintf(buf, sizeof(buf), "gagal membuat sertifikat self-signed (err=%lu)",
                          static_cast<unsigned long>(e));
            if (err) *err = buf;
            return nullptr;
        }
        id->cert_ = cert;

        // Ikatan kunci: CERT_KEY_PROV_INFO agar Schannel menemukan kunci privat.
        CRYPT_KEY_PROV_INFO kpi{};
        kpi.pwszContainerName = const_cast<LPWSTR>(kContainer);
        kpi.pwszProvName = const_cast<LPWSTR>(kProvName);
        kpi.dwProvType = kProvType;
        kpi.dwKeySpec = AT_KEYEXCHANGE;
        CertSetCertificateContextProperty(id->cert_, CERT_KEY_PROV_INFO_PROP_ID, 0, &kpi);

        // Simpan DER.
        std::ofstream f(std::filesystem::path(derPath), std::ios::binary | std::ios::trunc);
        if (!f || !f.write(reinterpret_cast<const char*>(id->cert_->pbCertEncoded),
                           id->cert_->cbCertEncoded)) {
            if (err) *err = "gagal menyimpan identity.der";
            return nullptr;
        }
        LLOG_I("identitas TLS host baru dibuat (RSA-2048, 10 tahun)");
    } else {
        // Pastikan properti KEY_PROV_INFO terpasang (bila DER ada tetapi container baru).
        CRYPT_KEY_PROV_INFO kpi{};
        kpi.pwszContainerName = const_cast<LPWSTR>(kContainer);
        kpi.pwszProvName = const_cast<LPWSTR>(kProvName);
        kpi.dwProvType = kProvType;
        kpi.dwKeySpec = AT_KEYEXCHANGE;
        CertSetCertificateContextProperty(id->cert_, CERT_KEY_PROV_INFO_PROP_ID, 0, &kpi);
        LLOG_I("identitas TLS host dimuat dari identity.der");
    }

    // 3. Fingerprint = SHA-256 keseluruhan sertifikat (dipakai untuk pinning).
    DWORD hashLen = 32;
    if (!CertGetCertificateContextProperty(id->cert_, CERT_SHA256_HASH_PROP_ID,
                                           id->sha256_.data(), &hashLen) || hashLen != 32) {
        if (err) *err = "gagal menghitung fingerprint sertifikat";
        return nullptr;
    }
    return id;
}

} // namespace ldr::sec
