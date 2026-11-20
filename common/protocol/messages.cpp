#include "messages.h"
#include <algorithm>
#include <cstring>

namespace ldr::proto {

std::vector<uint8_t> EncodeHello(const Hello& m) {
    BinaryWriter w;
    w.U32(m.protoVer); w.U64(m.features); w.Str(m.deviceId); w.Str(m.deviceName); w.U8(m.kind);
    return w.Take();
}
std::optional<Hello> DecodeHello(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    Hello m;
    m.protoVer = r.U32(); m.features = r.U64(); m.deviceId = r.Str(); m.deviceName = r.Str(); m.kind = r.U8();
    if (!r.Ok()) return std::nullopt;
    return m;
}

std::vector<uint8_t> EncodeHelloAck(const HelloAck& m) {
    BinaryWriter w;
    w.U32(m.protoVer); w.U64(m.features); w.Str(m.deviceId); w.Str(m.hostName); w.U64(m.sessionId);
    return w.Take();
}
std::optional<HelloAck> DecodeHelloAck(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    HelloAck m;
    m.protoVer = r.U32(); m.features = r.U64(); m.deviceId = r.Str(); m.hostName = r.Str(); m.sessionId = r.U64();
    if (!r.Ok()) return std::nullopt;
    return m;
}

std::vector<uint8_t> EncodeAuthChallenge(const AuthChallenge& m) {
    BinaryWriter w;
    w.Raw(m.serverNonce.data(), m.serverNonce.size());
    w.Raw(m.salt.data(), m.salt.size());
    w.U32(m.iterations);
    return w.Take();
}
std::optional<AuthChallenge> DecodeAuthChallenge(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    AuthChallenge m;
    if (!r.Raw(m.serverNonce.data(), 32) || !r.Raw(m.salt.data(), 16)) return std::nullopt;
    m.iterations = r.U32();
    if (!r.Ok()) return std::nullopt;
    return m;
}

std::vector<uint8_t> EncodeAuthResponse(const AuthResponse& m) {
    BinaryWriter w;
    w.Raw(m.clientNonce.data(), m.clientNonce.size());
    w.Raw(m.proof.data(), m.proof.size());
    w.Str(m.deviceId); w.Str(m.deviceName); w.U64(m.requestedPerms);
    return w.Take();
}
std::optional<AuthResponse> DecodeAuthResponse(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    AuthResponse m;
    if (!r.Raw(m.clientNonce.data(), 32) || !r.Raw(m.proof.data(), 32)) return std::nullopt;
    m.deviceId = r.Str(); m.deviceName = r.Str(); m.requestedPerms = r.U64();
    if (!r.Ok()) return std::nullopt;
    return m;
}

std::vector<uint8_t> EncodeAuthResult(const AuthResult& m) {
    BinaryWriter w;
    w.U8(m.ok ? 1 : 0); w.Str(m.reason); w.U64(m.permissions);
    return w.Take();
}
std::optional<AuthResult> DecodeAuthResult(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    AuthResult m;
    m.ok = r.U8() != 0; m.reason = r.Str(); m.permissions = r.U64();
    if (!r.Ok()) return std::nullopt;
    return m;
}

std::vector<uint8_t> EncodePermissionUpdate(const PermissionUpdate& m) {
    BinaryWriter w; w.U64(m.permissions); return w.Take();
}
std::optional<PermissionUpdate> DecodePermissionUpdate(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    PermissionUpdate m;
    m.permissions = r.U64();
    if (!r.Ok()) return std::nullopt;
    return m;
}

std::vector<uint8_t> EncodePing(const Ping& m) {
    BinaryWriter w; w.U64(m.tSend); return w.Take();
}
std::optional<Ping> DecodePing(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    Ping m; m.tSend = r.U64();
    if (!r.Ok()) return std::nullopt;
    return m;
}

std::vector<uint8_t> EncodePong(const Pong& m) {
    BinaryWriter w; w.U64(m.tSend); w.U64(m.tRecv); w.U64(m.tNow); return w.Take();
}
std::optional<Pong> DecodePong(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    Pong m; m.tSend = r.U64(); m.tRecv = r.U64(); m.tNow = r.U64();
    if (!r.Ok()) return std::nullopt;
    return m;
}

std::vector<uint8_t> EncodeSessionClosed(const SessionClosed& m) {
    BinaryWriter w; w.U8(m.reasonCode); w.Str(m.text); return w.Take();
}
std::optional<SessionClosed> DecodeSessionClosed(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    SessionClosed m; m.reasonCode = r.U8(); m.text = r.Str();
    if (!r.Ok()) return std::nullopt;
    return m;
}

std::vector<uint8_t> EncodeControlStatus(const ControlStatus& m) {
    BinaryWriter w; w.U8(m.level); w.Str(m.text); return w.Take();
}
std::optional<ControlStatus> DecodeControlStatus(const uint8_t* p, size_t n) {
    BinaryReader r(p, n);
    ControlStatus m; m.level = r.U8(); m.text = r.Str();
    if (!r.Ok()) return std::nullopt;
    return m;
}

} // namespace ldr::proto
