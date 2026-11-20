#include "framed.h"
#include "../util/time.h"

namespace ldr::net {

bool FramedChannel::ReadFrame(proto::Frame& out, int timeoutMs, std::string* err) {
    int64_t deadline = util::NowMs() + timeoutMs;
    // 1. Coba parse dari akumulasi yang sudah ada.
    // 2. Bila kurang, baca plaintext TLS dalam potongan 8 KB dan coba lagi.
    while (true) {
        proto::FramedReader reader;
        reader.Feed(acc_.data(), acc_.size());
        if (reader.Failed()) {
            if (err) *err = "protokol error: " + reader.Error();
            return false;
        }
        auto frames = reader.TakeFrames();
        if (!frames.empty()) {
            // Tidak ada frame lain dalam satu loop karena kita memproses satu frame per baca;
            // sisa byte tetap di acc_.
            size_t consumed = proto::kFrameHeaderSize + frames[0].payload.size();
            acc_.erase(acc_.begin(), acc_.begin() + static_cast<long>(consumed));
            out = std::move(frames[0]);
            return true;
        }
        if (util::NowMs() >= deadline) {
            if (err) *err = "timeout menunggu frame";
            return false;
        }
        uint8_t buf[8 * 1024];
        std::string e;
        int n = tls_.Recv(buf, sizeof(buf), &e);
        if (n > 0) {
            acc_.insert(acc_.end(), buf, buf + n);
            continue;
        }
        if (n == 0) {
            if (err) *err = "koneksi ditutup peer";
            return false;
        }
        // -1: error atau timeout recv; coba lagi sampai deadline keseluruhan habis.
        if (util::NowMs() >= deadline) {
            if (err) *err = e.empty() ? "timeout menunggu frame" : e;
            return false;
        }
    }
}

bool FramedChannel::WriteFrame(proto::MsgType type, const std::vector<uint8_t>& payload,
                               std::string* err) {
    if (payload.size() > proto::kMaxPayload) {
        if (err) *err = "payload melebihi batas";
        return false;
    }
    auto frame = proto::BuildFrame(type, payload);
    return tls_.Send(frame.data(), frame.size(), err);
}

} // namespace ldr::net
