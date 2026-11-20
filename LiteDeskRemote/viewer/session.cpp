// session.cpp — implementasi ViewerSession + KnownHostStore.
#include "session.h"
#include "../common/protocol/messages.h"
#include "../common/security/security.h"
#include "../common/util/config.h"
#include "../common/util/log.h"
#include "../common/util/time.h"

#if defined(_WIN32)
#include "../common/security/crypto_win.h"
#else
#include "../common/security/crypto_portable.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <thread>

namespace ldr::viewer {

using namespace ldr::proto;
using namespace ldr::net;
using namespace ldr::util;

// ===== KnownHostStore =====
bool KnownHostStore::Load() {
    std::ifstream f(std::filesystem::path(path_), std::ios::binary);
    if (!f) return true; // file belum ada → daftar kosong
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto v = json::Parse(content);
    if (!v || !v->IsObj()) return true;
    const json::Value* arr = v->Find("hosts");
    if (arr && arr->IsArr()) {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& e : arr->arr) {
            KnownHost h;
            if (const json::Value* x = e.Find("device_id")) {
                auto s = x->AsStr(); if (s) h.deviceId = *s;
            }
            if (const json::Value* x = e.Find("name")) {
                auto s = x->AsStr(); if (s) h.name = *s;
            }
            if (const json::Value* x = e.Find("fingerprint_hex")) {
                auto s = x->AsStr(); if (s) h.fingerprintHex = *s;
            }
            if (const json::Value* x = e.Find("added")) {
                auto n = x->AsInt(); if (n) h.added = *n;
            }
            if (!h.deviceId.empty() && !h.fingerprintHex.empty())
                hosts_.push_back(std::move(h));
        }
    }
    return true;
}

bool KnownHostStore::Save(std::string* err) {
    json::Value root = json::Value::ObjectV();
    json::Value arr = json::Value::ArrayV();
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& h : hosts_) {
            json::Value e = json::Value::ObjectV();
            e.Set("device_id", json::Value::StrV(h.deviceId));
            e.Set("name", json::Value::StrV(h.name));
            e.Set("fingerprint_hex", json::Value::StrV(h.fingerprintHex));
            e.Set("added", json::Value::NumV(static_cast<double>(h.added)));
            arr.arr.push_back(std::move(e));
        }
    }
    root.Set("v", json::Value::NumV(1));
    root.Set("hosts", std::move(arr));

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
    std::filesystem::path finalPath(path_);
    std::filesystem::path tmp = finalPath.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (err) *err = "tidak bisa menulis known_hosts.json"; return false; }
        f << json::Dump(root, 2);
        f.flush();
        if (!f.good()) { if (err) *err = "gagal menulis known_hosts.json"; return false; }
    }
    std::filesystem::rename(tmp, finalPath, ec);
    if (ec) { if (err) *err = "gagal rename known_hosts.json"; return false; }
    dirty_ = false;
    return true;
}

const KnownHost* KnownHostStore::Find(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& h : hosts_)
        if (h.deviceId == deviceId) return &h;
    return nullptr;
}

void KnownHostStore::AddOrUpdate(KnownHost h) {
    std::lock_guard<std::mutex> lk(mu_);
    bool found = false;
    for (auto& e : hosts_)
        if (e.deviceId == h.deviceId) {
            e = h;
            found = true;
            break;
        }
    if (!found) hosts_.push_back(std::move(h));
    dirty_ = true;
}

// ===== ViewerSession =====
ViewerSession::~ViewerSession() {
    Disconnect();
    if (th_.joinable()) th_.join();
}

void ViewerSession::SetStatus(ConnState st, const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    state_.state = st;
    state_.status = msg;
}

void ViewerSession::Log(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu_);
    log_.push_back(util::FormatWall(WallNowMs()) + "  " + line);
    if (log_.size() > 500) log_.erase(log_.begin(), log_.begin() + 100);
}

std::vector<std::string> ViewerSession::LogSnapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return log_;
}

ViewerState ViewerSession::Snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return state_;
}

void ViewerSession::Connect(const std::string& endpoint, int port, const std::string& pin) {
    Disconnect();
    if (th_.joinable()) th_.join();
    runId_++;
    stopFlag_ = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_ = ViewerState{};
        state_.port = port;
    }
    th_ = std::thread(&ViewerSession::Run, this, endpoint, port, pin);
}

void ViewerSession::Disconnect() {
    stopFlag_ = true;
    // shutdown socket → recv di Run() langsung gagal → thread keluar cepat
    SOCKET s = curSock_.load();
    if (s != INVALID_SOCKET) shutdown(s, SD_BOTH);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_.state == ConnState::Active) {
            state_.state = ConnState::Idle;
            state_.status = "diputus";
        }
    }
}

