// session.cpp — implementasi SessionManager host.
#include "session.h"
#include "../common/protocol/messages.h"
#include "../common/util/config.h"
#include "../common/util/log.h"
#include "../common/util/time.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>

namespace ldr::host {

using namespace ldr::proto;
using namespace ldr::net;
using namespace ldr::util;

// ===== Permission =====
namespace perm {

std::string PermissionNames(uint64_t mask) {
    std::string out;
    auto add = [&](const char* n) { if (!out.empty()) out += ", "; out += n; };
    if (mask & Mouse) add("mouse");
    if (mask & Keyboard) add("keyboard");
    if (mask & Clipboard) add("clipboard");
    if (mask & FileTransfer) add("file transfer");
    if (mask & Audio) add("audio");
    return out;
}

uint64_t RequiredFor(MsgType type) {
    switch (type) {
        case MsgType::InputMouse:     return Mouse;
        case MsgType::InputKeyboard:  return Keyboard;
        case MsgType::ClipboardData:  return Clipboard;
        case MsgType::FileChunk:      return FileTransfer;
        case MsgType::AudioData:      return Audio;
        default:                      return 0;
    }
}

} // namespace perm

// ===== Lifecycle =====
SessionManager::~SessionManager() { Stop(); }

bool SessionManager::Initialize(std::string* err) {
    deviceId_ = sec::ComputeDeviceId();
    hostname_ = net::HostNameUtf8();
    identity_ = sec::HostIdentity::LoadOrCreate(Config::I().SecurityDir(), deviceId_, err);
    if (!identity_) return false;
    cred_ = MakeServerCred(identity_->Cert(), err);
    if (!cred_) return false;
    LLOG_I("host init: deviceId=%s fingerprint=%s", deviceId_.c_str(),
           identity_->FingerprintHex().c_str());
    return true;
}

bool SessionManager::Start(std::string* err) {
    if (running_) return true;
    if (!cred_) { if (err) *err = "host belum diinisialisasi"; return false; }

    uint16_t port = static_cast<uint16_t>(Config::I().GetInt("network.tcp_port", 48500));
    if (!listener_.Listen("0.0.0.0", port, err)) return false;
    port_ = listener_.ActualPort();

    bool discovery = Config::I().GetBool("network.discovery_enabled", true);
    if (discovery) {
        std::string derr;
        if (!responder_.Start(Config::I().GetInt("network.discovery_port", 48501),
                              deviceId_, hostname_, port_, &derr)) {
            LLOG_W("discovery tidak aktif: %s", derr.c_str());
        }
    }

    limiter_ = sec::RateLimiter(sec::RateLimiterOptions{});
    running_ = true;
    acceptThread_ = std::thread(&SessionManager::AcceptLoop, this);
    Audit("listener_start", "", deviceId_, "port=" + std::to_string(port_));
    LLOG_I("remote access ON di port %u", port_);
    return true;
}

void SessionManager::Stop() {
    if (!running_) return;
    running_ = false;
    Audit("listener_stop", "", deviceId_, "");
    responder_.Stop();
    listener_.Stop();
    {
        std::lock_guard<std::mutex> lk(listMu_);
        for (auto& s : sessions_) {
            s->stopFlag = true;
            s->sock.Shutdown();
        }
    }
    if (acceptThread_.joinable()) acceptThread_.join();
    // Sesi diberi waktu keluar dengan rapi (thread-nya self-detach).
    for (int i = 0; i < 30; ++i) {
        std::lock_guard<std::mutex> lk(listMu_);
        if (sessions_.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    LLOG_I("remote access OFF");
}

// ===== Accept =====
void SessionManager::AcceptLoop() {
    while (running_) {
        TcpSocket sock;
        std::string err;
        if (!listener_.Accept(sock, 1000, &err)) continue;

        size_t active;
        {
            std::lock_guard<std::mutex> lk(listMu_);
            active = sessions_.size();
        }
        uint64_t maxSessions = static_cast<uint64_t>(
            Config::I().GetInt("network.max_sessions", 3));
        auto ctx = std::make_shared<SessionCtx>();
        ctx->sock = std::move(sock);
        ctx->ip = ctx->sock.PeerIp();
        ctx->sessionId = 0;
        ctx->startedMs = NowMs();

        if (active >= maxSessions) {
            // tolak langsung: tutup socket (tanpa handshake TLS lengkap agar hemat)
            Audit("connect_rejected", ctx->ip, "", "max_sessions");
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(listMu_);
            sessions_.push_back(ctx);
        }
        Audit("connect", ctx->ip, "", "");
        ctx->th = std::thread(&SessionManager::SessionThread, this, ctx);
    }
}

// ===== Sesi =====
void SessionManager::CloseSession(std::shared_ptr<SessionCtx> ctx, uint8_t reasonCode,
                                  const char* text) {
    if (ctx->framed) {
        SessionClosed m;
        m.reasonCode = reasonCode;
        m.text = text;
        ctx->framed->WriteFrame(MsgType::SessionClosed, EncodeSessionClosed(m));
        ctx->framed->CloseNotify();
    }
    ctx->sock.Shutdown();
    ctx->state = static_cast<int>(SessionState::Closed);
}

void SessionManager::SessionThread(std::shared_ptr<SessionCtx> ctx) {
    // Pastikan entri dibersihkan dari daftar saat thread selesai.
    auto selfCleanup = [this, ctx]() {
        std::lock_guard<std::mutex> lk(listMu_);
        auto it = std::find(sessions_.begin(), sessions_.end(), ctx);
        if (it != sessions_.end()) sessions_.erase(it);
        if (ctx->th.joinable()) ctx->th.detach();
    };

    std::string err;
    sec::PinStore& pin = PinStoreInstance();

    // 1. TLS server handshake (10 s)
    ctx->tls.HandshakeServer(ctx->sock.Handle(), cred_.get(), 10000, &err);
    if (!ctx->tls.Active()) {
        LLOG_I("TLS handshake gagal dari %s: %s", ctx->ip.c_str(), err.c_str());
        Audit("tls_failed", ctx->ip, "", err);
        ctx->sock.Close();
        selfCleanup();
        return;
    }
    ctx->framed = std::make_unique<FramedChannel>(ctx->tls);
    ctx->state = static_cast<int>(SessionState::Authing);

    auto failAuth = [&](const char* reason, uint8_t closeCode, bool countFail) {
        if (countFail) limiter_.Fail(ctx->ip);
        AuthResult m;
        m.ok = false;
        m.reason = reason;
        ctx->framed->WriteFrame(MsgType::AuthResult, EncodeAuthResult(m));
        Audit("auth_failed", ctx->ip, "", reason);
        CloseSession(ctx, closeCode, reason);
        selfCleanup();
    };

    // 2. Rate limit
    if (!limiter_.Allowed(ctx->ip)) {
        failAuth("terkunci sementara: terlalu banyak percobaan gagal", reason::LockedOut, false);
        return;
    }
    if (!limiter_.ChallengeAllowed(ctx->ip)) {
        failAuth("terlalu banyak permintaan challenge", reason::ChallengeBudget, false);
        return;
    }

    // 3. HELLO
    Frame f;
    if (!ctx->framed->ReadFrame(f, 10000, &err) || f.type != MsgType::Hello) {
        failAuth("handshake protokol tidak valid", reason::ProtocolError, true);
        return;
    }
    auto hello = DecodeHello(f.payload.data(), f.payload.size());
    if (!hello || hello->protoVer != kProtocolVersion || hello->kind != 0) {
        if (hello && hello->protoVer != kProtocolVersion) {
            CloseSession(ctx, reason::VersionMismatch, "versi protokol tidak cocok");
            Audit("version_mismatch", ctx->ip, hello->deviceId, "");
        } else {
            failAuth("hello tidak valid", reason::ProtocolError, true);
        }
        selfCleanup();
        return;
    }
    {
        uint8_t rnd[8];
        sec::RandomBytes(rnd, sizeof(rnd));
        std::memcpy(&ctx->sessionId, rnd, sizeof(rnd));
    }
    ctx->deviceId = hello->deviceId;
    ctx->deviceName = hello->deviceName;
    {
        std::lock_guard<std::mutex> lk(ctx->infoMu);
        ctx->deviceId = hello->deviceId;
        ctx->deviceName = hello->deviceName;
    }

    HelloAck ack;
    ack.protoVer = kProtocolVersion;
    ack.deviceId = deviceId_;
    ack.hostName = hostname_;
    ack.sessionId = ctx->sessionId;
    ctx->framed->WriteFrame(MsgType::HelloAck, EncodeHelloAck(ack));

    // 4. CHALLENGE
    auto pinRec = pin.Get();
    if (!pinRec) {
        failAuth("host belum mengatur PIN", reason::AuthFailed, false);
        return;
    }
    uint8_t serverNonce[32];
    if (!sec::RandomBytes(serverNonce, sizeof(serverNonce))) {
        failAuth("random gagal", reason::ProtocolError, false);
        return;
    }
    auto salt = sec::Base32Decode(pinRec->saltB64);
    if (salt.size() != 16) {
        failAuth("pin.json korup", reason::ProtocolError, false);
        return;
    }
    AuthChallenge ch;
    std::copy(serverNonce, serverNonce + 32, ch.serverNonce.begin());
    std::copy(salt.begin(), salt.end(), ch.salt.begin());
    ch.iterations = pinRec->iterations;
    ctx->framed->WriteFrame(MsgType::AuthChallenge, EncodeAuthChallenge(ch));

    // 5. RESPONSE
    if (!ctx->framed->ReadFrame(f, 25000, &err) || f.type != MsgType::AuthResponse) {
        failAuth("response tidak valid", reason::ProtocolError, true);
        return;
    }
    auto resp = DecodeAuthResponse(f.payload.data(), f.payload.size());
    if (!resp) {
        failAuth("response tidak dapat di-decode", reason::ProtocolError, true);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(ctx->infoMu);
        ctx->deviceId = resp->deviceId.empty() ? ctx->deviceId : resp->deviceId;
        ctx->deviceName = resp->deviceName.empty() ? ctx->deviceName : resp->deviceName;
    }

    auto digest = sec::Base32Decode(pinRec->digestB64);
    if (digest.size() != 32) {
        failAuth("pin.json korup", reason::ProtocolError, false);
        return;
    }
    uint8_t certSha[32];
    std::memcpy(certSha, identity_->CertSha256(), 32);
    bool ok = sec::VerifyAuthProof(digest.data(), resp->proof.data(), serverNonce,
                                   resp->clientNonce.data(), ctx->sessionId, certSha);
    if (!ok) {
        failAuth("PIN salah", reason::AuthFailed, true);
        return;
    }
    limiter_.Success(ctx->ip);

    // 6. Konfirmasi pengguna (bila tidak unattended / tidak di-auto-accept)
    bool requireConf = Config::I().GetBool("security.require_confirmation", true);
    bool unattended = Config::I().GetBool("security.unattended", false);
    std::vector<std::string> autoAccept =
        Config::I().GetStrArray("security.auto_accept_devices");
    bool trusted = std::find(autoAccept.begin(), autoAccept.end(), ctx->deviceId) !=
                   autoAccept.end();

    if (requireConf && !unattended && !trusted) {
        ctx->state = static_cast<int>(SessionState::AwaitingConfirm);
        Audit("awaiting_confirm", ctx->ip, ctx->deviceId, ctx->deviceName);
        if (confirm_) {
            ConfirmRequest req;
            req.sessionId = ctx->sessionId;
            req.ip = ctx->ip;
            req.deviceId = ctx->deviceId;
            req.deviceName = ctx->deviceName;
            req.permissions = perm::PermissionNames(perms_);
            // Provider UI menunggu jawaban pengguna maks 60 detik.
            bool accepted = confirm_(req);
            if (!accepted) {
                Audit("denied_by_user", ctx->ip, ctx->deviceId, ctx->deviceName);
                AuthResult m;
                m.ok = false;
                m.reason = "ditolak oleh pengguna host";
                ctx->framed->WriteFrame(MsgType::AuthResult, EncodeAuthResult(m));
                CloseSession(ctx, reason::DeniedByUser, "ditolak oleh pengguna host");
                selfCleanup();
                return;
            }
        }
    }

    // 7. AUTH_RESULT → aktif
    uint64_t granted = perms_ & resp->requestedPerms;
    AuthResult result;
    result.ok = true;
    result.permissions = granted;
    if (!ctx->framed->WriteFrame(MsgType::AuthResult, EncodeAuthResult(result), &err)) {
        LLOG_I("gagal kirim auth result: %s", err.c_str());
        selfCleanup();
        return;
    }
    ctx->state = static_cast<int>(SessionState::Active);
    Audit("auth_ok", ctx->ip, ctx->deviceId, ctx->deviceName);
    LLOG_I("sesi aktif sid=%llu ip=%s id=%s",
           static_cast<unsigned long long>(ctx->sessionId), ctx->ip.c_str(),
           ctx->deviceId.c_str());

    // Keepalive thread: host mengirim Ping tiap 5 detik → RTT utk UI + deteksi peer mati.
    std::thread pingTh([ctx]() {
        while (!ctx->stopFlag &&
               ctx->state.load() == static_cast<int>(SessionState::Active)) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (ctx->stopFlag) break;
            Ping p;
            p.tSend = static_cast<uint64_t>(NowMs());
            ctx->framed->WriteFrame(MsgType::Ping, EncodePing(p));
        }
    });
    pingTh.detach();

    // 8. Loop pesan
    int64_t idleMin = Config::I().GetInt("security.idle_timeout_min", 0);
    int64_t lastActivity = NowMs();
    while (!ctx->stopFlag) {
        std::string rerr;
        if (!ctx->framed->ReadFrame(f, 15000, &rerr)) {
            if (rerr.find("timeout") == std::string::npos) {
                Audit("session_closed", ctx->ip, ctx->deviceId, rerr);
                break;
            }
            // recv timeout: peer mati tanpa ping → tutup
            Audit("session_closed", ctx->ip, ctx->deviceId, "tidak ada ping 15 detik");
            break;
        }
        if (f.type != MsgType::Ping && f.type != MsgType::Pong) lastActivity = NowMs();

        switch (f.type) {
            case MsgType::Ping: {
                auto p = DecodePing(f.payload.data(), f.payload.size());
                if (!p) break;
                Pong pong;
                pong.tSend = p->tSend;
                pong.tRecv = static_cast<uint64_t>(NowMs());
                pong.tNow = pong.tRecv;
                ctx->framed->WriteFrame(MsgType::Pong, EncodePong(pong));
                break;
            }
            case MsgType::Pong: {
                auto p = DecodePong(f.payload.data(), f.payload.size());
                if (!p) break;
                int rtt = static_cast<int>(NowMs() - static_cast<int64_t>(p->tSend));
                if (rtt >= 0 && rtt < 60000) ctx->latency = rtt;
                break;
            }
            case MsgType::PermissionUpdate:
                break; // host→viewer arah; diabaikan di host
            case MsgType::ControlStatus: {
                auto c = DecodeControlStatus(f.payload.data(), f.payload.size());
                if (c) Audit("status", ctx->ip, ctx->deviceId, c->text);
                break;
            }
            default: {
                uint64_t need = perm::RequiredFor(f.type);
                if (need && !(granted & need)) {
                    // permission tidak diberikan → tolak pesan
                    ControlStatus st;
                    st.level = 1;
                    st.text = "izin tidak diberikan untuk " +
                              std::string(MsgTypeName(f.type));
                    ctx->framed->WriteFrame(MsgType::ControlStatus, EncodeControlStatus(st));
                    Audit("permission_denied", ctx->ip, ctx->deviceId,
                          MsgTypeName(f.type));
                    break;
                }
                // tipe masa depan (FASE 2+): dicatat lalu diabaikan
                LLOG_D("frame %s diterima (belum ditangani FASE 1)",
                       MsgTypeName(f.type));
                break;
            }
        }

        if (idleMin > 0 && (NowMs() - lastActivity) > idleMin * 60000) {
            Audit("idle_timeout", ctx->ip, ctx->deviceId, "");
            CloseSession(ctx, reason::IdleTimeout, "sesi tidak aktif");
            selfCleanup();
            return;
        }
    }

    ctx->state = static_cast<int>(SessionState::Closed);
    ctx->sock.Shutdown();
    selfCleanup();
}

// ===== State utk UI =====
std::vector<SessionInfo> SessionManager::Snapshot() const {
    std::lock_guard<std::mutex> lk(listMu_);
    std::vector<SessionInfo> out;
    for (const auto& s : sessions_) {
        SessionInfo i;
        i.sessionId = s->sessionId;
        i.ip = s->ip;
        {
            std::lock_guard<std::mutex> l2(s->infoMu);
            i.deviceId = s->deviceId;
            i.deviceName = s->deviceName;
        }
        switch (static_cast<SessionState>(s->state.load())) {
            case SessionState::Authing: i.state = "auth"; break;
            case SessionState::AwaitingConfirm: i.state = "konfirmasi"; break;
            case SessionState::Active: i.state = "aktif"; break;
            default: i.state = "tutup"; break;
        }
        i.latencyMs = s->latency.load();
        i.startedMs = s->startedMs;
        out.push_back(std::move(i));
    }
    return out;
}

size_t SessionManager::ActiveCount() const {
    std::lock_guard<std::mutex> lk(listMu_);
    size_t n = 0;
    for (const auto& s : sessions_)
        if (s->state.load() == static_cast<int>(SessionState::Active)) ++n;
    return n;
}

void SessionManager::Audit(const char* event, const std::string& ip,
                           const std::string& deviceId, const std::string& detail) {
    {
        std::lock_guard<std::mutex> lk(auditMu_);
        audit_.push_back(AuditEntry{WallNowMs(), event, ip, deviceId, detail});
        if (audit_.size() > 500) audit_.erase(audit_.begin(), audit_.begin() + 100);
    }
    sec::AuditAppend(Config::I().AuditLogPath(), event, ip, deviceId, detail);
}

std::vector<AuditEntry> SessionManager::AuditSnapshot() const {
    std::lock_guard<std::mutex> lk(auditMu_);
    return audit_;
}

// ===== PinStore global (pin.json di security dir) =====
sec::PinStore& PinStoreInstance() {
    static sec::PinStore store(Config::I().SecurityPath("pin.json"));
    return store;
}

} // namespace ldr::host
