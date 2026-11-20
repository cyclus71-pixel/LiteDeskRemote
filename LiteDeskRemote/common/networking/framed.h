// framed.h — pesan protokol (frame) di atas TlsChannel, dengan timeout per operasi.
#pragma once
#include "../protocol/protocol.h"
#include "tls.h"

namespace ldr::net {

class FramedChannel {
public:
    explicit FramedChannel(TlsChannel& tls) : tls_(tls) {}

    // Membaca satu frame utuh dengan deadline timeoutMs.
    // false bila gagal/timeout/koneksi mati (err berisi alasan; "timeout" = waktu habis).
    bool ReadFrame(proto::Frame& out, int timeoutMs, std::string* err);

    // Mengirim satu frame (header + payload) dalam satu panggilan TLS Send.
    bool WriteFrame(proto::MsgType type, const std::vector<uint8_t>& payload,
                    std::string* err = nullptr);

    void CloseNotify() { tls_.CloseNotify(); }

private:
    TlsChannel& tls_;
    std::vector<uint8_t> acc_; // stream plaintext yang belum jadi frame lengkap
};

} // namespace ldr::net
