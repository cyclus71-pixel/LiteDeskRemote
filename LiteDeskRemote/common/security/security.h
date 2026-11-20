// security.h — identitas perangkat, PIN store (PBKDF2), helper auth proof, audit.
// Bagian kripto memakai CNG di Windows (crypto_win.h); bagian murni lintas-platform
// untuk dapat diuji di OS mana pun.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../util/json.h"

namespace ldr::sec {

// ===== Random =====
// Windows: BCryptGenRandom (CNG). Non-Windows: fallback std::random_device (HANYA untuk
// unit test di lingkungan non-Windows; build produksi selalu Windows/CNG).
bool RandomBytes(uint8_t* dst, size_t len);

// ===== Device ID =====
// Fingerprint mesin: MachineGuid (registry) + ComputerName + VolumeSerialNumber,
// di-hash SHA-256 → 6 byte pertama (48 bit) → base32 → format "LD-XXXXX-XXXXX".
std::string FormatDeviceId(const uint8_t raw[10]);
// Implementasi penuh hanya di Windows (butuh registry + volume serial).
std::string ComputeDeviceId();

// Dekoder base32 (RFC 4648) untuk salt/digest/fingerprint yang tersimpan sebagai teks.
std::vector<uint8_t> Base32Decode(const std::string& s);

// ===== PinStore =====
// PIN tidak pernah disimpan plaintext. Yang disimpan: salt acak 16B + jumlah iterasi
// + digest PBKDF2-HMAC-SHA-256 32B. PIN ditampilkan SEKALI saat dibuat; setelah itu
// hanya bisa diganti (generate baru).
struct PinRecord {
    uint32_t v = 1;
    std::string saltB64;       // base32 (16 byte)
    std::string digestB64;     // base32 (32 byte)
    uint32_t iterations = 210000;
    int64_t created = 0;
    int64_t updated = 0;
};

class PinStore {
public:
    explicit PinStore(std::wstring filePath) : path_(std::move(filePath)) {}
    bool Exists();
    // Membuat PIN baru acak (digits digit). outPin diisi PIN plaintext SEKALI ini.
    bool Generate(int digits, std::string* outPin, std::string* err);
    // PIN kustom dari pengguna (4..10 digit).
    bool SetCustom(const std::string& pin, std::string* err);
    bool Verify(const std::string& pin) const;
    const PinRecord* Get() const { return rec_ ? &*rec_ : nullptr; }
    const std::wstring& Path() const { return path_; }

private:
    bool Load();
    bool SaveRecord(const PinRecord& rec, std::string* err);
    std::wstring path_;
    std::unique_ptr<PinRecord> rec_;
};

// ===== Auth proof (challenge-response) =====
// proof = HMAC-SHA256(key = digest PBKDF2, msg = "LDR1" || serverNonce || clientNonce
//                     || sessionId(8 LE) || certSha256(32))
using Bytes32 = std::array<uint8_t, 32>;
Bytes32 ComputeAuthProof(const uint8_t digest[32], const uint8_t serverNonce[32],
                         const uint8_t clientNonce[32], uint64_t sessionId,
                         const uint8_t certSha256[32]);
bool VerifyAuthProof(const uint8_t storedDigest[32], const uint8_t proof[32],
                     const uint8_t serverNonce[32], const uint8_t clientNonce[32],
                     uint64_t sessionId, const uint8_t certSha256[32]);

// ===== Audit log =====
// Satu baris JSON per kejadian ke audit-latest.jsonl. TIDAK PERNAH mencatat PIN/secret.
void AuditAppend(const std::wstring& file, const std::string& event,
                 const std::string& ip, const std::string& deviceId,
                 const std::string& detail);

} // namespace ldr::sec
