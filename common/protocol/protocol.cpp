#include "protocol.h"
#include "../util/codec.h"
#include <cstdio>

namespace ldr::proto {

const char* MsgTypeName(MsgType t) {
    switch (t) {
        case MsgType::Hello:            return "Hello";
        case MsgType::HelloAck:         return "HelloAck";
        case MsgType::AuthChallenge:    return "AuthChallenge";
        case MsgType::AuthResponse:     return "AuthResponse";
        case MsgType::AuthResult:       return "AuthResult";
        case MsgType::PermissionUpdate: return "PermissionUpdate";
        case MsgType::Ping:             return "Ping";
        case MsgType::Pong:             return "Pong";
        case MsgType::SessionClosed:    return "SessionClosed";
        case MsgType::ControlStatus:    return "ControlStatus";
        case MsgType::VideoData:        return "VideoData";
        case MsgType::InputMouse:       return "InputMouse";
        case MsgType::InputKeyboard:    return "InputKeyboard";
        case MsgType::FileChunk:        return "FileChunk";
        case MsgType::AudioData:        return "AudioData";
        case MsgType::ClipboardData:    return "ClipboardData";
        default:                        return "Unknown";
    }
}

const char* ReasonText(uint8_t code) {
    switch (code) {
        case reason::Normal:          return "sesi ditutup normal";
        case reason::AuthFailed:      return "autentikasi gagal";
        case reason::DeniedByUser:    return "ditolak oleh pengguna host";
        case reason::MaxSessions:     return "batas jumlah sesi tercapai";
        case reason::IdleTimeout:     return "sesi tidak aktif (idle timeout)";
        case reason::ProtocolError:   return "kesalahan protokol";
        case reason::ServerStopping:  return "remote access dimatikan di host";
        case reason::PermissionDenied: return "izin tidak diberikan";
        case reason::VersionMismatch: return "versi protokol tidak cocok";
        case reason::LockedOut:       return "terkunci sementara (terlalu banyak percobaan gagal)";
        case reason::ChallengeBudget: return "terlalu banyak permintaan challenge";
        default:                      return "tidak diketahui";
    }
}

void WriteHeader(uint8_t dst[kFrameHeaderSize], MsgType type, uint32_t payloadSize) {
    util::PutU32LE(dst + 0, kFrameMagic);
    util::PutU16LE(dst + 4, kProtocolVersion);
    util::PutU16LE(dst + 6, static_cast<uint16_t>(type));
    util::PutU32LE(dst + 8, payloadSize);
    util::PutU32LE(dst + 12, 0); // flags
}

bool ParseHeader(const uint8_t* src, size_t len, FrameHeader* out) {
    if (len < kFrameHeaderSize) return false;
    if (util::GetU32LE(src) != kFrameMagic) return false;
    uint16_t ver = util::GetU16LE(src + 4);
    if (ver != kProtocolVersion) return false;
    uint32_t size = util::GetU32LE(src + 8);
    if (size > kMaxPayload) return false;
    out->version = ver;
    out->type = static_cast<MsgType>(util::GetU16LE(src + 6));
    out->payloadSize = size;
    return true;
}

std::vector<uint8_t> BuildFrame(MsgType type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.reserve(kFrameHeaderSize + payload.size());
    out.resize(kFrameHeaderSize);
    WriteHeader(out.data(), type, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

void FramedReader::Feed(const uint8_t* data, size_t len) {
    if (failed_) return;
    buf_.insert(buf_.end(), data, data + len);
    // Guard: header muncul tetapi ukuran payload melebihi batas → protokol error.
    while (true) {
        if (buf_.size() < kFrameHeaderSize) return;
        FrameHeader h;
        if (!ParseHeader(buf_.data(), buf_.size(), &h)) {
            // Bisa berarti magic/version salah ATAU belum cukup byte utk field ukuran.
            // ParseHeader butuh 16 byte; bila sudah cukup 16 byte berarti salah sungguhan.
            if (buf_.size() >= kFrameHeaderSize) {
                // cek magic spesifik agar pesan error informatif
                if (util::GetU32LE(buf_.data()) != kFrameMagic) {
                    failed_ = true;
                    error_ = "magic frame tidak valid";
                } else if (util::GetU16LE(buf_.data() + 4) != kProtocolVersion) {
                    failed_ = true;
                    error_ = "versi protokol tidak didukung";
                } else {
                    failed_ = true;
                    error_ = "ukuran payload melebihi batas";
                }
            }
            return;
        }
        if (buf_.size() < kFrameHeaderSize + h.payloadSize) return; // menunggu payload
        Frame f;
        f.type = h.type;
        f.payload.assign(buf_.begin() + kFrameHeaderSize,
                         buf_.begin() + kFrameHeaderSize + h.payloadSize);
        frames_.push_back(std::move(f));
        buf_.erase(buf_.begin(), buf_.begin() + (kFrameHeaderSize + h.payloadSize));
    }
}

std::vector<Frame> FramedReader::TakeFrames() {
    std::vector<Frame> out;
    out.swap(frames_);
    return out;
}

void FramedReader::Reset() {
    buf_.clear();
    frames_.clear();
    failed_ = false;
    error_.clear();
}

} // namespace ldr::proto