void ViewerSession::Run(std::string endpoint, int port, std::string pin) {
    uint64_t myRun = runId_;
    auto stale = [&]() { return stopFlag_ || runId_ != myRun; };

    SetStatus(ConnState::Connecting, "menyambung...");

    // 1. Resolve endpoint
    std::string ip = endpoint;
    int connPort = port > 0 ? port : 48500;
    // format "ip:port"
    auto colon = endpoint.rfind(':');
    if (colon != std::string::npos &&
        endpoint.find_first_not_of("0123456789.:") == std::string::npos) {
        ip = endpoint.substr(0, colon);
        connPort = std::atoi(endpoint.c_str() + colon + 1);
    } else if (endpoint.rfind("LD-", 0) == 0) {
        std::string rip;
        uint16_t rport = 0;
        if (resolver_ && resolver_(endpoint, &rip, &rport)) {
            ip = rip;
            connPort = rport;
            Log("device id " + endpoint + " → " + rip + ":" + std::to_string(rport));
        } else {
            SetStatus(ConnState::Failed,
                      "device id tidak ditemukan di discovery (segarkan daftar atau pakai IP)");
            return;
        }
    }

    // 2. TCP connect
    TcpSocket sock;
    std::string err;
    if (!TcpSocket::Connect(sock, ip, static_cast<uint16_t>(connPort), 8000, &err)) {
        SetStatus(ConnState::Failed, err);
        Log("connect gagal: " + err);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_.hostIp = ip;
        state_.port = static_cast<uint16_t>(connPort);
    }
    curSock_ = sock.Handle();
    Log("tersambung TCP ke " + ip + ":" + std::to_string(connPort));
    if (stale()) return;

    // 3. TLS client handshake + pinning
    SetStatus(ConnState::Tls, "TLS handshake...");
    if (!cred_) cred_ = MakeClientCred(&err);
    if (!cred_) {
        SetStatus(ConnState::Failed, err);
        return;
    }
    TlsChannel tls;
    uint8_t certSha[32];
    if (!tls.HandshakeClient(sock.Handle(), cred_.get(), 10000, &err, certSha)) {
        SetStatus(ConnState::Failed, "TLS: " + err);
        Log("tls gagal: " + err);
        return;
    }
    if (stale()) return;
    std::string fpHex = util::HexEncode(certSha, 32, false);
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_.fingerprintHex = fpHex;
    }
    Log("TLS ok, fingerprint host: " + fpHex.substr(0, 23) + "...");

    // 4. HELLO
    FramedChannel framed(tls);
    std::string viewerId = sec::ComputeDeviceId();
    std::string viewerName = HostNameUtf8();
    Hello hello;
    hello.protoVer = kProtocolVersion;
    hello.kind = 0;
    hello.deviceId = viewerId;
    hello.deviceName = viewerName;
    framed.WriteFrame(MsgType::Hello, EncodeHello(hello));

    Frame f;
    if (!framed.ReadFrame(f, 10000, &err) || f.type != MsgType::HelloAck) {
        SetStatus(ConnState::Failed, "handshake host tidak valid: " + err);
        return;
    }
    auto ack = DecodeHelloAck(f.payload.data(), f.payload.size());
    if (!ack || ack->protoVer != kProtocolVersion) {
        SetStatus(ConnState::Failed, "versi protokol host tidak didukung");
        CloseSession(reason::VersionMismatch, "versi protokol tidak cocok", framed);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_.hostId = ack->deviceId;
        state_.hostName = ack->hostName;
    }

    // 5. Pinning TOFU keyed by device id
    const KnownHost* known = store_.Find(ack->deviceId);
    if (known) {
        std::string stored = known->fingerprintHex;
        // normalisasi (tanpa spasi)
        stored.erase(std::remove(stored.begin(), stored.end(), ' '), stored.end());
        if (stored != fpHex) {
            SetStatus(ConnState::Failed,
                      "fingerprint TLS host BERUBAH! Kemungkinan reinstall host atau serangan "
                      "MITM. Hapus host di halaman Security bila Anda yakin.");
            Log("fingerprint mismatch untuk " + ack->deviceId);
            CloseSession(reason::ProtocolError, "fingerprint mismatch", framed);
            return;
        }
    } else {
        KnownHost h;
        h.deviceId = ack->deviceId;
        h.name = ack->hostName;
        h.fingerprintHex = fpHex;
        h.added = WallNowMs();
        store_.AddOrUpdate(std::move(h));
        std::string e2;
        store_.Save(&e2);
        std::lock_guard<std::mutex> lk(mu_);
        state_.fingerprintNew = true;
    }
    if (stale()) return;

    // 6. AUTH: challenge → response
    SetStatus(ConnState::Auth, "autentikasi PIN...");
    if (!framed.ReadFrame(f, 10000, &err) || f.type != MsgType::AuthChallenge) {
        SetStatus(ConnState::Failed, "challenge tidak valid: " + err);
        return;
    }
    auto ch = DecodeAuthChallenge(f.payload.data(), f.payload.size());
    if (!ch) {
        SetStatus(ConnState::Failed, "challenge tidak dapat di-decode");
        return;
    }
    auto digest = sec::Pbkdf2Sha256(reinterpret_cast<const uint8_t*>(pin.data()), pin.size(),
                                    ch->salt.data(), ch->salt.size(), ch->iterations);
    uint8_t clientNonce[32];
    sec::RandomBytes(clientNonce, sizeof(clientNonce));
    auto proof = sec::ComputeAuthProof(digest.data(), ch->serverNonce.data(), clientNonce,
                                       ack->sessionId, certSha);
    AuthResponse resp;
    std::memcpy(resp.clientNonce.data(), clientNonce, 32);
    std::memcpy(resp.proof.data(), proof.data(), 32);
    resp.deviceId = viewerId;
    resp.deviceName = viewerName;
    resp.requestedPerms = 0x1FULL; // minta semua; host memutuskan subset
    framed.WriteFrame(MsgType::AuthResponse, EncodeAuthResponse(resp));

    if (!framed.ReadFrame(f, 60000, &err) || f.type != MsgType::AuthResult) {
        SetStatus(ConnState::Failed, "auth result tidak valid (host mungkin menunggu "
                                     "konfirmasi / menolak): " + err);
        return;
    }
    auto result = DecodeAuthResult(f.payload.data(), f.payload.size());
    if (!result || !result->ok) {
        SetStatus(ConnState::Failed,
                  "autentikasi ditolak: " + (result ? result->reason : std::string("?")));
        Log("auth gagal");
        return;
    }
    if (stale()) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_.permissions = result->permissions;
        state_.state = ConnState::Active;
        state_.status = "terhubung - streaming layar hadir di FASE 2";
    }
    Log("auth ok");
    SetStatus(ConnState::Active, "terhubung ke " + ack->deviceId +
                                     " - streaming layar hadir di FASE 2");

    // 7. Keepalive + loop pesan
    pingTh_ = std::thread([this, myRun, &framed]() {
        while (!stopFlag_ && runId_ == myRun) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (stopFlag_ || runId_ != myRun) break;
            Ping p;
            p.tSend = static_cast<uint64_t>(NowMs());
            framed.WriteFrame(MsgType::Ping, EncodePing(p));
        }
    });

    while (!stopFlag_ && runId_ == myRun) {
        std::string rerr;
        if (!framed.ReadFrame(f, 20000, &rerr)) {
            if (rerr.find("timeout") == std::string::npos) {
                SetStatus(ConnState::Closed, "host menutup koneksi: " + rerr);
            } else {
                SetStatus(ConnState::Closed, "tidak ada ping dari host (koneksi mati)");
            }
            break;
        }
        bool hostClosed = false;
        switch (f.type) {
            case MsgType::Pong: {
                auto p = DecodePong(f.payload.data(), f.payload.size());
                if (!p) break;
                int rtt = static_cast<int>(NowMs() - static_cast<int64_t>(p->tSend));
                if (rtt >= 0 && rtt < 60000) {
                    std::lock_guard<std::mutex> lk(mu_);
                    state_.latencyMs = rtt;
                }
                break;
            }
            case MsgType::PermissionUpdate: {
                auto pu = DecodePermissionUpdate(f.payload.data(), f.payload.size());
                if (!pu) break;
                std::lock_guard<std::mutex> lk(mu_);
                state_.permissions = pu->permissions;
                Log("izin diperbarui host");
                break;
            }
            case MsgType::SessionClosed: {
                auto sc = DecodeSessionClosed(f.payload.data(), f.payload.size());
                SetStatus(ConnState::Closed,
                          sc ? std::string("host: ") + sc->text : "host menutup sesi");
                hostClosed = true;
                break;
            }
            default:
                Log(std::string("frame ") + MsgTypeName(f.type) + " diterima (FASE 2+)");
                break;
        }
        if (hostClosed) break;
    }

    stopFlag_ = true;
    framed.CloseNotify();
    tls.Close();
    sock.Close();
    curSock_ = INVALID_SOCKET;
    Log("koneksi selesai");
    if (pingTh_.joinable()) pingTh_.join();
}

void ViewerSession::CloseSession(uint8_t code, const std::string& text, FramedChannel& framed) {
    SessionClosed m;
    m.reasonCode = code;
    m.text = text;
    framed.WriteFrame(MsgType::SessionClosed, EncodeSessionClosed(m));
    framed.CloseNotify();
}

} // namespace ldr::viewer
