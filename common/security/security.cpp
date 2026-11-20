#include "security.h"
#include "rate_limiter.h"
#include "../util/codec.h"
#include "../util/time.h"
#include "../util/log.h"
#include "../util/config.h"

#if defined(_WIN32)
#include "crypto_win.h"
#else
#include "crypto_portable.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iterator>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ldr::sec {

// ===== RateLimiter::DefaultNow =====
int64_t RateLimiter::DefaultNow() { return util::NowMs(); }

// ===== Device ID =====
std::string FormatDeviceId(const uint8_t raw[10]) {
    // 48 bit = 6 byte → base32 tepat 10 karakter → "LD-XXXXX-XXXXX"
    std::string b32 = util::Base32Encode(raw, 6);
    return "LD-" + b32.substr(0, 5) + "-" + b32.substr(5, 5);
}

#if defined(_WIN32)
std::string ComputeDeviceId() {
    std::string machineGuid;
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
                          KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            wchar_t buf[256] = {0};
            DWORD size = sizeof(buf);
            DWORD type = 0;
            if (RegQueryValueExW(hKey, L"MachineGuid", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
                machineGuid = "M:" + std::string(buf, buf + (size / sizeof(wchar_t)));
            }
            RegCloseKey(hKey);
        }
    }
    wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD compSize = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(compName, &compSize);
    std::string host = "H:" + std::string(compName, compName + compSize);

    std::string volume;
    wchar_t sysDrive[3] = L"C:";
    {
        wchar_t windir[MAX_PATH] = {0};
        UINT n = GetWindowsDirectoryW(windir, MAX_PATH);
        if (n >= 2) { sysDrive[0] = windir[0]; sysDrive[1] = windir[1]; }
    }
    DWORD serial = 0;
    wchar_t volName[MAX_PATH + 1] = {0};
    DWORD fsFlags = 0;
    wchar_t fsName[MAX_PATH + 1] = {0};
    if (GetVolumeInformationW((std::wstring(sysDrive) + L"\\").c_str(), volName, MAX_PATH,
                              &serial, nullptr, &fsFlags, fsName, MAX_PATH)) {
        volume = "V:" + std::to_string(serial);
    }

    std::string fp = machineGuid + "|" + host + "|" + volume;
    auto hash = Sha256(reinterpret_cast<const uint8_t*>(fp.data()), fp.size());
    return FormatDeviceId(hash.data());
}
#else
std::string ComputeDeviceId() {
    // Non-Windows (unit test): fingerprint pseudo-stabil dari hostname.
    char buf[128] = {0};
    if (gethostname(buf, sizeof(buf) - 1) != 0)
        snprintf(buf, sizeof(buf), "unknown-host");
    std::string fp = std::string("test:") + buf;
    auto hash = Sha256(reinterpret_cast<const uint8_t*>(fp.data()), fp.size());
    return FormatDeviceId(hash.data());
}
#endif

// ===== PinStore =====

static std::string B32(const std::vector<uint8_t>& v) {
    return util::Base32Encode(v.data(), v.size());
}

std::vector<uint8_t> Base32Decode(const std::string& s) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::vector<uint8_t> out;
    uint32_t buf = 0;
    int bits = 0;
    for (char c : s) {
        const char* pos = std::strchr(A, c);
        if (!pos) return {};
        buf = (buf << 5) | static_cast<uint32_t>(pos - A);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

bool PinStore::Exists() {
    std::error_code ec;
    return fs::exists(std::filesystem::path(path_), ec);
}

bool PinStore::Load() {
    std::ifstream f(std::filesystem::path(path_), std::ios::binary);
    if (!f) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto v = util::json::Parse(content);
    if (!v || !v->IsObj()) return false;
    auto rec = std::make_unique<PinRecord>();
    if (const util::json::Value* x = v->Find("v")) {
        auto n = x->AsInt(); if (n) rec->v = static_cast<uint32_t>(*n);
    }
    if (const util::json::Value* x = v->Find("salt_b64")) {
        auto s = x->AsStr(); if (s) rec->saltB64 = *s;
    }
    if (const util::json::Value* x = v->Find("digest_b64")) {
        auto s = x->AsStr(); if (s) rec->digestB64 = *s;
    }
    if (const util::json::Value* x = v->Find("iterations")) {
        auto n = x->AsInt(); if (n) rec->iterations = static_cast<uint32_t>(*n);
    }
    if (const util::json::Value* x = v->Find("created")) {
        auto n = x->AsInt(); if (n) rec->created = *n;
    }
    if (const util::json::Value* x = v->Find("updated")) {
        auto n = x->AsInt(); if (n) rec->updated = *n;
    }
    auto salt = Base32Decode(rec->saltB64);
    auto dig = Base32Decode(rec->digestB64);
    if (salt.size() != 16 || dig.size() != 32 || rec->iterations < 10000) {
        LLOG_E("pin.json korup atau tidak lengkap");
        return false;
    }
    rec_ = std::move(rec);
    return true;
}

bool PinStore::SaveRecord(const PinRecord& rec, std::string* err) {
    util::json::Value v = util::json::Value::ObjectV();
    v.Set("v", util::json::Value::NumV(rec.v));
    v.Set("salt_b64", util::json::Value::StrV(rec.saltB64));
    v.Set("digest_b64", util::json::Value::StrV(rec.digestB64));
    v.Set("iterations", util::json::Value::NumV(rec.iterations));
    v.Set("created", util::json::Value::NumV(static_cast<double>(rec.created)));
    v.Set("updated", util::json::Value::NumV(static_cast<double>(rec.updated)));

    std::error_code ec;
    fs::create_directories(std::filesystem::path(path_).parent_path(), ec);
    std::filesystem::path finalPath(path_);
    std::filesystem::path tmp = finalPath.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (err) *err = "tidak bisa menulis pin.json"; return false; }
        f << util::json::Dump(v, 2);
        f.flush();
        if (!f.good()) { if (err) *err = "gagal menulis pin.json"; return false; }
    }
    fs::rename(tmp, finalPath, ec);
    if (ec) { if (err) *err = "gagal rename pin.json"; return false; }
    return true;
}

