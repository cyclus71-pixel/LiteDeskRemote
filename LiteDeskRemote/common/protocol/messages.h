// messages.h — definisi dan encode/decode seluruh pesan protokol v1.
// Field order pada setiap pesan KONTRAK KAWAT — tidak boleh berubah tanpa naik versi.
#pragma once
#include "protocol.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace ldr::proto {

// ---- BinaryWriter / BinaryReader dengan bound check ketat ----

class BinaryWriter {
public:
    void U8(uint8_t v)  { util::AppendU8(buf_, v); }
    void U16(uint16_t v) { util::AppendU16(buf_, v); }
    void U32(uint32_t v) { util::AppendU32(buf_, v); }
    void U64(uint64_t v) { util::AppendU64(buf_, v); }
    void Raw(const void* p, size_t n) { util::AppendBytes(buf_, p, n); }
    void Str(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(std::min<size_t>(s.size(), 1024));
        U16(len);
        Raw(s.data(), len);
    }
    const std::vector<uint8_t>& Data() const { return buf_; }
    std::vector<uint8_t> Take() { return std::move(buf_); }
private:
    std::vector<uint8_t> buf_;
};

class BinaryReader {
public:
    BinaryReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    explicit BinaryReader(const std::vector<uint8_t>& v) : p_(v.data()), n_(v.size()) {}
    bool Ok() const { return ok_; }

    uint8_t  U8()  { if (!Need(1)) return 0;  return p_[off_++]; }
    uint16_t U16() { if (!Need(2)) return 0;  uint16_t v = util::GetU16LE(p_ + off_); off_ += 2; return v; }
    uint32_t U32() { if (!Need(4)) return 0;  uint32_t v = util::GetU32LE(p_ + off_); off_ += 4; return v; }
    uint64_t U64() { if (!Need(8)) return 0;  uint64_t v = util::GetU64LE(p_ + off_); off_ += 8; return v; }
    std::string Str() {
        uint16_t len = U16();
        if (!ok_ || !Need(len)) return {};
        std::string s(reinterpret_cast<const char*>(p_ + off_), len);
        off_ += len;
        return s;
    }
    bool Raw(uint8_t* dst, size_t n) {
        if (!Need(n)) return false;
        std::memcpy(dst, p_ + off_, n);
        off_ += n;
        return true;
    }
    std::vector<uint8_t> RawVec(size_t n) {
        std::vector<uint8_t> v;
        if (!Need(n)) return v;
        v.assign(p_ + off_, p_ + off_ + n);
        off_ += n;
        return v;
    }
private:
    bool Need(size_t k) {
        if (!ok_) return false;
        if (off_ + k > n_) { ok_ = false; return false; }
        return true;
    }
    const uint8_t* p_;
    size_t n_;
    size_t off_ = 0;
    bool ok_ = true;
};

// ---- Pesan ----

struct Hello {
    uint32_t protoVer = kProtocolVersion;
    uint64_t features = 0;
    std::string deviceId;
    std::string deviceName;
    uint8_t kind = 0; // 0=viewer, 1=host
};

struct HelloAck {
    uint32_t protoVer = kProtocolVersion;
    uint64_t features = 0;
    std::string deviceId;
    std::string hostName;
    uint64_t sessionId = 0;
};

struct AuthChallenge {
    std::array<uint8_t, 32> serverNonce{};
    std::array<uint8_t, 16> salt{};
    uint32_t iterations = 0;
};

struct AuthResponse {
    std::array<uint8_t, 32> clientNonce{};
    std::array<uint8_t, 32> proof{};
    std::string deviceId;
    std::string deviceName;
    uint64_t requestedPerms = 0;
};

struct AuthResult {
    bool ok = false;
    std::string reason;
    uint64_t permissions = 0;
};

struct PermissionUpdate {
    uint64_t permissions = 0;
};

struct Ping {
    uint64_t tSend = 0;
};

struct Pong {
    uint64_t tSend = 0;
    uint64_t tRecv = 0;
    uint64_t tNow = 0;
};

struct SessionClosed {
    uint8_t reasonCode = 0;
    std::string text;
};

struct ControlStatus {
    uint8_t level = 0; // 0=info 1=warn 2=error
    std::string text;
};

// ---- Encode/Decode (mengembalikan std::nullopt bila payload tidak valid) ----

std::vector<uint8_t> EncodeHello(const Hello& m);
std::optional<Hello> DecodeHello(const uint8_t* p, size_t n);
std::vector<uint8_t> EncodeHelloAck(const HelloAck& m);
std::optional<HelloAck> DecodeHelloAck(const uint8_t* p, size_t n);
std::vector<uint8_t> EncodeAuthChallenge(const AuthChallenge& m);
std::optional<AuthChallenge> DecodeAuthChallenge(const uint8_t* p, size_t n);
std::vector<uint8_t> EncodeAuthResponse(const AuthResponse& m);
std::optional<AuthResponse> DecodeAuthResponse(const uint8_t* p, size_t n);
std::vector<uint8_t> EncodeAuthResult(const AuthResult& m);
std::optional<AuthResult> DecodeAuthResult(const uint8_t* p, size_t n);
std::vector<uint8_t> EncodePermissionUpdate(const PermissionUpdate& m);
std::optional<PermissionUpdate> DecodePermissionUpdate(const uint8_t* p, size_t n);
std::vector<uint8_t> EncodePing(const Ping& m);
std::optional<Ping> DecodePing(const uint8_t* p, size_t n);
std::vector<uint8_t> EncodePong(const Pong& m);
std::optional<Pong> DecodePong(const uint8_t* p, size_t n);
std::vector<uint8_t> EncodeSessionClosed(const SessionClosed& m);
std::optional<SessionClosed> DecodeSessionClosed(const uint8_t* p, size_t n);
std::vector<uint8_t> EncodeControlStatus(const ControlStatus& m);
std::optional<ControlStatus> DecodeControlStatus(const uint8_t* p, size_t n);

} // namespace ldr::proto
