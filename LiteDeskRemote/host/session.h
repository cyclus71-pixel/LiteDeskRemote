// session.h — SessionManager host: listener TLS, autentikasi PIN, permission,
// konfirmasi akses, keepalive, idle timeout, audit log, batas jumlah sesi.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/networking/framed.h"
#include "../common/networking/net.h"
#include "../common/networking/tls.h"
#include "../common/security/cert_identity.h"
#include "../common/security/rate_limiter.h"
#include "../common/security/security.h"
#include "../common/util/events.h"

namespace ldr::host {

// ===== Permission mask =====
namespace perm {
constexpr uint64_t Mouse = 1ULL << 0;
constexpr uint64_t Keyboard = 1ULL << 1;
constexpr uint64_t Clipboard = 1ULL << 2;
constexpr uint64_t FileTransfer = 1ULL << 3;
constexpr uint64_t Audio = 1ULL << 4;
constexpr uint64_t DefaultMask = Mouse | Keyboard | Clipboard | Audio;
std::string PermissionNames(uint64_t mask);
// Bit yang dibutuhkan untuk tipe pesan tertentu (0 = tidak butuh permission).
uint64_t RequiredFor(proto::MsgType type);
} // namespace perm

// PIN store global host (security/pin.json).
sec::PinStore& PinStoreInstance();

enum class SessionState { Authing, AwaitingConfirm, Active, Closed };

struct SessionInfo {
    uint64_t sessionId = 0;
    std::string ip;
    std::string deviceId;
    std::string deviceName;
    std::string state; // "auth", "konfirmasi", "aktif", "tutup"
    int latencyMs = -1;
    int64_t startedMs = 0;
};

struct ConfirmRequest {
    uint64_t sessionId = 0;
    std::string ip;
    std::string deviceId;
    std::string deviceName;
    std::string permissions;
};

struct AuditEntry {
    int64_t wallMs = 0;
    std::string event;
    std::string ip;
    std::string deviceId;
    std::string detail;
};

class SessionManager {
public:
    // true = terima koneksi. Implementasi UI menampilkan dialog & menunggu jawaban
    // (dengan timeout internal 60 detik).
    using ConfirmProvider = std::function<bool(const ConfirmRequest&)>;

    SessionManager() = default;
    ~SessionManager();

    bool Initialize(std::string* err); // deviceId + identitas TLS + credential schannel
    bool Start(std::string* err);      // Remote Access ON: listener + discovery
    void Stop();                       // Remote Access OFF: tutup semua
    bool Running() const { return running_; }
    uint16_t Port() const { return port_; }
    const std::string& DeviceId() const { return deviceId_; }
    const sec::HostIdentity* Identity() const { return identity_.get(); }
    std::string FingerprintHex() const {
        return identity_ ? identity_->FingerprintHex() : "";
    }

    void SetConfirmProvider(ConfirmProvider f) { confirm_ = std::move(f); }
    void SetDefaultPermissions(uint64_t m) { perms_ = m; }
    uint64_t DefaultPermissions() const { return perms_; }

    std::vector<SessionInfo> Snapshot() const;
    size_t ActiveCount() const;
    std::vector<AuditEntry> AuditSnapshot() const;

private:
    struct SessionCtx {
        uint64_t sessionId = 0;
        net::TcpSocket sock;
        net::TlsChannel tls;
        std::unique_ptr<net::FramedChannel> framed;
        std::string ip;
        std::atomic<int> state{static_cast<int>(SessionState::Authing)};
        std::atomic<int> latency{-1};
        std::mutex infoMu;            // melindungi deviceId/deviceName
        std::string deviceId;
        std::string deviceName;
        std::atomic<bool> stopFlag{false};
        int64_t startedMs = 0;
        std::thread th;
    };

    void AcceptLoop();
    void SessionThread(std::shared_ptr<SessionCtx> ctx);
    void Audit(const char* event, const std::string& ip, const std::string& deviceId,
               const std::string& detail);
    void CloseSession(std::shared_ptr<SessionCtx> ctx, uint8_t reasonCode,
                      const char* text);

    net::WsaInit wsa_;
    net::TcpListener listener_;
    net::DiscoveryResponder responder_;
    std::thread acceptThread_;
    net::CredPtr cred_;
    std::unique_ptr<sec::HostIdentity> identity_;
    std::string deviceId_;
    std::string hostname_;
    sec::RateLimiter limiter_;
    uint64_t perms_ = perm::DefaultMask;
    ConfirmProvider confirm_;
    std::atomic<bool> running_{false};
    uint16_t port_ = 0;
    mutable std::mutex listMu_;
    std::vector<std::shared_ptr<SessionCtx>> sessions_;
    mutable std::mutex auditMu_;
    std::vector<AuditEntry> audit_;
};

} // namespace ldr::host
