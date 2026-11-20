// protocol.h — spesifikasi framing kawat LiteDesk Remote (protokol v1).
// Header 16 byte little-endian, guard ukuran, parser streaming.
#pragma once
#include "../util/codec.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ldr::proto {

constexpr uint32_t kFrameMagic = 0x4C44524Du;      // "LDRM"
constexpr uint16_t kProtocolVersion = 1;
constexpr size_t kFrameHeaderSize = 16;
constexpr size_t kMaxPayload = 1u * 1024u * 1024u; // 1 MiB guard

// Reservasi nomor type untuk fase berikutnya (lihat docs/ARCHITECTURE.md §4):
//   100..149 file transfer (FASE 4), 200..249 video (FASE 2), 300..349 audio (FASE 4),
//   400..449 input (FASE 3), 500..549 clipboard (FASE 4).
enum class MsgType : uint16_t {
    Hello = 1,
    HelloAck = 2,
    AuthChallenge = 3,
    AuthResponse = 4,
    AuthResult = 5,
    PermissionUpdate = 6,
    Ping = 7,
    Pong = 8,
    SessionClosed = 9,
    ControlStatus = 10,
    VideoData = 200,      // FASE 2 (reservasi)
    InputMouse = 400,     // FASE 3 (reservasi)
    InputKeyboard = 401,  // FASE 3 (reservasi)
    FileChunk = 100,      // FASE 4 (reservasi)
    AudioData = 300,      // FASE 4 (reservasi)
    ClipboardData = 500,  // FASE 4 (reservasi)
};

const char* MsgTypeName(MsgType t);

// Kode alasan penutupan sesi (SESSION_CLOSED.reasonCode).
namespace reason {
constexpr uint8_t Normal = 0;
constexpr uint8_t AuthFailed = 1;
constexpr uint8_t DeniedByUser = 2;
constexpr uint8_t MaxSessions = 3;
constexpr uint8_t IdleTimeout = 4;
constexpr uint8_t ProtocolError = 5;
constexpr uint8_t ServerStopping = 6;
constexpr uint8_t PermissionDenied = 7;
constexpr uint8_t VersionMismatch = 8;
constexpr uint8_t LockedOut = 9;
constexpr uint8_t ChallengeBudget = 10;
} // namespace reason

const char* ReasonText(uint8_t code);

struct FrameHeader {
    uint16_t version = kProtocolVersion;
    MsgType type = MsgType::Hello;
    uint32_t payloadSize = 0;
};

void WriteHeader(uint8_t dst[kFrameHeaderSize], MsgType type, uint32_t payloadSize);
// Validasi magic, versi, dan batas ukuran. Mengembalikan false bila invalid.
bool ParseHeader(const uint8_t* src, size_t len, FrameHeader* out);

std::vector<uint8_t> BuildFrame(MsgType type, const std::vector<uint8_t>& payload);

struct Frame {
    MsgType type = MsgType::Hello;
    std::vector<uint8_t> payload;
};

// Parser streaming: Feed menerima byte apa pun (parsial/puny/berantakan),
// TakeFrames mengembalikan frame utuh yang sudah lengkap. Bila protocol error
// (magic/version/ukuran salah) → Failed() true dan Error() berisi alasan.
class FramedReader {
public:
    void Feed(const uint8_t* data, size_t len);
    void Feed(const std::vector<uint8_t>& data) { Feed(data.data(), data.size()); }
    std::vector<Frame> TakeFrames();
    bool Failed() const { return failed_; }
    const std::string& Error() const { return error_; }
    void Reset();

private:
    std::vector<uint8_t> buf_;
    std::vector<Frame> frames_;
    bool failed_ = false;
    std::string error_;
};

} // namespace ldr::proto