bool PinStore::Generate(int digits, std::string* outPin, std::string* err) {
    if (digits < 4 || digits > 10) { if (err) *err = "panjang PIN harus 4-10 digit"; return false; }
    std::vector<uint8_t> rnd(digits);
    if (!RandomBytes(rnd.data(), rnd.size())) { if (err) *err = "random generator gagal"; return false; }
    std::string pin;
    for (uint8_t b : rnd) pin += static_cast<char>('0' + (b % 10));
    if (!SetCustom(pin, err)) return false;
    *outPin = pin;
    return true;
}

bool PinStore::SetCustom(const std::string& pin, std::string* err) {
    if (pin.size() < 4 || pin.size() > 10) { if (err) *err = "panjang PIN harus 4-10 digit"; return false; }
    for (char c : pin)
        if (c < '0' || c > '9') { if (err) *err = "PIN hanya boleh berisi angka"; return false; }

    PinRecord rec;
    uint8_t salt[16];
    if (!RandomBytes(salt, sizeof(salt))) { if (err) *err = "random generator gagal"; return false; }
    rec.iterations = static_cast<uint32_t>(
        util::Config::I().GetInt("security.pin_iterations", 210000));
    rec.created = rec.updated = util::WallNowMs();
    rec.saltB64 = B32(std::vector<uint8_t>(salt, salt + 16));

    auto digest = Pbkdf2Sha256(reinterpret_cast<const uint8_t*>(pin.data()), pin.size(),
                               salt, sizeof(salt), rec.iterations);
    rec.digestB64 = B32(std::vector<uint8_t>(digest.begin(), digest.end()));

    if (!SaveRecord(rec, err)) return false;
    rec_ = std::make_unique<PinRecord>(rec);
    LLOG_I("PIN baru disimpan (iter=%u, panjang=%zu)", rec.iterations, pin.size());
    return true;
}

bool PinStore::Verify(const std::string& pin) const {
    if (!rec_) return false;
    auto salt = Base32Decode(rec_->saltB64);
    auto dig = Base32Decode(rec_->digestB64);
    if (salt.size() != 16 || dig.size() != 32) return false;
    auto calc = Pbkdf2Sha256(reinterpret_cast<const uint8_t*>(pin.data()), pin.size(),
                             salt.data(), salt.size(), rec_->iterations);
    return util::ConstTimeEqual(calc.data(), dig.data(), 32);
}

// ===== Auth proof =====
static std::vector<uint8_t> BuildAuthMsg(const uint8_t serverNonce[32],
                                         const uint8_t clientNonce[32],
                                         uint64_t sessionId, const uint8_t certSha256[32]) {
    std::vector<uint8_t> msg;
    util::AppendBytes(msg, "LDR1", 4);
    util::AppendBytes(msg, serverNonce, 32);
    util::AppendBytes(msg, clientNonce, 32);
    util::AppendU64(msg, sessionId);
    util::AppendBytes(msg, certSha256, 32);
    return msg;
}

Bytes32 ComputeAuthProof(const uint8_t digest[32], const uint8_t serverNonce[32],
                         const uint8_t clientNonce[32], uint64_t sessionId,
                         const uint8_t certSha256[32]) {
    auto msg = BuildAuthMsg(serverNonce, clientNonce, sessionId, certSha256);
    return HmacSha256(digest, 32, msg.data(), msg.size());
}

bool VerifyAuthProof(const uint8_t storedDigest[32], const uint8_t proof[32],
                     const uint8_t serverNonce[32], const uint8_t clientNonce[32],
                     uint64_t sessionId, const uint8_t certSha256[32]) {
    auto expect = ComputeAuthProof(storedDigest, serverNonce, clientNonce, sessionId, certSha256);
    return util::ConstTimeEqual(expect.data(), proof, 32);
}

// ===== Audit =====
void AuditAppend(const std::wstring& file, const std::string& event,
                 const std::string& ip, const std::string& deviceId,
                 const std::string& detail) {
    util::json::Value v = util::json::Value::ObjectV();
    v.Set("ts", util::json::Value::StrV(util::FormatWall(util::WallNowMs())));
    v.Set("event", util::json::Value::StrV(event));
    v.Set("ip", util::json::Value::StrV(ip));
    v.Set("device_id", util::json::Value::StrV(deviceId));
    v.Set("detail", util::json::Value::StrV(detail));

    std::error_code ec;
    fs::create_directories(std::filesystem::path(file).parent_path(), ec);
    std::ofstream f(std::filesystem::path(file), std::ios::binary | std::ios::app);
    if (f) f << util::json::Dump(v, 0);
    LLOG_I("audit: %s ip=%s id=%s %s", event.c_str(), ip.c_str(), deviceId.c_str(), detail.c_str());
}

} // namespace ldr::sec
