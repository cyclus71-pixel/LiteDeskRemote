// session.h — ViewerSession: koneksi ke host (TLS + pinning TOFU), auth PIN,
// keepalive, RTT, status untuk UI, penyimpanan host dikenal (known_hosts.json).
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/networking/framed.h"
#include "../common/networking/net.h"
#include "../common/networking/tls.h"
#include "../common/util/events.h"

namespace ldr::viewer {

enum class ConnState { Idle, Connecting, Tls, Auth, Active, Closed, Failed };

struct ViewerState {
    ConnState state = ConnState::Idle;
    std::string status = "siap";     // pesan status utk UI
    std::string hostId, hostName, hostIp;
    int port = 0;
    int latencyMs = -1;
    uint64_t permissions = 0;
    bool fingerprintNew = false;     // TOFU: fingerprint pertama kali disimpan
    std::string fingerprintHex;
};

struct KnownHost {
    std::string deviceId;
    std::string name;
    std::string fingerprintHex;
    int64_t added = 0;
};

// Penyimpanan host dikenal (pinning sertifikat keyed by device id).
class KnownHostStore {
public:
    explicit KnownHostStore(std::wstring path) : path_(std::move(path)) {}
    bool Load();
    bool Save(std::string* err = nullptr);
    const KnownHost* Find(const std::string& deviceId) const;
    void AddOrUpdate(KnownHost h);
    void Remove(const std::string& deviceId) {
        std::lock_guard<std::mutex> lk(mu_);
        hosts_.erase(std::remove_if(hosts_.begin(), hosts_.end(),
                                    [&](const KnownHost& k) { return k.deviceId == deviceId; }),
                     hosts_.end());
        dirty_ = true;
    }
    void Clear() {
        std::lock_guard<std::mutex> lk(mu_);
        hosts_.clear();
        dirty_ = true;
    }
    std::vector<KnownHost> Snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return hosts_;
    }

private:
    std::wstring path_;
    mutable std::mutex mu_;
    std::vector<KnownHost> hosts_;
    bool dirty_ = false;
};

class ViewerSession {
public:
    // deviceId → (ip, port); dipakai bila endpoint berupa Device ID dari discovery.
    using Resolver = std::function<bool(const std::string& deviceId, std::string* ip,
                                        uint16_t* port)>;

    explicit ViewerSession(KnownHostStore& store) : store_(store) {}
    ~ViewerSession();

    void SetResolver(Resolver r) { resolver_ = std::move(r); }
    // Endpoint: "LD-XXXXX-XXXXX" | "192.168.1.10" | "host:port" | hostname. port = default.
    void Connect(const std::string& endpoint, int port, const std::string& pin);
    void Disconnect();
    ViewerState Snapshot() const;
    std::vector<std::string> LogSnapshot() const;

private:
    void Run(std::string endpoint, int port, std::string pin);
    void SetStatus(ConnState st, const std::string& msg);
    void Log(const std::string& line);
    void CloseSession(uint8_t code, const std::string& text, net::FramedChannel& framed);

    KnownHostStore& store_;
    Resolver resolver_;
    net::WsaInit wsa_;
    net::CredPtr cred_;
    std::thread th_;
    std::thread pingTh_;
    std::atomic<bool> stopFlag_{false};
    std::atomic<SOCKET> curSock_{INVALID_SOCKET}; // utk shutdown dari thread UI
    uint64_t runId_ = 0; // membedakan sesi lama/baru

    mutable std::mutex mu_;
    ViewerState state_;
    std::vector<std::string> log_; // ring log UI
};

} // namespace ldr::viewer
